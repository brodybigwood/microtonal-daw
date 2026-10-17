#include "ControlServer.h"
#include "ControlApi.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef __EMSCRIPTEN__

// No POSIX sockets under Emscripten: the control channel is a no-op there.
ControlServer& ControlServer::instance() { static ControlServer s; return s; }
ControlServer::~ControlServer() {}
void ControlServer::start(const std::string&) {}
void ControlServer::stop() {}
void ControlServer::poll() {}

#else

#include <cerrno>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr size_t kMaxLineBytes = 64u * 1024u * 1024u; // guard against runaway clients
constexpr size_t kMaxRequestsPerFrame = 256;          // keeps one client from stalling a frame
constexpr int    kPollTimeoutMs = 50;
}

ControlServer& ControlServer::instance() { static ControlServer s; return s; }

ControlServer::~ControlServer() { stop(); }

void ControlServer::start(const std::string& socketPath) {
    if (running_.load()) return;

    if (socketPath.empty()) {
        std::cerr << "[Control] empty socket path, not starting" << std::endl;
        return;
    }

    sockaddr_un addr{};
    if (socketPath.size() >= sizeof(addr.sun_path)) {
        std::cerr << "[Control] socket path too long: " << socketPath << std::endl;
        return;
    }

    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "[Control] socket() failed: " << std::strerror(errno) << std::endl;
        return;
    }

    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    ::unlink(socketPath.c_str()); // clear a stale socket left by a previous run
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Control] bind(" << socketPath << ") failed: " << std::strerror(errno) << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }
    if (::listen(listenFd_, 8) < 0) {
        std::cerr << "[Control] listen() failed: " << std::strerror(errno) << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }
    ::chmod(socketPath.c_str(), 0600);

    path_ = socketPath;
    running_.store(true);
    thread_ = std::thread(&ControlServer::serverLoop, this);
    std::cout << "[Control] listening on " << path_ << std::endl;
}

void ControlServer::stop() {
    if (!running_.exchange(false)) {
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        return;
    }
    if (thread_.joinable()) thread_.join();
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
        path_.clear();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.clear();
    outbox_.clear();
}

void ControlServer::serverLoop() {
    std::vector<int> clients;
    std::map<int, std::string> buffers;

    while (running_.load()) {
        std::vector<pollfd> fds;
        fds.push_back(pollfd{listenFd_, POLLIN, 0});
        for (int c : clients) fds.push_back(pollfd{c, POLLIN, 0});

        const int rc = ::poll(fds.data(), fds.size(), kPollTimeoutMs);

        if (rc > 0 && (fds[0].revents & POLLIN)) {
            const int c = ::accept(listenFd_, nullptr, nullptr);
            if (c >= 0) clients.push_back(c);
        }

        for (size_t i = 1; rc > 0 && i < fds.size(); ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const int fd = fds[i].fd;

            char buf[8192];
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                ::close(fd);
                clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                buffers.erase(fd);
                continue;
            }

            std::string& acc = buffers[fd];
            acc.append(buf, static_cast<size_t>(n));
            if (acc.size() > kMaxLineBytes) {
                ::close(fd);
                clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                buffers.erase(fd);
                continue;
            }

            // Complete lines become requests; the remainder stays buffered.
            size_t start = 0;
            for (;;) {
                const size_t nl = acc.find('\n', start);
                if (nl == std::string::npos) break;
                std::string line = acc.substr(start, nl - start);
                start = nl + 1;
                if (line.empty()) continue;
                std::lock_guard<std::mutex> lock(mutex_);
                inbox_.push_back(Message{fd, std::move(line)});
            }
            acc.erase(0, start);
        }

        // Send any responses the GUI thread produced. Sending happens here, never
        // on the GUI thread, so a client that stops reading cannot stall a frame.
        std::vector<Message> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            out.swap(outbox_);
        }
        for (auto& m : out) {
            if (m.fd < 0) continue;
            size_t sent = 0;
            while (sent < m.line.size()) {
                const ssize_t n = ::send(m.fd, m.line.data() + sent, m.line.size() - sent, MSG_NOSIGNAL);
                if (n <= 0) break;
                sent += static_cast<size_t>(n);
            }
        }
    }

    for (int c : clients) ::close(c);
}

void ControlServer::poll() {
    std::vector<Message> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t n = std::min(inbox_.size(), kMaxRequestsPerFrame);
        batch.assign(std::make_move_iterator(inbox_.begin()),
                     std::make_move_iterator(inbox_.begin() + static_cast<long>(n)));
        inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<long>(n));
    }
    if (batch.empty()) return;

    std::vector<Message> responses;
    responses.reserve(batch.size());
    for (auto& req : batch) {
        std::string reply = ControlApi::handle(req.line);
        if (reply.empty()) continue;
        reply.push_back('\n');
        responses.push_back(Message{req.fd, std::move(reply)});
    }
    if (responses.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : responses) outbox_.push_back(std::move(r));
}

#endif
