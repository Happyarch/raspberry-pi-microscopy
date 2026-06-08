#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class MjpegServer {
public:
    // https=true requires cert_file and key_file (PEM). Falls back to HTTP on error.
    MjpegServer(int port, int jpeg_quality, float scale, int max_fps,
                bool https = false,
                const std::string& cert_file = {},
                const std::string& key_file  = {});
    ~MjpegServer();

    MjpegServer(const MjpegServer&) = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;

    bool ok() const { return listen_fd_ >= 0; }

    // Push a raw YUV420 viewfinder frame. Called from main loop — fast memcpy only.
    void push_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                    int w, int h, int y_stride, int uv_stride);

    // Swap in a new status JSON string for GET /api/status. Called from main loop.
    void set_status(const std::string& json);

    // Pop one pending REST command from the queue. Returns true if cmd_out is filled.
    // Caller must invoke reply_fn exactly once to complete the HTTP response.
    bool pop_command(std::string& cmd_out,
                     std::function<void(const std::string&)>& reply_fn);

private:
    struct YuvBuf {
        std::vector<uint8_t> data;
        int w{0}, h{0}, y_stride{0}, uv_stride{0};
    };

    struct PendingCmd {
        std::string cmd;
        std::shared_ptr<std::promise<std::string>> reply;
    };

    void encode_loop();
    void listen_loop();
    void client_loop(int fd, void* ssl); // ssl is SSL* cast to void* (avoids OpenSSL in header)

    int port_, quality_, max_fps_;
    float scale_;
    bool  https_{false};
    void* ssl_ctx_{nullptr}; // SSL_CTX*; non-null only when https_ is true and init succeeded
    int listen_fd_{-1};

    // YUV ping-pong (main writes, encode reads — no lock needed for the data copy)
    YuvBuf yuv_[2];
    int yuv_write_{0};    // main thread writes to this slot
    int yuv_latest_{-1};  // most recently completed slot (set by main, read by encode)
    std::mutex yuv_mtx_;
    std::condition_variable yuv_cv_;
    uint64_t yuv_seq_{0};

    // JPEG output — shared_ptr so multiple stream threads ref-count without copying
    std::mutex jpeg_mtx_;
    std::condition_variable jpeg_cv_;
    std::shared_ptr<const std::vector<uint8_t>> jpeg_ptr_;
    uint64_t jpeg_seq_{0};

    // Status JSON
    std::mutex status_mtx_;
    std::string status_json_{"{}"};

    // Command queue (REST → main loop)
    std::mutex cmd_mtx_;
    std::queue<PendingCmd> cmd_queue_;

    std::atomic<bool> stopping_{false};
    std::atomic<int>  stream_count_{0};
    std::thread encode_thread_;
    std::thread listen_thread_;
};
