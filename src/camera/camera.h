#pragma once
#include <libcamera/libcamera.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <functional>

enum class ExposureMode { P = 0, A = 1, S = 2, M = 3 };

struct CameraMode {
    int         width;
    int         height;

    std::string label() const {
        return std::to_string(width) + "x" + std::to_string(height);
    }
    bool operator==(const CameraMode& o) const {
        return width == o.width && height == o.height;
    }
};

// Range of a camera control value, as reported by libcamera.
// `available` is false when the camera does not expose that control.
struct ControlRange {
    float min{0};
    float max{0};
    bool  available{false};
};

struct CameraStatus {
    float aperture;        // f-number (e.g. 2.8); 0 if fixed/unknown
    float exposure_time;   // microseconds
    float lens_position;   // 0.0 (infinity) – 1.0 (macro); NaN if AF active
    bool  ae_enabled;
    bool  af_enabled;
    int   iso;
};

// Frame data delivered by the camera to the render loop.
struct CameraFrame {
    const uint8_t* y;
    const uint8_t* u;
    const uint8_t* v;
    int            width;
    int            height;
    int            y_stride;
    int            uv_stride;
    // Caller must call release() when done with this frame.
    std::function<void()> release;
};

class Camera {
public:
    explicit Camera(int index);
    ~Camera();

    bool start(int width, int height, int fps);
    void stop();

    // Returns the most recently completed viewfinder frame.
    // Blocks briefly (up to ~33ms) if no new frame is ready.
    // Returns false if the camera was stopped.
    bool get_frame(CameraFrame& out);

    bool capture_still(const std::string& path);

    // Returns all YUV420 resolutions the camera supports, largest first.
    // Safe to call while the camera is running.
    std::vector<CameraMode> get_modes() const;

    // Stop and restart the camera with a different resolution.
    // Returns false if reconfiguration fails (camera remains stopped on failure).
    bool restart_with_mode(const CameraMode& mode);

    void set_ae_enable(bool enable);
    void set_af_enable(bool enable);
    void set_lens_position(float pos);   // 0.0 = infinity, 1.0 = macro
    void set_aperture(float fstop);      // cosmetic on fixed-aperture lenses
    void set_shutter_speed(float us);    // sets ExposureTime; caller must disable AE first
    void set_iso(int iso);               // sets AnalogueGain = iso/100; caller manages AE

    CameraStatus get_status() const;
    std::string  model_name() const; // camera model string from libcamera properties

    // Query the live control range reported by the camera after start().
    // Returns available=false if the camera does not expose that control.
    ControlRange shutter_range()  const; // ExposureTime, µs
    ControlRange gain_range()     const; // AnalogueGain (ISO ≈ gain × 100)
    ControlRange aperture_range() const; // f-stop; most Pi cameras: available=false

    int width()  const { return width_; }
    int height() const { return height_; }
    int fps()    const { return fps_; }

    CameraMode current_mode() const { return {width_, height_}; }

private:
    void request_complete(libcamera::Request* req);
    void enqueue_request(libcamera::Request* req);

    int index_;
    int width_{0}, height_{0}, fps_{0};

    std::shared_ptr<libcamera::CameraManager>    mgr_;
    std::shared_ptr<libcamera::Camera>            cam_;
    std::unique_ptr<libcamera::CameraConfiguration> cfg_;
    libcamera::FrameBufferAllocator*              allocator_{nullptr};
    std::vector<std::unique_ptr<libcamera::Request>> requests_;

    mutable std::mutex              frame_mutex_;
    std::queue<libcamera::Request*> ready_frames_;

    mutable std::mutex        status_mutex_;
    CameraStatus              status_{};

    // Controls queued by set_*() calls, merged into the next outgoing request.
    // Initialised lazily from cam_->controls() on first use.
    std::mutex                          pending_mutex_;
    std::unique_ptr<libcamera::ControlList> pending_controls_;

    bool running_{false};
};
