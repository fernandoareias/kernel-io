#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <system_error>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Bypass spdlog's internal try/catch (SPDLOG_LOGGER_CATCH), which expands to a
// FMT_STRING compile-time check that fails with clang 21 + fmt 10.2.1. We
// don't rely on spdlog throwing on formatting errors here, so disabling the
// catch is safe.
#define SPDLOG_NO_EXCEPTIONS
#include <spdlog/spdlog.h>

namespace {

class unique_fd {
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept : fd_{fd} {}
    ~unique_fd() { reset(); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    unique_fd(unique_fd&& other) noexcept : fd_{other.release()} {}
    unique_fd& operator=(unique_fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.release();
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] int release() noexcept {
        int f = fd_;
        fd_ = -1;
        return f;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_{-1};
};

constexpr std::uint16_t kPort         = 1815;
constexpr int           kBacklog      = 128;
constexpr std::size_t   kReadBufBytes = 4096;
constexpr int           kEventBatch   = 64;

std::atomic<bool> g_stop{false};

extern "C" void on_signal(int /*signo*/) noexcept {
    g_stop.store(true, std::memory_order_relaxed);
}

void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = &on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART: we want kevent() to return EINTR so
                      // the loop sees g_stop promptly.
    if (::sigaction(SIGINT, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "sigaction SIGINT");
    }
    if (::sigaction(SIGTERM, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "sigaction SIGTERM");
    }

    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    if (::sigaction(SIGPIPE, &ign, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "sigaction SIGPIPE");
    }
}

void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl F_GETFL");
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl F_SETFL O_NONBLOCK");
    }
}

[[nodiscard]] unique_fd make_listen_socket(std::uint16_t port) {
    unique_fd fd{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!fd) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");
    }

    set_nonblocking(fd.get());

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == -1) {
        throw std::system_error(errno, std::generic_category(), "bind");
    }
    if (::listen(fd.get(), kBacklog) == -1) {
        throw std::system_error(errno, std::generic_category(), "listen");
    }

    return fd;
}

[[nodiscard]] unique_fd make_kqueue() {
    unique_fd kq{::kqueue()};
    if (!kq) {
        throw std::system_error(errno, std::generic_category(), "kqueue");
    }
    return kq;
}

void kq_register(int kq, int fd, std::int16_t filter, std::uint16_t flags) {
    struct kevent change{};
    EV_SET(&change, static_cast<std::uintptr_t>(fd), filter, flags, 0, 0, nullptr);
    if (::kevent(kq, &change, 1, nullptr, 0, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "kevent register");
    }
}

void handle_accept_burst(int listen_fd, int kq, std::intptr_t pending) {
    // kqueue sets ev.data to the backlog length; loop until that many accepts
    // succeed or the kernel queue drains (EAGAIN/EWOULDBLOCK on a non-blocking
    // listen socket).
    std::intptr_t cap = std::max<std::intptr_t>(pending, 1);
    for (std::intptr_t i = 0; i < cap; ++i) {
        sockaddr_storage ss{};
        socklen_t        len = sizeof(ss);
        int raw = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&ss), &len);
        if (raw == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // queue drained
            }
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            spdlog::warn("accept: {}", std::strerror(errno));
            break;
        }

        unique_fd client{raw};
        try {
            set_nonblocking(client.get());
            kq_register(kq, client.get(), EVFILT_READ, EV_ADD | EV_CLEAR);
        } catch (const std::system_error& e) {
            spdlog::warn("failed to register client fd={}: {}", client.get(), e.what());
            continue;  // unique_fd dtor closes the client
        }

        spdlog::info("accepted fd={}", client.get());
        (void)client.release();  // ownership now tracked by kqueue + close()
    }
}

void handle_client_read(const struct kevent& ev) {
    int fd = static_cast<int>(ev.ident);

    // kqueue tells us exactly how many bytes are currently buffered. We must
    // drain them even if EV_EOF is set — EV_EOF means "peer closed its write
    // half", NOT "discard the data still in the receive buffer".
    std::intptr_t remaining   = ev.data;
    bool          peer_closed = (ev.flags & EV_EOF) != 0;

    std::array<char, kReadBufBytes> buf{};
    while (true) {
        ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n > 0) {
            spdlog::info("fd={} read {} bytes", fd, n);
            remaining -= n;
            if (remaining <= 0 && !peer_closed) {
                break;  // drained what kqueue promised
            }
            continue;
        }
        if (n == 0) {
            peer_closed = true;
            break;
        }
        // n == -1
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        spdlog::warn("recv fd={}: {}", fd, std::strerror(errno));
        peer_closed = true;
        break;
    }

    if (peer_closed) {
        spdlog::info("closing fd={}", fd);
        ::close(fd);  // kqueue removes the event automatically on close()
    }
}

int run() {
    install_signal_handlers();

    unique_fd listen_fd = make_listen_socket(kPort);
    unique_fd kq        = make_kqueue();

    kq_register(kq.get(), listen_fd.get(), EVFILT_READ, EV_ADD | EV_ENABLE);

    spdlog::info("kqueue TCP server listening on 0.0.0.0:{}", kPort);

    std::array<struct kevent, kEventBatch> events{};

    while (!g_stop.load(std::memory_order_relaxed)) {
        timespec timeout{1, 0};  // 1s wake-up to poll g_stop
        int n = ::kevent(kq.get(), nullptr, 0, events.data(),
                         static_cast<int>(events.size()), &timeout);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "kevent wait");
        }

        for (int i = 0; i < n; ++i) {
            const auto& ev = events[static_cast<std::size_t>(i)];
            int fd = static_cast<int>(ev.ident);

            if ((ev.flags & EV_ERROR) != 0) {
                spdlog::warn("kevent error fd={}: {}", fd,
                             std::strerror(static_cast<int>(ev.data)));
                continue;
            }

            if (fd == listen_fd.get()) {
                handle_accept_burst(listen_fd.get(), kq.get(), ev.data);
                continue;
            }

            // ev.filter is a single enum value, not a bitmask.
            if (ev.filter == EVFILT_READ) {
                handle_client_read(ev);
            }
        }
    }

    spdlog::info("shutdown requested, exiting loop");
    return 0;
}

}  // namespace

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    try {
        return run();
    } catch (const std::exception& e) {
        spdlog::critical("fatal: {}", e.what());
        return 1;
    }
}
