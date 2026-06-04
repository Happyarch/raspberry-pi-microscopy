#pragma once
#include <string>
#include <cstdint>

// Encodes YUV420 frames to an MKV file.
// Two backends: "builtin" uses V4L2 h264_v4l2m2m + libavformat;
// "ffmpeg" forks an ffmpeg subprocess and pipes raw frames.
class Encoder {
public:
    Encoder(const std::string& backend,
            int width, int height, int fps,
            int bitrate,
            const std::string& ffmpeg_cmd_template);
    ~Encoder();

    bool open(const std::string& output_path);
    // Submit one YUV420 frame (planar: Y then U then V, width*height*3/2 bytes).
    bool submit_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                      int y_stride, int uv_stride);
    void close();

    bool is_open() const { return open_; }

private:
    bool open_builtin(const std::string& path);
    bool open_ffmpeg(const std::string& path);
    bool submit_builtin(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                        int y_stride, int uv_stride);
    bool submit_ffmpeg(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int y_stride, int uv_stride);
    void close_builtin();
    void close_ffmpeg();

    std::string backend_;
    int width_, height_, fps_, bitrate_;
    std::string ffmpeg_cmd_template_;
    bool open_{false};

    // V4L2 builtin state
    int v4l2_fd_{-1};
    int avfmt_ctx_{0};  // opaque — actually AVFormatContext*

    // ffmpeg subprocess state
    int ffmpeg_stdin_{-1};
    int ffmpeg_pid_{-1};
};
