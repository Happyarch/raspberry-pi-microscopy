#include "util/mjpeg_server.h"
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Allocate an ephemeral port so tests don't collide with each other or the app.
static int alloc_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0; // let OS choose
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd); return -1;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    ::close(fd);
    return ntohs(addr.sin_port);
}

// Open a TCP connection to localhost:port. Returns fd or -1.
static int tcp_connect(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd); return -1;
    }
    return fd;
}

// Send a raw string to an fd.
static bool tcp_send(int fd, const std::string& s) {
    ssize_t n = ::send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
    return n == static_cast<ssize_t>(s.size());
}

// Synchronous HTTP GET: sends request, reads full response (connection closed by server).
static std::string http_get(int port, const std::string& path) {
    int fd = tcp_connect(port);
    if (fd < 0) return "";
    tcp_send(fd, "GET " + path + " HTTP/1.0\r\n\r\n");
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return resp;
}

// Synchronous HTTP POST: sends request, waits for server to reply then close.
// For commands that block on the command queue this must be run in a thread
// while the test's "main loop" calls pop_command + reply_fn.
static std::string http_post(int port, const std::string& path) {
    int fd = tcp_connect(port);
    if (fd < 0) return "";
    tcp_send(fd, "POST " + path + " HTTP/1.0\r\nContent-Length: 0\r\n\r\n");
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return resp;
}

// Minimal 16×16 YUV420 frame filled with a flat gray value.
struct YuvFrame16 {
    std::array<uint8_t, 16*16>   y;
    std::array<uint8_t, 8*8>     u;
    std::array<uint8_t, 8*8>     v;
    YuvFrame16() { y.fill(128); u.fill(128); v.fill(128); }
};

// Poll pop_command up to `tries` times (1 ms apart) until it returns true.
static bool drain_command(MjpegServer& srv, std::string& cmd_out,
                           std::function<void(const std::string&)>& reply_out,
                           int tries = 500) {
    for (int i = 0; i < tries; ++i) {
        if (srv.pop_command(cmd_out, reply_out)) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(MjpegServer, StartsOnValidPort) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    EXPECT_TRUE(srv.ok());
}

TEST(MjpegServer, FailsOnPrivilegedPort) {
    // Port 1 requires CAP_NET_BIND_SERVICE. Under normal test execution this fails.
    MjpegServer srv(1, 75, 1.0f, 30);
    EXPECT_FALSE(srv.ok());
}

// ---------------------------------------------------------------------------
// No-client safety
// ---------------------------------------------------------------------------

TEST(MjpegServer, PushFrameWithNoClients_NoCrash) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());
    YuvFrame16 f;
    EXPECT_NO_THROW(srv.push_frame(f.y.data(), f.u.data(), f.v.data(), 16, 16, 16, 8));
}

TEST(MjpegServer, SetStatusWithNoClients_NoCrash) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());
    EXPECT_NO_THROW(srv.set_status(R"({"mode":"P","iso":0})"));
}

TEST(MjpegServer, PopCommandEmptyQueue_ReturnsFalse) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());
    std::string cmd;
    std::function<void(const std::string&)> reply;
    EXPECT_FALSE(srv.pop_command(cmd, reply));
}

// ---------------------------------------------------------------------------
// HTTP routing — wait for listen_thread to be ready
// ---------------------------------------------------------------------------

// Retry tcp_connect until the server is accepting (up to 500 ms).
static int tcp_connect_retry(int port) {
    for (int i = 0; i < 100; ++i) {
        int fd = tcp_connect(port);
        if (fd >= 0) return fd;
        std::this_thread::sleep_for(5ms);
    }
    return -1;
}

TEST(MjpegServer, HTTP_GET_Root_ReturnsHtml) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    // Wait until the listen thread is ready
    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0) << "server not listening";
    ::close(probe);

    std::string resp = http_get(port, "/");
    EXPECT_NE(resp.find("200 OK"), std::string::npos) << "expected HTTP 200";
    EXPECT_NE(resp.find("text/html"), std::string::npos) << "expected text/html content-type";
    EXPECT_NE(resp.find("<!DOCTYPE html"), std::string::npos) << "expected HTML body";
}

TEST(MjpegServer, HTTP_GET_Unknown_Returns404) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    std::string resp = http_get(port, "/no-such-path");
    EXPECT_NE(resp.find("404"), std::string::npos);
}

TEST(MjpegServer, HTTP_GET_Status_ReturnsJson) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    srv.set_status(R"({"mode":"S","iso":400})");

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    std::string resp = http_get(port, "/api/status");
    EXPECT_NE(resp.find("application/json"), std::string::npos);
    EXPECT_NE(resp.find("\"mode\""), std::string::npos);
    EXPECT_NE(resp.find("iso"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Command queue roundtrip
// ---------------------------------------------------------------------------

TEST(MjpegServer, POST_Command_PushedToQueue) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    // POST /api/ping in a background thread (will block until reply_fn is called)
    std::string http_resp;
    std::thread requester([&]{
        http_resp = http_post(port, "/api/ping");
    });

    // Act as the main loop: drain the command queue
    std::string cmd;
    std::function<void(const std::string&)> reply_fn;
    ASSERT_TRUE(drain_command(srv, cmd, reply_fn));
    EXPECT_EQ(cmd, "ping");

    // Fulfill the promise — unblocks the HTTP response
    reply_fn("PONG");
    requester.join();

    EXPECT_NE(http_resp.find("PONG"), std::string::npos);
}

TEST(MjpegServer, POST_Command_UrlDecoded) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    std::thread requester([&]{
        http_post(port, "/api/focus%200.35"); // "focus 0.35"
    });

    std::string cmd;
    std::function<void(const std::string&)> reply_fn;
    ASSERT_TRUE(drain_command(srv, cmd, reply_fn));
    EXPECT_EQ(cmd, "focus 0.35");
    reply_fn("OK");
    requester.join();
}

TEST(MjpegServer, POST_MultipleCommands_ServedInOrder) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    // Issue two commands sequentially (each completes before the next is issued)
    for (const char* verb : {"iso%20400", "af%20on"}) {
        std::string resp;
        std::thread t([&, verb]{
            resp = http_post(port, std::string("/api/") + verb);
        });
        std::string cmd;
        std::function<void(const std::string&)> reply_fn;
        ASSERT_TRUE(drain_command(srv, cmd, reply_fn));
        reply_fn("OK " + cmd);
        t.join();
        EXPECT_NE(resp.find("OK"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// MJPEG stream
// ---------------------------------------------------------------------------

TEST(MjpegServer, Stream_ReceivesMjpegContentType) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    // Push a frame so the encode thread has a JPEG ready
    YuvFrame16 f;
    srv.push_frame(f.y.data(), f.u.data(), f.v.data(), 16, 16, 16, 8);

    // Wait for encode to finish (16×16 at q75 is ~1 ms; 200 ms is very conservative)
    std::this_thread::sleep_for(200ms);

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    int fd = tcp_connect(port);
    ASSERT_GE(fd, 0);
    tcp_send(fd, "GET /stream HTTP/1.0\r\n\r\n");

    // Read enough to see the HTTP headers + first multipart boundary
    std::string resp;
    char buf[4096];
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n > 0) resp.assign(buf, static_cast<size_t>(n));

    ::close(fd);

    EXPECT_NE(resp.find("multipart/x-mixed-replace"), std::string::npos)
        << "expected MJPEG content-type in: " << resp.substr(0, 256);
    EXPECT_NE(resp.find("mjpegframe"), std::string::npos)
        << "expected boundary 'mjpegframe' in: " << resp.substr(0, 256);
}

TEST(MjpegServer, Stream_ContainsJpegFrame) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    YuvFrame16 f;
    srv.push_frame(f.y.data(), f.u.data(), f.v.data(), 16, 16, 16, 8);
    std::this_thread::sleep_for(200ms); // allow encode to complete

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    int fd = tcp_connect(port);
    ASSERT_GE(fd, 0);
    // Short per-read timeout so we stop once the server stops sending
    struct timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tcp_send(fd, "GET /stream HTTP/1.0\r\n\r\n");

    // Read in a loop: the server sends headers + boundary + JPEG in separate writes.
    // Stop once we've seen the boundary or the socket times out / closes.
    std::string resp;
    char buf[8192];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(n));
        if (resp.find("Content-Length:") != std::string::npos) break;
    }

    ::close(fd);

    EXPECT_NE(resp.find("--mjpegframe"), std::string::npos)
        << "expected boundary in stream response";
    EXPECT_NE(resp.find("Content-Type: image/jpeg"), std::string::npos)
        << "expected image/jpeg in frame header";
    EXPECT_NE(resp.find("Content-Length:"), std::string::npos)
        << "expected Content-Length in frame header";
}

// ---------------------------------------------------------------------------
// HTTPS — TLS wrapping via OpenSSL
// ---------------------------------------------------------------------------

// Generate a temporary P-256 self-signed cert+key using the openssl CLI.
// Returns true and fills cert_path / key_path with /tmp paths on success.
static bool gen_test_cert(std::string& cert_path, std::string& key_path) {
    cert_path = "/tmp/test_mjpeg_cert.pem";
    key_path  = "/tmp/test_mjpeg_key.pem";
    std::string cmd =
        "openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256"
        " -keyout " + key_path +
        " -out " + cert_path +
        " -days 1 -nodes -subj '/CN=test' 2>/dev/null";
    return std::system(cmd.c_str()) == 0;
}

// TLS GET helper: wraps tcp_connect in an SSL client session.
static std::string https_get(int port, const std::string& path) {
    int fd = tcp_connect(port);
    if (fd < 0) return "";

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // accept self-signed
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl); SSL_CTX_free(ctx); ::close(fd); return "";
    }

    std::string req = "GET " + path + " HTTP/1.0\r\n\r\n";
    SSL_write(ssl, req.c_str(), static_cast<int>(req.size()));

    std::string resp;
    char buf[4096];
    int  n;
    while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0)
        resp.append(buf, static_cast<size_t>(n));

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return resp;
}

TEST(MjpegServer, HTTPS_FallsBackToHttpWhenNoCert) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    // https=true but empty cert/key — server must start anyway (as plain HTTP)
    MjpegServer srv(port, 75, 1.0f, 30, true, "", "");
    EXPECT_TRUE(srv.ok()) << "server should still bind even when TLS setup fails";
}

TEST(MjpegServer, HTTPS_FallsBackToHttpOnMissingCertFile) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30, true, "/nonexistent.crt", "/nonexistent.key");
    EXPECT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0) << "server not listening after fallback to HTTP";
    ::close(probe);

    // Must serve plain HTTP (no TLS)
    std::string resp = http_get(port, "/");
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
}

TEST(MjpegServer, HTTPS_ServesPageOverTls) {
    std::string cert, key;
    if (!gen_test_cert(cert, key)) {
        GTEST_SKIP() << "openssl CLI not available — skipping TLS test";
    }

    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30, true, cert, key);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0) << "HTTPS server not listening";
    ::close(probe);

    std::string resp = https_get(port, "/");
    EXPECT_NE(resp.find("200 OK"), std::string::npos) << "expected HTTP 200 over TLS";
    EXPECT_NE(resp.find("text/html"), std::string::npos);
    EXPECT_NE(resp.find("<!DOCTYPE html"), std::string::npos);

    std::remove(cert.c_str());
    std::remove(key.c_str());
}

TEST(MjpegServer, HTTPS_StatusEndpointOverTls) {
    std::string cert, key;
    if (!gen_test_cert(cert, key)) GTEST_SKIP() << "openssl CLI not available";

    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30, true, cert, key);
    ASSERT_TRUE(srv.ok());
    srv.set_status(R"({"mode":"P","https":true})");

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    std::string resp = https_get(port, "/api/status");
    EXPECT_NE(resp.find("application/json"), std::string::npos);
    EXPECT_NE(resp.find("https"), std::string::npos);

    std::remove(cert.c_str());
    std::remove(key.c_str());
}

// ---------------------------------------------------------------------------
// Destructor — should not deadlock or crash even with active clients
// ---------------------------------------------------------------------------

TEST(MjpegServer, DestructorWithActiveStreamClient_NoCrash) {
    int port = alloc_port();
    ASSERT_GT(port, 0);

    int client_fd = -1;
    {
        MjpegServer srv(port, 75, 1.0f, 30);
        ASSERT_TRUE(srv.ok());

        YuvFrame16 f;
        srv.push_frame(f.y.data(), f.u.data(), f.v.data(), 16, 16, 16, 8);
        std::this_thread::sleep_for(150ms);

        int probe = tcp_connect_retry(port);
        ASSERT_GE(probe, 0);
        ::close(probe);

        client_fd = tcp_connect(port);
        ASSERT_GE(client_fd, 0);
        tcp_send(client_fd, "GET /stream HTTP/1.0\r\n\r\n");

        std::this_thread::sleep_for(50ms); // let stream thread enter jpeg_cv_.wait

        // srv destructor called here — must not deadlock
    }

    // Clean up the client connection after destructor returns
    if (client_fd >= 0) ::close(client_fd);
}
