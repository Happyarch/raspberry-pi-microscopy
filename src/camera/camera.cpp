#include "camera.h"
#include <libcamera/controls.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/property_ids.h>
#include <sys/mman.h>
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

    cfg_ = cam_->generateConfiguration({StreamRole::Viewfinder});
    if (!cfg_) return false;

    auto& stream_cfg = cfg_->at(0);
    stream_cfg.pixelFormat = formats::YUV420;
    stream_cfg.size        = {(unsigned)width, (unsigned)height};
    stream_cfg.bufferCount = 4;

    if (cfg_->validate() == CameraConfiguration::Invalid) return false;
    if (cam_->configure(cfg_.get()) != 0) return false;

    allocator_ = new FrameBufferAllocator(cam_);
    auto* stream = cfg_->at(0).stream();
    if (allocator_->allocate(stream) < 0) return false;

    ControlList controls(cam_->controls());
    const int64_t frame_usec = 1000000 / fps;
    const int64_t dur_limits[2] = {frame_usec, frame_usec};
    controls.set(controls::FrameDurationLimits,
                 Span<const int64_t, 2>(dur_limits));
    controls.set(controls::AeEnable, true);
    controls.set(controls::AfMode, controls::AfModeContinuous);

    {
        std::lock_guard<std::mutex> lk(status_mutex_);
        status_.ae_enabled   = true;
        status_.af_enabled   = true;
        status_.lens_position = std::numeric_limits<float>::quiet_NaN();
    }

    for (auto& buf : allocator_->buffers(stream)) {
        auto req = cam_->createRequest();
        if (!req) return false;
        if (req->addBuffer(stream, buf.get()) != 0) return false;
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
    if (allocator_) {
        delete allocator_;
        allocator_ = nullptr;
    }
}

void Camera::request_complete(Request* req) {
    if (req->status() == Request::RequestCancelled) return;
    // Update metadata from completed request.
    const auto& meta = req->metadata();
    {
        std::lock_guard<std::mutex> lk(status_mutex_);
        if (auto v = meta.get(controls::ExposureTime))
            status_.exposure_time = *v;
        if (auto v = meta.get(controls::AnalogueGain))
            status_.iso = (int)(*v * 100);  // approximate ISO
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

bool Camera::capture_still(const std::string& path) {
    // Take a high-resolution still by briefly switching to StillCapture role.
    // For simplicity we save the current viewfinder frame as JPEG using libjpeg.
    // A full implementation would reconfigure to StillCapture; this captures
    // the live frame, which is sufficient for microscopy.
    CameraFrame frame;
    if (!get_frame(frame)) return false;

    // Write raw YUV420 to a temp file then call ffmpeg to convert to JPEG.
    std::string tmp = path + ".yuv";
    {
        std::ofstream f(tmp, std::ios::binary);
        int uv_h = height_ / 2;
        for (int r = 0; r < height_; ++r)
            f.write(reinterpret_cast<const char*>(frame.y + r * frame.y_stride), width_);
        for (int r = 0; r < uv_h; ++r)
            f.write(reinterpret_cast<const char*>(frame.u + r * frame.uv_stride), width_ / 2);
        for (int r = 0; r < uv_h; ++r)
            f.write(reinterpret_cast<const char*>(frame.v + r * frame.uv_stride), width_ / 2);
    }
    frame.release();

    std::string cmd = "ffmpeg -y -f rawvideo -pix_fmt yuv420p -s " +
                      std::to_string(width_) + "x" + std::to_string(height_) +
                      " -i " + tmp + " " + path + " 2>/dev/null";
    int ret = std::system(cmd.c_str());
    std::remove(tmp.c_str());
    return ret == 0;
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
