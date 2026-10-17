#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// External control channel.
//
// Native builds open a Unix-domain socket and speak line-delimited JSON on it,
// so an outside process (the MCP bridge, a script, Claude Code) can drive the
// DAW. The socket thread performs I/O only — it never touches DAW state. Every
// parsed request is handed to the GUI thread through a mailbox and executed
// from WindowHandler::tick() -> ControlServer::poll(), because both graph reads
// and graph mutations are only safe on the thread that owns the graph.
//
// Under Emscripten (no POSIX sockets) every method is a no-op so callers need
// no platform guards.
class ControlServer {
public:
    static ControlServer& instance();

    void start(const std::string& socketPath);
    void stop();
    void poll();                        // GUI thread only
    bool isRunning() const { return running_.load(); }

private:
    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    struct Message {
        int fd = -1;
        std::string line;
    };

    void serverLoop();

    std::atomic<bool> running_{false};
    std::thread thread_;
    int listenFd_ = -1;
    std::string path_;

    std::mutex mutex_;
    std::vector<Message> inbox_;    // socket thread -> GUI thread
    std::vector<Message> outbox_;   // GUI thread   -> socket thread
};
