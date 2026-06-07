#include "camera.h"
#include <libcamera/controls.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/property_ids.h>
#include <sys/mman.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <cstring>
// Import libcamera names selectively — do NOT import libcamera::Camera since
// it clashes with our own Camera class defined in camera.h.
using libcamera::CameraManager;
using libcamera::CameraConfiguration;
using libcamera::FrameBufferAllocator;
using libcamera::Request;
using libcamera::ControlList;
using libcamera::StreamRole;
using libcamera::Span;
namespace controls = libcamera::controls;
namespace formats  = libcamera::formats;

Camera::Camera(int index) : index_(index) {
    mgr_ = std::make_shared<CameraManager>();
    if (mgr_->start() != 0)
        throw std::runtime_error("Failed to start CameraManager");

    auto cameras = mgr_->cameras();
    if (cameras.empty())
        throw std::runtime_error("No cameras found");
    if (index >= (int)cameras.size())
        throw std::runtime_error("Camera index out of range");

    cam_ = cameras[index];
    if (cam_->acquire() != 0)
        throw std::runtime_error("Failed to acquire camera");
}

Camera::~Camera() {
    stop();
    if (cam_) cam_->release();
    if (mgr_) mgr_->stop();
}

bool Camera::start(int width, int height, int fps) {
    width_ = width; height_ = height; fps_ = fps;
    dual_stream_ = false;
    vf_stream_ = still_stream_ = nullptr;

    // Attempt dual-stream (Viewfinder + StillCapture). If the camera or
    // validate() rejects it, fall back to single Viewfinder.
    cfg_ = cam_->generateConfiguration({StreamRole::Viewfinder, StreamRole::StillCapture});
    if (cfg_) {
        cfg_->at(0).pixelFormat = formats::YUV420;
        cfg_->at(0).size        = {(unsigned)width, (unsigned)height};
        cfg_->at(0).bufferCount = 4;
        cfg_->at(1).pixelFormat = formats::YUV420;
        cfg_->at(1).bufferCount = 1;
        if (cfg_->validate() != CameraConfiguration::Invalid)
            dual_stream_ = true;
    }
    if (!dual_stream_) {
        cfg_ = cam_->generateConfiguration({StreamRole::Viewfinder});
        if (!cfg_) return false;
        cfg_->at(0).pixelFormat = formats::YUV420;
        cfg_->at(0).size        = {(unsigned)width, (unsigned)height};
        cfg_->at(0).bufferCount = 4;
        if (cfg_->validate() == CameraConfiguration::Invalid) return false;
    }

    if (cam_->configure(cfg_.get()) != 0) return false;

    vf_stream_ = cfg_->at(0).stream();
    allocator_  = new FrameBufferAllocator(cam_);
    if (allocator_->allocate(vf_stream_) < 0) return false;

    if (dual_stream_) {
        still_stream_ = cfg_->at(1).stream();
        still_w_      = (int)cfg_->at(1).size.width;
        still_h_      = (int)cfg_->at(1).size.height;
        still_stride_ = (int)cfg_->at(1).stride;
        still_alloc_  = new FrameBufferAllocator(cam_);
        if (still_alloc_->allocate(still_stream_) < 0) {
            delete still_alloc_; still_alloc_ = nullptr;
            still_stream_ = nullptr;
            dual_stream_  = false;
        }
    }

    std::cerr << "[camera] " << (dual_stream_ ? "dual" : "single") << "-stream "
              << width << "x" << height << " @ " << fps << " fps";
    if (dual_stream_)
        std::cerr << " | still " << still_w_ << "x" << still_h_;
    std::cerr << "\n";

    ControlList controls(cam_->controls());
    const int64_t frame_usec = 1000000 / fps;
    const int64_t dur_limits[2] = {frame_usec, frame_usec};
    controls.set(controls::FrameDurationLimits,
                 Span<const int64_t, 2>(dur_limits));
    controls.set(controls::AeEnable, true);
    controls.set(controls::AfMode, controls::AfModeContinuous);

    {
        std::lock_guard<std::mutex> lk(status_mutex_);
        status_.ae_enabled    = true;
        status_.af_enabled    = true;
        status_.lens_position = std::numeric_limits<float>::quiet_NaN();
    }

    for (auto& buf : allocator_->buffers(vf_stream_)) {
        auto req = cam_->createRequest();
        if (!req) return false;
        if (req->addBuffer(vf_stream_, buf.get()) != 0) return false;
        req->controls().merge(controls);
        requests_.push_back(std::move(req));
    }

    cam_->requestCompleted.connect(this, &Camera::request_complete);

    if (cam_->start() != 0) return false;
    for (auto& req : requests_)
        cam_->queueRequest(req.get());

    running_ = true;
    return true;
}

void Camera::stop() {
    if (!running_) return;
    running_ = false;
    cam_->stop();
    cam_->requestCompleted.disconnect(this, &Camera::request_complete);
    requests_.clear();
    still_pending_req_.reset();
    if (allocator_) { delete allocator_; allocator_ = nullptr; }
    if (still_alloc_) { delete still_alloc_; still_alloc_ = nullptr; }
    vf_stream_ = still_stream_ = nullptr;
    dual_stream_ = false;
}

void Camera::request_complete(Request* req) {
    // In dual-stream mode, still-only requests arrive on this signal too.
    // Detect them by the presence of the still stream buffer, route them to
    // the still capture waiter, and do NOT push them into the viewfinder queue.
    if (dual_stream_ && still_stream_ && req->findBuffer(still_stream_)) {
        std::lock_guard<std::mutex> lk(still_mutex_);
        still_req_ = req;
        still_cv_.notify_one();
        return;
    }

    if (req->status() == Request::RequestCancelled) return;

    const auto& meta = req->metadata();
    {
        std::lock_guard<std::mutex> lk(status_mutex_);
        if (auto v = meta.get(controls::ExposureTime))
            status_.exposure_time = *v;
        if (auto v = meta.get(controls::AnalogueGain))
            status_.iso = (int)(*v * 100);
        if (auto v = meta.get(controls::LensPosition))
            status_.lens_position = *v;
        if (auto v = meta.get(controls::AeEnable))
            status_.ae_enabled = *v;
    }

    std::lock_guard<std::mutex> lk(frame_mutex_);
    ready_frames_.push(req);
}

bool Camera::get_frame(CameraFrame& out) {
    using namespace std::chrono_literals;
    auto deadline = std::chrono::steady_clock::now() + 100ms;

    libcamera::Request* req = nullptr;
    while (true) {
        {
            std::lock_guard<std::mutex> lk(frame_mutex_);
            if (!ready_frames_.empty()) {
                req = ready_frames_.front();
                ready_frames_.pop();
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }

    auto* stream = cfg_->at(0).stream();
    auto* buf    = req->findBuffer(stream);
    const auto& planes = buf->planes();

    auto& scfg = cfg_->at(0);
    int stride = scfg.stride;

    // Map the whole DMA-BUF at offset 0.  Per-plane offsets on the Pi are not
    // page-aligned (e.g. Y ends at 1920*1080 = 2 073 600 bytes which is not a
    // multiple of 4096), so mmapping each plane at its own offset fails with
    // MAP_FAILED.  One mmap of the full buffer avoids that.
    size_t total = planes.back().offset + planes.back().length;
    void* p = ::mmap(nullptr, total, PROT_READ, MAP_SHARED,
                     planes[0].fd.get(), 0);
    if (p == MAP_FAILED) return false;

    const auto* base = static_cast<const uint8_t*>(p);
    out.width    = width_;
    out.height   = height_;
    out.y_stride  = stride;
    out.uv_stride = stride / 2;

    if (planes.size() == 1) {
        // Single-plane: Y then U then V packed contiguously.
        size_t y_size = (size_t)stride * height_;
        out.y = base;
        out.u = base + y_size;
        out.v = base + y_size + (size_t)(stride / 2) * (height_ / 2);
    } else {
        // Multi-plane sharing one fd: use the stored plane offsets.
        out.y = base + planes[0].offset;
        out.u = base + planes[1].offset;
        out.v = base + planes[2].offset;
    }

    out.release = [this, req, p, total]() {
        ::munmap(p, total);
        req->reuse(Request::ReuseBuffers);
        enqueue_request(req);
    };
    return true;
}

void Camera::enqueue_request(Request* req) {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        if (pending_controls_ && !pending_controls_->empty()) {
            req->controls().merge(*pending_controls_);
            pending_controls_->clear();
        }
    }
    cam_->queueRequest(req);
}

// Helper called by set_*() methods to lazily init and update pending controls.
static void queue_control(std::mutex& mtx,
                          std::unique_ptr<libcamera::ControlList>& list,
                          const libcamera::ControlInfoMap& info_map,
                          std::function<void(libcamera::ControlList&)> fn) {
    std::lock_guard<std::mutex> lk(mtx);
    if (!list) list = std::make_unique<libcamera::ControlList>(info_map);
    fn(*list);
}

std::vector<CameraMode> Camera::get_modes() const {
    std::vector<CameraMode> modes;
    auto qcfg = cam_->generateConfiguration({StreamRole::Viewfinder});
    if (!qcfg) return modes;

    const auto& fmts = qcfg->at(0).formats();
    for (const auto& fmt : fmts.pixelformats()) {
        if (fmt != formats::YUV420) continue;
        for (const auto& sz : fmts.sizes(fmt))
            modes.push_back({(int)sz.width, (int)sz.height});
    }

    // Largest resolution first.
    std::sort(modes.begin(), modes.end(), [](const CameraMode& a, const CameraMode& b) {
        return (a.width * a.height) > (b.width * b.height);
    });
    return modes;
}

bool Camera::restart_with_mode(const CameraMode& mode) {
    stop();
    return start(mode.width, mode.height, fps_);
}

void Camera::still_complete(Request* req) {
    std::lock_guard<std::mutex> lk(still_mutex_);
    still_req_ = req;
    still_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// Static helpers used only by capture_still
// ---------------------------------------------------------------------------

static bool still_save_jpeg(const std::string& jpeg_path,
                            const libcamera::FrameBuffer* buf,
                            int w, int h, int stride) {

    const auto& planes = buf->planes();
    size_t total = planes.back().offset + planes.back().length;
    void* p = ::mmap(nullptr, total, PROT_READ, MAP_SHARED, planes[0].fd.get(), 0);
    if (p == MAP_FAILED) return false;

    const auto* base = static_cast<const uint8_t*>(p);
    const uint8_t *y_ptr, *u_ptr, *v_ptr;
    if (planes.size() == 1) {
        size_t y_size = (size_t)stride * h;
        y_ptr = base;
        u_ptr = base + y_size;
        v_ptr = base + y_size + (size_t)(stride / 2) * (h / 2);
    } else {
        y_ptr = base + planes[0].offset;
        u_ptr = base + planes[1].offset;
        v_ptr = base + planes[2].offset;
    }

    std::string tmp = jpeg_path + ".yuv";
    bool ok = false;
    {
        std::ofstream f(tmp, std::ios::binary);
        int uv_stride = stride / 2, uv_h = h / 2;
        for (int r = 0; r < h; ++r)
            f.write(reinterpret_cast<const char*>(y_ptr + r * stride), w);
        for (int r = 0; r < uv_h; ++r)
            f.write(reinterpret_cast<const char*>(u_ptr + r * uv_stride), w / 2);
        for (int r = 0; r < uv_h; ++r)
            f.write(reinterpret_cast<const char*>(v_ptr + r * uv_stride), w / 2);
        ok = f.good();
    }
    ::munmap(p, total);

    if (ok) {
        std::string cmd = "ffmpeg -y -f rawvideo -pix_fmt yuv420p -s " +
                          std::to_string(w) + "x" + std::to_string(h) +
                          " -i " + tmp + " " + jpeg_path + " 2>/dev/null";
        ok = (std::system(cmd.c_str()) == 0);
        std::remove(tmp.c_str());
    }
    return ok;
}

static bool still_save_raw(const std::string& raw_path,
                           const libcamera::FrameBuffer* buf,
                           const libcamera::StreamConfiguration& rcfg) {
    const auto& planes = buf->planes();
    size_t total = planes.back().offset + planes.back().length;
    void* p = ::mmap(nullptr, total, PROT_READ, MAP_SHARED, planes[0].fd.get(), 0);
    if (p == MAP_FAILED) return false;

    const auto* base = static_cast<const uint8_t*>(p);
    bool ok = false;
    {
        std::ofstream f(raw_path, std::ios::binary);
        for (const auto& pl : planes)
            f.write(reinterpret_cast<const char*>(base + pl.offset), pl.length);
        ok = f.good();
    }
    if (ok) {
        // Sidecar with format metadata so the raw bytes can be decoded later
        // (e.g. dcraw -D -4 -j -t 0, rawtherapee, darktable, or numpy).
        std::ofstream mf(raw_path + ".meta");
        mf << "width="  << rcfg.size.width  << "\n"
           << "height=" << rcfg.size.height << "\n"
           << "format=" << rcfg.pixelFormat.toString() << "\n"
           << "stride=" << rcfg.stride      << "\n";
    }
    ::munmap(p, total);
    return ok;
}

bool Camera::capture_still(const std::string& jpeg_path, StillFormat fmt) {
    bool want_jpeg = (fmt == StillFormat::JPEG || fmt == StillFormat::JPEG_RAW);
    bool want_raw  = (fmt == StillFormat::RAW  || fmt == StillFormat::JPEG_RAW);

    // ---------------------------------------------------------------------------
    // Dual-stream fast path: queue a still-only request alongside the running
    // viewfinder — no stop/start, no viewfinder interruption.
    // Raw Bayer is not available in this path (the still stream is YUV);
    // fall through to the reconfigure path for raw-only captures.
    // ---------------------------------------------------------------------------
    if (dual_stream_ && still_stream_ && want_jpeg) {
        still_req_ = nullptr;
        auto req = cam_->createRequest();
        if (!req) return false;

        auto& stills = still_alloc_->buffers(still_stream_);
        if (req->addBuffer(still_stream_, stills[0].get()) != 0) return false;

        // Queue the still request alongside ongoing viewfinder requests.
        still_pending_req_ = std::move(req);
        cam_->queueRequest(still_pending_req_.get());

        {
            std::unique_lock<std::mutex> lk(still_mutex_);
            still_cv_.wait_for(lk, std::chrono::seconds(5),
                               [this]{ return still_req_ != nullptr; });
        }

        bool ok = false;
        if (still_req_ && still_req_->status() == Request::RequestComplete) {
            ok = still_save_jpeg(jpeg_path,
                                 still_req_->findBuffer(still_stream_),
                                 still_w_, still_h_, still_stride_);
        }
        still_pending_req_.reset();
        return ok;
    }

    // ---------------------------------------------------------------------------
    // Single-stream fallback: stop viewfinder, reconfigure, capture, restart.
    // Used when dual-stream is unavailable or for raw-only captures.
    // ---------------------------------------------------------------------------

    // Save viewfinder dimensions before stopping.
    int saved_w = width_, saved_h = height_, saved_fps = fps_;
    stop();

    // Build a configuration with the requested roles.
    // StillCapture (index 0) gives full sensor resolution; Raw (index 0 or 1)
    // gives native Bayer data.
    std::vector<StreamRole> roles;
    if (want_jpeg) roles.push_back(StreamRole::StillCapture);
    if (want_raw)  roles.push_back(StreamRole::Raw);

    auto scfg = cam_->generateConfiguration(roles);
    if (!scfg) { start(saved_w, saved_h, saved_fps); return false; }

    if (want_jpeg) {
        auto& cfg0 = scfg->at(0);
        cfg0.pixelFormat = formats::YUV420;
        cfg0.bufferCount = 1;
        // Leave size at the camera's default (maximum sensor resolution).
    }
    // Raw stream: keep whatever format the camera proposes; one buffer is enough.
    if (want_raw)
        scfg->at(want_jpeg ? 1 : 0).bufferCount = 1;

    if (scfg->validate() == CameraConfiguration::Invalid ||
        cam_->configure(scfg.get()) != 0) {
        start(saved_w, saved_h, saved_fps);
        return false;
    }

    // Allocate one buffer per stream.
    FrameBufferAllocator* alloc = new FrameBufferAllocator(cam_);
    for (unsigned i = 0; i < scfg->size(); ++i)
        alloc->allocate(scfg->at(i).stream());

    // Build one request covering all streams.
    auto req = cam_->createRequest();
    for (unsigned i = 0; i < scfg->size(); ++i) {
        auto* s = scfg->at(i).stream();
        req->addBuffer(s, alloc->buffers(s)[0].get());
    }

    // Arm the one-shot completion handler.
    still_req_ = nullptr;
    cam_->requestCompleted.connect(this, &Camera::still_complete);
    if (cam_->start() != 0) {
        cam_->requestCompleted.disconnect(this, &Camera::still_complete);
        delete alloc;
        start(saved_w, saved_h, saved_fps);
        return false;
    }
    cam_->queueRequest(req.get());

    // Wait up to 5 seconds for the frame.
    {
        std::unique_lock<std::mutex> lk(still_mutex_);
        still_cv_.wait_for(lk, std::chrono::seconds(5),
                           [this]{ return still_req_ != nullptr; });
    }

    cam_->stop();
    cam_->requestCompleted.disconnect(this, &Camera::still_complete);

    bool ok = false;
    if (still_req_ && still_req_->status() == Request::RequestComplete) {
        if (want_jpeg) {
            ok = still_save_jpeg(jpeg_path,
                                 still_req_->findBuffer(scfg->at(0).stream()),
                                 (int)scfg->at(0).size.width,
                                 (int)scfg->at(0).size.height,
                                 (int)scfg->at(0).stride);
        }
        if (want_raw) {
            int ri = want_jpeg ? 1 : 0;
            std::string raw_path =
                jpeg_path.substr(0, jpeg_path.rfind('.')) + ".raw";
            bool raw_ok = still_save_raw(raw_path,
                                         still_req_->findBuffer(scfg->at(ri).stream()),
                                         scfg->at(ri));
            if (!want_jpeg) ok = raw_ok;
        }
    }

    delete alloc;
    // Restart the viewfinder at the original resolution.
    start(saved_w, saved_h, saved_fps);
    return ok;
}

void Camera::set_ae_enable(bool enable) {
    queue_control(pending_mutex_, pending_controls_, cam_->controls(),
                  [enable](ControlList& c){ c.set(controls::AeEnable, enable); });
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.ae_enabled = enable;
}

void Camera::set_af_enable(bool enable) {
    auto mode = enable ? controls::AfModeContinuous : controls::AfModeManual;
    queue_control(pending_mutex_, pending_controls_, cam_->controls(),
                  [mode](ControlList& c){ c.set(controls::AfMode, mode); });
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.af_enabled = enable;
    if (enable)
        status_.lens_position = std::numeric_limits<float>::quiet_NaN();
}

void Camera::set_lens_position(float pos) {
    pos = std::clamp(pos, 0.0f, 1.0f);
    queue_control(pending_mutex_, pending_controls_, cam_->controls(),
                  [pos](ControlList& c){
                      c.set(controls::AfMode, controls::AfModeManual);
                      c.set(controls::LensPosition, pos);
                  });
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.af_enabled    = false;
    status_.lens_position = pos;
}

void Camera::set_aperture(float fstop) {
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.aperture = fstop;
}

void Camera::set_shutter_speed(float us) {
    int32_t val = (int32_t)us;
    queue_control(pending_mutex_, pending_controls_, cam_->controls(),
                  [val](ControlList& c){ c.set(controls::ExposureTime, val); });
}

void Camera::set_iso(int iso) {
    float gain = iso / 100.0f;
    queue_control(pending_mutex_, pending_controls_, cam_->controls(),
                  [gain](ControlList& c){ c.set(controls::AnalogueGain, gain); });
}

CameraStatus Camera::get_status() const {
    std::lock_guard<std::mutex> lk(status_mutex_);
    return status_;
}

std::string Camera::model_name() const {
    // libcamera exposes the sensor model via the Model property.
    namespace props = libcamera::properties;
    const auto& p = cam_->properties();
    if (auto v = p.get(props::Model))
        return std::string(*v);
    return {};
}

ControlRange Camera::shutter_range() const {
    const auto& info = cam_->controls();
    auto it = info.find(&controls::ExposureTime);
    if (it == info.end()) return {};
    return {
        (float)it->second.min().get<int32_t>(),
        (float)it->second.max().get<int32_t>(),
        true
    };
}

ControlRange Camera::gain_range() const {
    const auto& info = cam_->controls();
    auto it = info.find(&controls::AnalogueGain);
    if (it == info.end()) return {};
    return {
        it->second.min().get<float>(),
        it->second.max().get<float>(),
        true
    };
}

ControlRange Camera::aperture_range() const {
    // libcamera has no standard aperture control; Pi cameras have fixed aperture.
    // This returns available=false until a future camera or libcamera version
    // exposes aperture as a settable control.
    return {};
}
