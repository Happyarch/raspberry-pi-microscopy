#include "encoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <regex>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string ffcmd_expand(const std::string& tmpl,
                                int w, int h, int fps,
                                const std::string& output) {
    auto replace = [](std::string s, const std::string& from, const std::string& to) {
        size_t p;
        while ((p = s.find(from)) != std::string::npos) s.replace(p, from.size(), to);
        return s;
    };
    std::string s = tmpl;
    s = replace(s, "{width}",  std::to_string(w));
    s = replace(s, "{height}", std::to_string(h));
    s = replace(s, "{fps}",    std::to_string(fps));
    s = replace(s, "{output}", output);
    return s;
}

// ---------------------------------------------------------------------------
// Encoder public API
// ---------------------------------------------------------------------------

Encoder::Encoder(const std::string& backend,
                 int width, int height, int fps,
                 int bitrate,
                 const std::string& ffmpeg_cmd_template)
    : backend_(backend), width_(width), height_(height),
      fps_(fps), bitrate_(bitrate),
      ffmpeg_cmd_template_(ffmpeg_cmd_template) {}

Encoder::~Encoder() {
    close();
}

bool Encoder::open(const std::string& path) {
    if (open_) return false;
    bool ok = (backend_ == "ffmpeg") ? open_ffmpeg(path) : open_builtin(path);
    open_ = ok;
    return ok;
}

bool Encoder::submit_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                            int y_stride, int uv_stride) {
    if (!open_) return false;
    return (backend_ == "ffmpeg")
        ? submit_ffmpeg(y, u, v, y_stride, uv_stride)
        : submit_builtin(y, u, v, y_stride, uv_stride);
}

void Encoder::close() {
    if (!open_) return;
    if (backend_ == "ffmpeg") close_ffmpeg();
    else                       close_builtin();
    open_ = false;
}

// ---------------------------------------------------------------------------
// FFmpeg subprocess backend
// ---------------------------------------------------------------------------

bool Encoder::open_ffmpeg(const std::string& path) {
    std::string cmd = ffcmd_expand(ffmpeg_cmd_template_, width_, height_, fps_, path);

    int pipefd[2];
    if (pipe(pipefd) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return false; }

    if (pid == 0) {
        // Child: stdin = read end of pipe.
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        // Redirect stderr to /dev/null to keep the UI clean.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(1);
    }

    close(pipefd[0]);
    ffmpeg_stdin_ = pipefd[1];
    ffmpeg_pid_   = pid;
    return true;
}

bool Encoder::submit_ffmpeg(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                             int y_stride, int uv_stride) {
    int uv_h = height_ / 2;
    // Write Y plane.
    for (int r = 0; r < height_; ++r) {
        if (write(ffmpeg_stdin_, y + r * y_stride, width_) < 0) return false;
    }
    // Write U plane.
    for (int r = 0; r < uv_h; ++r) {
        if (write(ffmpeg_stdin_, u + r * uv_stride, width_ / 2) < 0) return false;
    }
    // Write V plane.
    for (int r = 0; r < uv_h; ++r) {
        if (write(ffmpeg_stdin_, v + r * uv_stride, width_ / 2) < 0) return false;
    }
    return true;
}

void Encoder::close_ffmpeg() {
    if (ffmpeg_stdin_ >= 0) { ::close(ffmpeg_stdin_); ffmpeg_stdin_ = -1; }
    if (ffmpeg_pid_ > 0)    { waitpid(ffmpeg_pid_, nullptr, 0); ffmpeg_pid_ = -1; }
}

// ---------------------------------------------------------------------------
// Builtin backend: V4L2 h264_v4l2m2m → libavformat MKV
// ---------------------------------------------------------------------------

static constexpr int kInputBufs  = 4;
static constexpr int kOutputBufs = 4;

struct V4L2Buf {
    void*  ptr;
    size_t len;
    int    index;
};

struct BuiltinCtx {
    int                  v4l2fd{-1};
    std::vector<V4L2Buf> in_bufs;
    std::vector<V4L2Buf> out_bufs;
    AVFormatContext*     fmtctx{nullptr};
    AVStream*            stream{nullptr};
    int64_t              pts{0};
    int                  width{0}, height{0}, fps{0};
};

static bool v4l2_ioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r == 0;
}

// Scan /dev/video0-31 for the bcm2835-codec M2M encoder node.
// Returns an open fd on success, -1 if not found.
static int open_encoder_device() {
    for (int i = 0; i < 32; ++i) {
        std::string dev = "/dev/video" + std::to_string(i);
        int fd = ::open(dev.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        struct v4l2_capability cap{};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            bool is_bcm  = (strncmp((char*)cap.driver, "bcm2835-codec", 13) == 0);
            bool is_m2m  = (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) != 0;
            if (is_bcm && is_m2m) return fd;
        }
        ::close(fd);
    }
    std::cerr << "[encoder] bcm2835-codec M2M encoder not found; "
                 "falling back to /dev/video11\n";
    return ::open("/dev/video11", O_RDWR | O_NONBLOCK);
}

bool Encoder::open_builtin(const std::string& path) {
    auto* ctx = new BuiltinCtx();
    ctx->width = width_; ctx->height = height_; ctx->fps = fps_;

    ctx->v4l2fd = open_encoder_device();
    if (ctx->v4l2fd < 0) { delete ctx; return false; }

    // Set output (encoder input) format: YUV420.
    v4l2_format fmt{};
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width     = width_;
    fmt.fmt.pix_mp.height    = height_;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix_mp.num_planes  = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width_ * height_ * 3 / 2;
    if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_S_FMT, &fmt)) {
        ::close(ctx->v4l2fd); delete ctx; return false;
    }

    // Set capture (encoder output) format: H264.
    fmt = {};
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width     = width_;
    fmt.fmt.pix_mp.height    = height_;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix_mp.num_planes  = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width_ * height_ * 2;
    if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_S_FMT, &fmt)) {
        ::close(ctx->v4l2fd); delete ctx; return false;
    }

    // Set bitrate via V4L2 ext controls.
    v4l2_ext_control ec{};
    ec.id    = V4L2_CID_MPEG_VIDEO_BITRATE;
    ec.value = bitrate_;
    v4l2_ext_controls ecs{};
    ecs.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    ecs.count      = 1;
    ecs.controls   = &ec;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_S_EXT_CTRLS, &ecs);  // best-effort

    // Request + mmap input buffers.
    v4l2_requestbuffers reqbuf{};
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count  = kInputBufs;
    if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_REQBUFS, &reqbuf)) {
        ::close(ctx->v4l2fd); delete ctx; return false;
    }
    for (int i = 0; i < (int)reqbuf.count; ++i) {
        v4l2_buffer buf{}; v4l2_plane plane{};
        buf.type    = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory  = V4L2_MEMORY_MMAP;
        buf.index   = i;
        buf.length  = 1;
        buf.m.planes = &plane;
        v4l2_ioctl(ctx->v4l2fd, VIDIOC_QUERYBUF, &buf);
        void* p = mmap(nullptr, plane.length, PROT_READ|PROT_WRITE, MAP_SHARED,
                       ctx->v4l2fd, plane.m.mem_offset);
        ctx->in_bufs.push_back({p, plane.length, i});
    }

    // Request + mmap output buffers.
    reqbuf = {};
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count  = kOutputBufs;
    if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_REQBUFS, &reqbuf)) {
        ::close(ctx->v4l2fd); delete ctx; return false;
    }
    for (int i = 0; i < (int)reqbuf.count; ++i) {
        v4l2_buffer buf{}; v4l2_plane plane{};
        buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory  = V4L2_MEMORY_MMAP;
        buf.index   = i;
        buf.length  = 1;
        buf.m.planes = &plane;
        v4l2_ioctl(ctx->v4l2fd, VIDIOC_QUERYBUF, &buf);
        void* p = mmap(nullptr, plane.length, PROT_READ|PROT_WRITE, MAP_SHARED,
                       ctx->v4l2fd, plane.m.mem_offset);
        ctx->out_bufs.push_back({p, plane.length, i});
        // Queue capture buffers immediately.
        v4l2_plane qp{}; qp.length = plane.length;
        buf.m.planes = &qp;
        v4l2_ioctl(ctx->v4l2fd, VIDIOC_QBUF, &buf);
    }

    // Start streaming on both sides.
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_STREAMON, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_STREAMON, &type);

    // Open MKV output via libavformat.
    if (avformat_alloc_output_context2(&ctx->fmtctx, nullptr, "matroska",
                                       path.c_str()) < 0) {
        ::close(ctx->v4l2fd); delete ctx; return false;
    }
    ctx->stream = avformat_new_stream(ctx->fmtctx, nullptr);
    ctx->stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx->stream->codecpar->codec_id   = AV_CODEC_ID_H264;
    ctx->stream->codecpar->width      = width_;
    ctx->stream->codecpar->height     = height_;
    ctx->stream->time_base            = {1, fps_};

    if (!(ctx->fmtctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ctx->fmtctx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
            ::close(ctx->v4l2fd); avformat_free_context(ctx->fmtctx);
            delete ctx; return false;
        }
    }
    avformat_write_header(ctx->fmtctx, nullptr);

    builtin_ctx_ = ctx;
    return true;
}

bool Encoder::submit_builtin(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int y_stride, int uv_stride) {
    auto* ctx = builtin_ctx_;

    // Find a free input buffer.
    v4l2_buffer buf{}; v4l2_plane plane{};
    buf.type    = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory  = V4L2_MEMORY_MMAP;
    buf.length  = 1;
    buf.m.planes = &plane;
    if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_DQBUF, &buf)) return false;

    V4L2Buf& ib = ctx->in_bufs[buf.index];
    uint8_t* dst = static_cast<uint8_t*>(ib.ptr);
    int uv_h = ctx->height / 2;

    // Pack YUV420 planar into the V4L2 buffer.
    for (int r = 0; r < ctx->height; ++r)
        memcpy(dst + r * ctx->width, y + r * y_stride, ctx->width);
    dst += ctx->width * ctx->height;
    for (int r = 0; r < uv_h; ++r)
        memcpy(dst + r * (ctx->width/2), u + r * uv_stride, ctx->width/2);
    dst += ctx->width / 2 * uv_h;
    for (int r = 0; r < uv_h; ++r)
        memcpy(dst + r * (ctx->width/2), v + r * uv_stride, ctx->width/2);

    plane.bytesused = ctx->width * ctx->height * 3 / 2;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_QBUF, &buf);

    // Drain any available encoded packets.
    pollfd pfd{ctx->v4l2fd, POLLIN, 0};
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        v4l2_buffer obuf{}; v4l2_plane oplane{};
        obuf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        obuf.memory  = V4L2_MEMORY_MMAP;
        obuf.length  = 1;
        obuf.m.planes = &oplane;
        if (!v4l2_ioctl(ctx->v4l2fd, VIDIOC_DQBUF, &obuf)) break;

        V4L2Buf& ob = ctx->out_bufs[obuf.index];
        AVPacket* pkt = av_packet_alloc();
        pkt->data     = static_cast<uint8_t*>(ob.ptr);
        pkt->size     = oplane.bytesused;
        pkt->pts      = ctx->pts++;
        pkt->dts      = pkt->pts;
        av_packet_rescale_ts(pkt, {1, ctx->fps}, ctx->stream->time_base);
        pkt->stream_index = ctx->stream->index;
        av_interleaved_write_frame(ctx->fmtctx, pkt);
        av_packet_free(&pkt);

        // Re-queue capture buffer.
        oplane.length = ob.len;
        v4l2_ioctl(ctx->v4l2fd, VIDIOC_QBUF, &obuf);
    }
    return true;
}

void Encoder::close_builtin() {
    auto* ctx = builtin_ctx_;
    if (!ctx) return;

    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_ioctl(ctx->v4l2fd, VIDIOC_STREAMOFF, &type);

    for (auto& b : ctx->in_bufs)  munmap(b.ptr, b.len);
    for (auto& b : ctx->out_bufs) munmap(b.ptr, b.len);
    ::close(ctx->v4l2fd);

    av_write_trailer(ctx->fmtctx);
    if (!(ctx->fmtctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ctx->fmtctx->pb);
    avformat_free_context(ctx->fmtctx);

    delete ctx;
    builtin_ctx_ = nullptr;
}
