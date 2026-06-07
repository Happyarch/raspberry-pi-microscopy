#pragma once
#include <string>

// Non-blocking Unix domain socket server — one connection at a time.
// Poll once per frame from the main loop; no extra threads required.
//
// Protocol: newline-terminated UTF-8 text lines in both directions.
// Client sends one command per line; server replies with one line per command.
class SocketServer {
public:
    // Creates and binds the socket at `path`. Parent directory is created if
    // it does not exist. Any stale socket file at `path` is removed first.
    explicit SocketServer(const std::string& path);
    ~SocketServer(); // closes fds and removes the socket file

    // Non-copying.
    SocketServer(const SocketServer&)            = delete;
    SocketServer& operator=(const SocketServer&) = delete;

    // Returns true when the server socket was created successfully.
    bool ok() const { return listen_fd_ >= 0; }

    // Non-blocking poll. Accepts new connections, reads buffered data.
    // If a complete '\n'-terminated line is available, stores it (without the
    // newline) in `cmd_out` and returns true. Call reply() once per true return.
    bool poll(std::string& cmd_out);

    // Send a response line to the current client (appends '\n').
    // No-op if there is no connected client.
    void reply(const std::string& msg);

private:
    std::string path_;
    int         listen_fd_{-1};
    int         client_fd_{-1};
    std::string recv_buf_;
};
