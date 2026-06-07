#include "socket_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static bool set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

SocketServer::SocketServer(const std::string& path) : path_(path) {
    if (path.empty()) return;

    fs::create_directories(fs::path(path).parent_path());
    ::unlink(path.c_str()); // remove stale socket

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[socket] socket(): " << ::strerror(errno) << "\n";
        return;
    }

    set_nonblocking(listen_fd_);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[socket] bind(" << path << "): " << ::strerror(errno) << "\n";
        ::close(listen_fd_); listen_fd_ = -1;
        return;
    }
    if (::listen(listen_fd_, 4) != 0) {
        std::cerr << "[socket] listen(): " << ::strerror(errno) << "\n";
        ::close(listen_fd_); listen_fd_ = -1;
        return;
    }

    std::cerr << "[socket] listening on " << path << "\n";
}

SocketServer::~SocketServer() {
    if (client_fd_ >= 0) ::close(client_fd_);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (!path_.empty())  ::unlink(path_.c_str());
}

bool SocketServer::poll(std::string& cmd_out) {
    if (listen_fd_ < 0) return false;

    // Accept any new connection — replaces the current client.
    int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd >= 0) {
        if (client_fd_ >= 0) ::close(client_fd_);
        client_fd_ = fd;
        set_nonblocking(client_fd_);
        recv_buf_.clear();
    }

    if (client_fd_ < 0) return false;

    // Read whatever is available.
    char buf[512];
    ssize_t n = ::read(client_fd_, buf, sizeof(buf));
    if (n > 0) {
        recv_buf_.append(buf, static_cast<size_t>(n));
    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        ::close(client_fd_);
        client_fd_ = -1;
        recv_buf_.clear();
        return false;
    }

    // Extract the first complete line.
    auto pos = recv_buf_.find('\n');
    if (pos == std::string::npos) return false;

    cmd_out = recv_buf_.substr(0, pos);
    if (!cmd_out.empty() && cmd_out.back() == '\r') // strip CRLF
        cmd_out.pop_back();
    recv_buf_.erase(0, pos + 1);
    return !cmd_out.empty(); // skip blank lines
}

void SocketServer::reply(const std::string& msg) {
    if (client_fd_ < 0) return;
    std::string out = msg + "\n";
    ssize_t n = ::write(client_fd_, out.c_str(), out.size());
    if (n < 0 && errno != EAGAIN) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
}
