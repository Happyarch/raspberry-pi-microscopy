//
// Tests for the GET /api/media/<id>[/thumb] routes added to MjpegServer.
// Exercises: no-db 404, unknown-id 404, file serving (200), content-type
// detection, cached-thumbnail serving, and HTTP Range/206 support.
//
#include "util/mjpeg_server.h"
#include "util/media_db.h"
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers shared with test_mjpeg_server.cpp (re-declared locally)
// ---------------------------------------------------------------------------

static int alloc_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd); return -1;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    ::close(fd);
    return ntohs(addr.sin_port);
}

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

static int tcp_connect_retry(int port) {
    for (int i = 0; i < 100; ++i) {
        int fd = tcp_connect(port);
        if (fd >= 0) return fd;
        std::this_thread::sleep_for(5ms);
    }
    return -1;
}

static std::string http_get(int port, const std::string& path) {
    int fd = tcp_connect(port);
    if (fd < 0) return "";
    std::string req = "GET " + path + " HTTP/1.0\r\n\r\n";
    ::send(fd, req.c_str(), req.size(), MSG_NOSIGNAL);
    std::string resp;
    char buf[8192];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return resp;
}

// GET with a Range header.
static std::string http_get_range(int port, const std::string& path,
                                  const std::string& range) {
    int fd = tcp_connect(port);
    if (fd < 0) return "";
    std::string req = "GET " + path + " HTTP/1.0\r\nRange: bytes=" + range + "\r\n\r\n";
    ::send(fd, req.c_str(), req.size(), MSG_NOSIGNAL);
    std::string resp;
    char buf[8192];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return resp;
}

// ---------------------------------------------------------------------------
// Fixture — sets up MediaDb + MjpegServer in a temp tree
// ---------------------------------------------------------------------------

class MjpegMediaTest : public ::testing::Test {
protected:
    std::string root_, stills_, videos_, tl_, thumbs_;
    std::unique_ptr<MediaDb>    db_;
    std::unique_ptr<MjpegServer> srv_;
    int port_{-1};

    void SetUp() override {
        char tmp[] = "/tmp/mjpeg_media_test_XXXXXX";
        char* r = mkdtemp(tmp);
        ASSERT_NE(r, nullptr);
        root_   = r;
        stills_ = root_ + "/stills";
        videos_ = root_ + "/videos";
        tl_     = root_ + "/tl";
        thumbs_ = root_ + "/thumbs";
        fs::create_directories(stills_);
        fs::create_directories(videos_);
        fs::create_directories(tl_);
        fs::create_directories(thumbs_);

        db_ = std::make_unique<MediaDb>(root_ + "/media.db", stills_, videos_, tl_);

        port_ = alloc_port();
        ASSERT_GT(port_, 0);
        srv_ = std::make_unique<MjpegServer>(port_, 75, 1.0f, 30);
        ASSERT_TRUE(srv_->ok());
        srv_->set_media_db(db_.get(), thumbs_);

        int probe = tcp_connect_retry(port_);
        ASSERT_GE(probe, 0) << "server not listening";
        ::close(probe);
    }

    void TearDown() override {
        srv_.reset();
        db_.reset();
        fs::remove_all(root_);
    }

    std::string make_file(const std::string& dir, const std::string& name,
                          const std::string& content = "testcontent") {
        std::string p = dir + "/" + name;
        std::ofstream f(p); f << content;
        return p;
    }

    std::string url(int64_t id) {
        return "/api/media/" + std::to_string(id);
    }
    std::string thumb_url(int64_t id) {
        return "/api/media/" + std::to_string(id) + "/thumb";
    }
};

// ---------------------------------------------------------------------------
// No MediaDb configured
// ---------------------------------------------------------------------------

TEST(MjpegMediaNoDb, GET_Media_NoDbSet_Returns404) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());
    // Do NOT call set_media_db — db_ remains null.

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0); ::close(probe);

    auto resp = http_get(port, "/api/media/1");
    EXPECT_NE(resp.find("404"), std::string::npos);
}

TEST(MjpegMediaNoDb, GET_Thumb_NoDbSet_Returns404) {
    int port = alloc_port();
    ASSERT_GT(port, 0);
    MjpegServer srv(port, 75, 1.0f, 30);
    ASSERT_TRUE(srv.ok());

    int probe = tcp_connect_retry(port);
    ASSERT_GE(probe, 0); ::close(probe);

    auto resp = http_get(port, "/api/media/1/thumb");
    EXPECT_NE(resp.find("404"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unknown ID
// ---------------------------------------------------------------------------

TEST_F(MjpegMediaTest, GET_Media_UnknownId_Returns404) {
    auto resp = http_get(port_, url(9999));
    EXPECT_NE(resp.find("404"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Media_BadId_Returns404) {
    auto resp = http_get(port_, "/api/media/not-a-number");
    EXPECT_NE(resp.find("404"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Thumb_UnknownId_Returns404) {
    auto resp = http_get(port_, thumb_url(9999));
    EXPECT_NE(resp.find("404"), std::string::npos);
}

// ---------------------------------------------------------------------------
// File serving — 200 OK
// ---------------------------------------------------------------------------

TEST_F(MjpegMediaTest, GET_Media_ValidStill_Returns200WithContent) {
    std::string p  = make_file(stills_, "img.jpg", "jpeg-payload");
    int64_t    id  = db_->add_still(p);
    ASSERT_GT(id, 0);

    auto resp = http_get(port_, url(id));
    EXPECT_NE(resp.find("200"),          std::string::npos);
    EXPECT_NE(resp.find("jpeg-payload"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Media_ValidStill_ContentType_Jpeg) {
    int64_t id = db_->add_still(make_file(stills_, "shot.jpg"));
    ASSERT_GT(id, 0);
    auto resp = http_get(port_, url(id));
    EXPECT_NE(resp.find("image/jpeg"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Media_ValidVideo_ContentType_Matroska) {
    int64_t id = db_->add_video(make_file(videos_, "clip.mkv"));
    ASSERT_GT(id, 0);
    auto resp = http_get(port_, url(id));
    EXPECT_NE(resp.find("video/x-matroska"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Media_AcceptsRangesHeader_Present) {
    int64_t id = db_->add_still(make_file(stills_, "ranges.jpg", "0123456789"));
    ASSERT_GT(id, 0);
    auto resp = http_get(port_, url(id));
    EXPECT_NE(resp.find("Accept-Ranges: bytes"), std::string::npos);
}

// ---------------------------------------------------------------------------
// HTTP Range / 206 Partial Content
// ---------------------------------------------------------------------------

TEST_F(MjpegMediaTest, GET_Media_RangeRequest_Returns206) {
    // 10-byte file; request first 5 bytes (bytes=0-4).
    int64_t id = db_->add_still(make_file(stills_, "range.jpg", "0123456789"));
    ASSERT_GT(id, 0);

    auto resp = http_get_range(port_, url(id), "0-4");
    EXPECT_NE(resp.find("206"), std::string::npos)
        << "expected 206 Partial Content; got: " << resp.substr(0, 128);
    EXPECT_NE(resp.find("Content-Range"), std::string::npos);
    EXPECT_NE(resp.find("01234"), std::string::npos)
        << "expected first 5 bytes in body";
}

TEST_F(MjpegMediaTest, GET_Media_RangeRequest_CorrectContentRange) {
    int64_t id = db_->add_still(make_file(stills_, "cr.jpg", "abcdefghij")); // 10 bytes
    ASSERT_GT(id, 0);

    auto resp = http_get_range(port_, url(id), "2-5");
    EXPECT_NE(resp.find("206"),                   std::string::npos);
    EXPECT_NE(resp.find("Content-Range: bytes 2-5/10"), std::string::npos);
    EXPECT_NE(resp.find("cdef"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Media_NoRangeHeader_Returns200) {
    int64_t id = db_->add_still(make_file(stills_, "full.jpg", "full-data"));
    ASSERT_GT(id, 0);

    auto resp = http_get(port_, url(id));
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_EQ(resp.find("206"),    std::string::npos); // must NOT be 206
    EXPECT_NE(resp.find("full-data"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Thumbnail serving — cache hit path
// ---------------------------------------------------------------------------

TEST_F(MjpegMediaTest, GET_Thumb_CachedThumbnail_Returns200) {
    int64_t id = db_->add_still(make_file(stills_, "cached.jpg", "src"));
    ASSERT_GT(id, 0);

    // Pre-create the cache file that serve_thumbnail looks for.
    make_file(thumbs_, std::to_string(id) + ".jpg", "thumb-bytes");

    auto resp = http_get(port_, thumb_url(id));
    EXPECT_NE(resp.find("200"),         std::string::npos);
    EXPECT_NE(resp.find("thumb-bytes"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Thumb_CachedThumbnail_ContentType_Jpeg) {
    int64_t id = db_->add_still(make_file(stills_, "ct.jpg"));
    ASSERT_GT(id, 0);
    make_file(thumbs_, std::to_string(id) + ".jpg", "x");

    auto resp = http_get(port_, thumb_url(id));
    EXPECT_NE(resp.find("image/jpeg"), std::string::npos);
}

TEST_F(MjpegMediaTest, GET_Thumb_NoCache_InvalidJpeg_Returns404) {
    // File is not a real JPEG; libturbojpeg decompress will fail → 404.
    int64_t id = db_->add_still(make_file(stills_, "bad.jpg", "not-a-jpeg"));
    ASSERT_GT(id, 0);

    auto resp = http_get(port_, thumb_url(id));
    EXPECT_NE(resp.find("404"), std::string::npos);
}
