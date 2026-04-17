/*
 * io_uring + thread pool — GET para www.google.com
 * Otimizações aplicadas:
 *   - Ring thread-local (inicializado uma vez por thread, reutilizado entre requests)
 *   - Buffer registrado via io_uring_register_buffers (elimina overhead de mapeamento de página)
 *   - Linked SQEs: connect→send em um único io_uring_submit()
 *   - response.reserve() para evitar realocações O(n²)
 *   - SOCK_NONBLOCK para compatibilidade com o modelo assíncrono do io_uring
 */

#include <liburing.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

static constexpr int RING_DEPTH = 64;
static constexpr int BUF_SIZE   = 16384;

struct ThreadRing {
    struct io_uring ring{};
    char            buf[BUF_SIZE]{};
    struct iovec    iov{};
    bool            ready = false;

    bool init() {
        if (io_uring_queue_init(RING_DEPTH, &ring, 0) < 0)
            return false;
        iov.iov_base = buf;
        iov.iov_len  = BUF_SIZE;
        if (io_uring_register_buffers(&ring, &iov, 1) < 0) {
            io_uring_queue_exit(&ring);
            return false;
        }
        ready = true;
        return true;
    }

    ~ThreadRing() {
        if (ready) io_uring_queue_exit(&ring);
    }
};

thread_local ThreadRing tl_ring;


class ThreadPool {
public:
    explicit ThreadPool(size_t n) : stop_(false) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
                // Inicializa o ring uma única vez para este thread
                if (!tl_ring.ready && !tl_ring.init()) {
                    std::cerr << "[thread] falha ao iniciar io_uring ring\n";
                    return;
                }
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx_);
                        cv_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    void enqueue(std::function<void()> f) {
        { std::lock_guard<std::mutex> lock(mtx_); tasks_.push(std::move(f)); }
        cv_.notify_one();
    }

    ~ThreadPool() {
        { std::lock_guard<std::mutex> lock(mtx_); stop_ = true; }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    bool                              stop_;
};


void do_http_get(int id) {
    if (!tl_ring.ready) {
        std::cerr << "[req " << id << "] ring não inicializado\n";
        return;
    }
    struct io_uring* ring = &tl_ring.ring;

    const char* ip      = "142.251.152.119";
    const int   port    = 80;
    const char* req_str = "GET / HTTP/1.0\r\nHost: www.google.com\r\nConnection: close\r\n\r\n";

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        std::cerr << "[req " << id << "] inet_pton falhou\n";
        return;
    }

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        std::cerr << "[req " << id << "] socket() falhou\n";
        return;
    }

    struct io_uring_sqe* sqe;
    struct io_uring_cqe* cqe;

    sqe = io_uring_get_sqe(ring);
    io_uring_prep_connect(sqe, fd,
        reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    sqe->flags    |= IOSQE_IO_LINK;
    sqe->user_data = 1;

    sqe = io_uring_get_sqe(ring);
    io_uring_prep_send(sqe, fd, req_str, strlen(req_str), 0);
    sqe->user_data = 2;

    io_uring_submit(ring); 

    if (io_uring_wait_cqe(ring, &cqe) < 0 || cqe->res < 0) {
        std::cerr << "[req " << id << "] connect falhou: "
                  << strerror(cqe ? -cqe->res : errno) << "\n";
        if (cqe) io_uring_cqe_seen(ring, cqe);
        if (io_uring_wait_cqe(ring, &cqe) == 0)
            io_uring_cqe_seen(ring, cqe);
        close(fd);
        return;
    }
    io_uring_cqe_seen(ring, cqe);
    std::cout << "[req " << id << "] Conectado em " << ip << ":" << port << "\n";

    if (io_uring_wait_cqe(ring, &cqe) < 0 || cqe->res < 0) {
        std::cerr << "[req " << id << "] send falhou\n";
        if (cqe) io_uring_cqe_seen(ring, cqe);
        close(fd);
        return;
    }
    std::cout << "[req " << id << "] Enviou " << cqe->res << " bytes\n";
    io_uring_cqe_seen(ring, cqe);

    std::string response;
    response.reserve(64 * 1024);

    while (true) {
        sqe = io_uring_get_sqe(ring);
        io_uring_prep_read_fixed(sqe, fd, tl_ring.buf, BUF_SIZE - 1, 0, 0);
        sqe->user_data = 3;
        io_uring_submit(ring);

        if (io_uring_wait_cqe(ring, &cqe) < 0) break;
        int n = cqe->res;
        io_uring_cqe_seen(ring, cqe);
        if (n <= 0) break;
        response.append(tl_ring.buf, static_cast<std::size_t>(n));
    }

    auto nl = response.find('\n');
    std::cout << "[req " << id << "] Status: "
              << (nl != std::string::npos ? response.substr(0, nl) : response.substr(0, 80))
              << "\n";

    close(fd);
}


int main() {
    const int THREADS  = 4;
    const int REQUESTS = 8;

    std::cout << "Pool de " << THREADS << " threads, "
              << REQUESTS << " requests\n\n";

    ThreadPool pool(THREADS);
    for (int i = 0; i < REQUESTS; ++i)
        pool.enqueue([i] { do_http_get(i); });

    return 0;
}
