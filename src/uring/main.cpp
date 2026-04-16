/*
 * io_uring + thread pool — GET para www.google.com
 * Sem DNS: usa IP fixo via inet_pton()
 *
 * Compilar:
 *   g++ io_uring_example.cpp -o io_uring_example -luring -std=c++17
 */

#include <liburing.h>
#include <sys/socket.h>
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


class ThreadPool {
public:
    explicit ThreadPool(size_t n) : stop_(false) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
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

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[req " << id << "] socket() falhou\n";
        return;
    }

    struct io_uring ring{};
    if (io_uring_queue_init(32, &ring, 0) < 0) {
        std::cerr << "[req " << id << "] io_uring_queue_init falhou\n";
        close(fd);
        return;
    }

    struct io_uring_sqe* sqe = nullptr;
    struct io_uring_cqe* cqe = nullptr;


    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_connect(sqe, fd,
        reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    sqe->user_data = 1;
    io_uring_submit(&ring);

    if (io_uring_wait_cqe(&ring, &cqe) < 0 || cqe->res < 0) {
        std::cerr << "[req " << id << "] connect falhou: "
                  << strerror(cqe ? -cqe->res : errno) << "\n";
        if (cqe) io_uring_cqe_seen(&ring, cqe);
        goto cleanup;
    }
    io_uring_cqe_seen(&ring, cqe);
    std::cout << "[req " << id << "] Conectado em " << ip << ":" << port << "\n";


    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_send(sqe, fd, req_str, strlen(req_str), 0);
    sqe->user_data = 2;
    io_uring_submit(&ring);

    if (io_uring_wait_cqe(&ring, &cqe) < 0 || cqe->res < 0) {
        std::cerr << "[req " << id << "] send falhou\n";
        if (cqe) io_uring_cqe_seen(&ring, cqe);
        goto cleanup;
    }
    std::cout << "[req " << id << "] Enviou " << cqe->res << " bytes\n";
    io_uring_cqe_seen(&ring, cqe);


    {
        char buf[4096];
        std::string response;

        while (true) {
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_recv(sqe, fd, buf, sizeof(buf) - 1, 0);
            sqe->user_data = 3;
            io_uring_submit(&ring);

            if (io_uring_wait_cqe(&ring, &cqe) < 0) break;
            int n = cqe->res;
            io_uring_cqe_seen(&ring, cqe);
            if (n <= 0) break;
            buf[n] = '\0';
            response += buf;
        }

        auto nl = response.find('\n');
        std::cout << "[req " << id << "] Status: "
                  << (nl != std::string::npos ? response.substr(0, nl) : response.substr(0, 80))
                  << "\n";
    }

cleanup:
    io_uring_queue_exit(&ring);
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