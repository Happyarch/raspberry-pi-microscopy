#include "util/socket_server.h"
#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string tmp_sock_path(const std::string& suffix = "") {
    return (fs::temp_directory_path() / ("microscopi_test_sock" + suffix + ".sock")).string();
}

// Connect to a Unix socket. Returns the fd or -1.
static int connect_client(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// Send a line to an open client fd.
static void client_send(int fd, const std::string& line) {
    std::string msg = line + "\n";
    ::write(fd, msg.c_str(), msg.size());
}

// Read one '\n'-terminated line from an open client fd (blocking, 1 s timeout).
static std::string client_recv(int fd) {
    struct timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string buf;
    char c;
    while (::read(fd, &c, 1) == 1) {
        if (c == '\n') break;
        buf += c;
    }
    return buf;
}

// Poll the server up to N times (1 ms apart) until poll() returns true.
static bool poll_until(SocketServer& srv, std::string& out, int tries = 50) {
    for (int i = 0; i < tries; ++i) {
        if (srv.poll(out)) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(SocketServer, DisabledOnEmptyPath) {
    SocketServer srv("");
    EXPECT_FALSE(srv.ok());
    std::string cmd;
    EXPECT_FALSE(srv.poll(cmd)); // must not crash
}

TEST(SocketServer, CreatesSocketFile) {
    std::string path = tmp_sock_path("_create");
    ::unlink(path.c_str());
    {
        SocketServer srv(path);
        EXPECT_TRUE(srv.ok());
        EXPECT_TRUE(fs::exists(path));
    }
    // Destructor removes the socket file.
    EXPECT_FALSE(fs::exists(path));
}

TEST(SocketServer, BadDirectoryDoesNotCrash) {
    // /proc is read-only for ordinary users.
    SocketServer srv("/proc/microscopi_no_write/test.sock");
    EXPECT_FALSE(srv.ok());
    std::string cmd;
    EXPECT_FALSE(srv.poll(cmd));
}

// ---------------------------------------------------------------------------
// No client
// ---------------------------------------------------------------------------

TEST(SocketServer, PollReturnsFalseWhenNoClient) {
    std::string path = tmp_sock_path("_noclient");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());
    std::string cmd;
    EXPECT_FALSE(srv.poll(cmd));
}

TEST(SocketServer, ReplyIsNoopWhenNoClient) {
    std::string path = tmp_sock_path("_noop");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());
    EXPECT_NO_THROW(srv.reply("OK")); // must not crash
}

// ---------------------------------------------------------------------------
// Basic send / receive
// ---------------------------------------------------------------------------

TEST(SocketServer, ReceivesCommandFromClient) {
    std::string path = tmp_sock_path("_recv");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    client_send(cfd, "ping");

    std::string cmd;
    EXPECT_TRUE(poll_until(srv, cmd));
    EXPECT_EQ(cmd, "ping");

    ::close(cfd);
}

TEST(SocketServer, SendsReplyToClient) {
    std::string path = tmp_sock_path("_reply");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    client_send(cfd, "ping");

    std::string cmd;
    ASSERT_TRUE(poll_until(srv, cmd));
    srv.reply("PONG");

    EXPECT_EQ(client_recv(cfd), "PONG");

    ::close(cfd);
}

TEST(SocketServer, MultipleCommandsOnSameConnection) {
    std::string path = tmp_sock_path("_multi");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    for (const auto& verb : {"ping", "status", "help"}) {
        client_send(cfd, verb);
        std::string cmd;
        ASSERT_TRUE(poll_until(srv, cmd));
        EXPECT_EQ(cmd, verb);
        srv.reply("OK " + std::string(verb));
        EXPECT_EQ(client_recv(cfd), std::string("OK ") + verb);
    }

    ::close(cfd);
}

// ---------------------------------------------------------------------------
// CRLF tolerance
// ---------------------------------------------------------------------------

TEST(SocketServer, StripsCarriageReturn) {
    std::string path = tmp_sock_path("_crlf");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    // Send with CRLF (as telnet would).
    std::string msg = "ping\r\n";
    ::write(cfd, msg.c_str(), msg.size());

    std::string cmd;
    ASSERT_TRUE(poll_until(srv, cmd));
    EXPECT_EQ(cmd, "ping"); // \r must be stripped

    ::close(cfd);
}

// ---------------------------------------------------------------------------
// Client disconnect
// ---------------------------------------------------------------------------

TEST(SocketServer, HandlesClientDisconnect) {
    std::string path = tmp_sock_path("_disc");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    client_send(cfd, "ping");
    std::string cmd;
    ASSERT_TRUE(poll_until(srv, cmd));
    srv.reply("PONG");

    ::close(cfd); // disconnect

    // poll() should detect EOF and return false without crashing.
    std::this_thread::sleep_for(5ms);
    EXPECT_FALSE(srv.poll(cmd));
}

TEST(SocketServer, AcceptsNewClientAfterDisconnect) {
    std::string path = tmp_sock_path("_reconnect");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    // First client.
    {
        int cfd = connect_client(path);
        ASSERT_GE(cfd, 0);
        client_send(cfd, "first");
        std::string cmd;
        ASSERT_TRUE(poll_until(srv, cmd));
        EXPECT_EQ(cmd, "first");
        ::close(cfd);
    }

    std::this_thread::sleep_for(5ms);

    // Second client after first disconnected.
    {
        int cfd = connect_client(path);
        ASSERT_GE(cfd, 0);
        client_send(cfd, "second");
        std::string cmd;
        ASSERT_TRUE(poll_until(srv, cmd));
        EXPECT_EQ(cmd, "second");
        srv.reply("OK");
        EXPECT_EQ(client_recv(cfd), "OK");
        ::close(cfd);
    }
}

// ---------------------------------------------------------------------------
// New connection bumps old one
// ---------------------------------------------------------------------------

TEST(SocketServer, NewConnectionBumpsOldClient) {
    std::string path = tmp_sock_path("_bump");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd1 = connect_client(path);
    ASSERT_GE(cfd1, 0);

    // Drain any accept for cfd1.
    std::string cmd;
    srv.poll(cmd);

    // Connect second client — server should replace cfd1.
    int cfd2 = connect_client(path);
    ASSERT_GE(cfd2, 0);

    client_send(cfd2, "hello");
    ASSERT_TRUE(poll_until(srv, cmd));
    EXPECT_EQ(cmd, "hello");
    srv.reply("PONG");
    EXPECT_EQ(client_recv(cfd2), "PONG");

    ::close(cfd1);
    ::close(cfd2);
}

// ---------------------------------------------------------------------------
// Blank lines are ignored
// ---------------------------------------------------------------------------

TEST(SocketServer, IgnoresBlankLines) {
    std::string path = tmp_sock_path("_blank");
    SocketServer srv(path);
    ASSERT_TRUE(srv.ok());

    int cfd = connect_client(path);
    ASSERT_GE(cfd, 0);

    // Send a blank line then a real command.
    ::write(cfd, "\n\nping\n", 7);

    std::string cmd;
    ASSERT_TRUE(poll_until(srv, cmd, 100));
    EXPECT_EQ(cmd, "ping");

    ::close(cfd);
}
