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
#include <cmath>

using namespace libcamera;

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
    controls.set(controls::FrameDurationLimits,
                 Span<const int64_t, 2>({1000000 / fps, 1000000 / fps}));
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

    // mmap the buffer planes.
    struct PlaneMap {
        void* ptr;
        size_t len;
    };
    // Store mapped ptrs; release lambda will unmap + re-queue.
    std::vector<PlaneMap> maps;
    for (auto& pl : planes) {
        void* p = ::mmap(nullptr, pl.length, PROT_READ, MAP_SHARED, pl.fd.get(), pl.offset);
        maps.push_back({p, pl.length});
    }

    auto& scfg = cfg_->at(0);
    int stride = scfg.stride;

    out.width     = width_;
    out.height    = height_;
    out.y_stride  = stride;
    out.uv_stride = stride / 2;
    out.y = static_cast<const uint8_t*>(maps[0].ptr);
    out.u = static_cast<const uint8_t*>(maps[1].ptr);
    out.v = static_cast<const uint8_t*>(maps[2].ptr);

    out.release = [this, req, maps]() mutable {
        for (auto& m : maps) ::munmap(m.ptr, m.len);
        req->reuse(Request::ReuseBuffers);
        enqueue_request(req);
    };
    return true;
}

void Camera::enqueue_request(Request* req) {
    if (running_) cam_->queueRequest(req);
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
    ControlList ctrls(cam_->controls());
    ctrls.set(controls::AeEnable, enable);
    cam_->setControls(ctrls);
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.ae_enabled = enable;
}

void Camera::set_af_enable(bool enable) {
    ControlList ctrls(cam_->controls());
    ctrls.set(controls::AfMode,
              enable ? controls::AfModeContinuous : controls::AfModeManual);
    cam_->setControls(ctrls);
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.af_enabled = enable;
    if (enable)
        status_.lens_position = std::numeric_limits<float>::quiet_NaN();
}

void Camera::set_lens_position(float pos) {
    pos = std::clamp(pos, 0.0f, 1.0f);
    ControlList ctrls(cam_->controls());
    ctrls.set(controls::AfMode, controls::AfModeManual);
    ctrls.set(controls::LensPosition, pos);
    cam_->setControls(ctrls);
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.af_enabled    = false;
    status_.lens_position = pos;
}

void Camera::set_aperture(float fstop) {
    // ApertureValue is only present on lenses that expose it (not Pi cameras).
    // ControlInfoMap::count() takes a const ControlId* per the libcamera API.
    if (cam_->controls().count(&controls::ApertureValue)) {
        ControlList ctrls(cam_->controls());
        ctrls.set(controls::ApertureValue, fstop);
        cam_->setControls(ctrls);
    }
    // Always update the OSD value even if the lens is fixed-aperture,
    // so manual aperture notation still shows in the display.
    std::lock_guard<std::mutex> lk(status_mutex_);
    status_.aperture = fstop;
}

CameraStatus Camera::get_status() const {
    std::lock_guard<std::mutex> lk(status_mutex_);
    return status_;
}
