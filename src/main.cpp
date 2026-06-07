#include "camera/camera.h"
#include "camera/encoder.h"
#include "config/config.h"
#include "ui/input.h"
#include "ui/osd.h"
#include "ui/renderer.h"
#include "util/resolution.h"

#include <SDL2/SDL.h>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string user_config_path() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/microscopi.conf" : "";
}

static std::string find_config() {
    std::string user = user_config_path();
    if (!user.empty() && fs::exists(user)) return user;
    if (fs::exists("/etc/microscopi.conf")) return "/etc/microscopi.conf";
    return "";
}

static Config bootstrap_config() {
    std::string path = find_config();
    if (path.empty()) {
        std::string user = user_config_path();
        if (!user.empty()) {
            write_default_config(user);
            path = user;
        }
    }
    return load_config(path);
}

static std::string ensure_dir(const std::string& path) {
    fs::create_directories(path);
    return path;
}

static std::string timestamp_filename(const std::string& ext) {
    time_t t  = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm);
    return std::string(buf) + ext;
}

// ---------------------------------------------------------------------------
// Shutter speed ladder (microseconds) for S/M mode stepping
// ---------------------------------------------------------------------------

static const float kShutterSteps[] = {
    31.25f,    // 1/32000
    62.5f,     // 1/16000
    125.0f,    // 1/8000
    250.0f,    // 1/4000
    500.0f,    // 1/2000
    1000.0f,   // 1/1000
    2000.0f,   // 1/500
    4000.0f,   // 1/250
    8333.0f,   // 1/120
    16667.0f,  // 1/60
    33333.0f,  // 1/30
    66667.0f,  // 1/15
    125000.0f, // 1/8
    250000.0f, // 1/4
    500000.0f, // 1/2
    1000000.0f,// 1"
    2000000.0f,// 2"
};
static constexpr int kShutterStepCount = (int)(sizeof(kShutterSteps) / sizeof(kShutterSteps[0]));

// Returns index of the closest step to current_us.
static int shutter_index(float current_us) {
    int best = 0;
    float best_d = std::abs(current_us - kShutterSteps[0]);
    for (int i = 1; i < kShutterStepCount; ++i) {
        float d = std::abs(current_us - kShutterSteps[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// ISO ladder
// ---------------------------------------------------------------------------

static const int kIsoSteps[] = { 100, 200, 400, 800, 1600, 3200, 6400 };
static constexpr int kIsoStepCount = (int)(sizeof(kIsoSteps) / sizeof(kIsoSteps[0]));

static int iso_index(int current) {
    int best = 0;
    int best_d = std::abs(current - kIsoSteps[0]);
    for (int i = 1; i < kIsoStepCount; ++i) {
        int d = std::abs(current - kIsoSteps[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    Config cfg = bootstrap_config();

    setenv("SDL_VIDEODRIVER", "kmsdrm", 1);
    setenv("SDL_AUDIODRIVER", "dummy",  1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    Resolution res = select_best_resolution(0, {cfg.fallback_width, cfg.fallback_height});

    Renderer renderer(res.width, res.height);
    const int rw = renderer.width();
    const int rh = renderer.height();

    const char* home      = std::getenv("HOME");
    std::string home_str  = home ? home : "/root";
    std::string cache_dir = ensure_dir(home_str + "/.cache/microscopi");
    std::string icons_dir = "/usr/local/share/microscopi/icons";
    std::string font_path = "/usr/local/share/microscopi/fonts/RobotoCondensed-Regular.ttf";

    Osd osd(renderer.sdl_renderer(), rw, rh, icons_dir, font_path, cache_dir, cfg.keys);

    Camera camera(cfg.camera_index);
    if (!camera.start(rw, rh, cfg.fps)) {
        std::cerr << "Failed to start camera\n";
        return 1;
    }

    Encoder encoder(cfg.video_backend,
                    res.width, res.height, cfg.fps,
                    cfg.builtin_bitrate,
                    cfg.ffmpeg_command);

    // ---- Application state ----
    OsdState   osd_state{};
    bool       recording      = false;
    uint64_t   record_start   = 0;
    int        still_count    = 0;
    bool       af_enabled     = cfg.initial_af_enabled;
    bool       show_crosshair = cfg.show_crosshair;
    bool       should_quit    = false;

    // Exposure state
    ExposureMode mode    = ExposureMode::P;
    int          mode_idx = 0;          // mirrors mode as int for modular arithmetic
    float        shutter_us = 16667.0f; // 1/60 s default for S/M
    int          shutter_step = shutter_index(shutter_us);
    int          iso          = 0;       // 0 = AUTO
    int          iso_step     = 0;       // index into kIsoSteps; only used when iso != 0

    // Apply initial camera state.
    camera.set_ae_enable(true);  // start in P mode (full auto)
    camera.set_af_enable(af_enabled);

    auto apply_mode = [&](ExposureMode m) {
        mode     = m;
        mode_idx = (int)m;
        switch (m) {
        case ExposureMode::P:
            camera.set_ae_enable(true);
            break;
        case ExposureMode::A:
            // Auto shutter, user-set ISO — keep AE on for shutter.
            camera.set_ae_enable(true);
            if (iso != 0) camera.set_iso(iso);
            break;
        case ExposureMode::S:
            camera.set_ae_enable(false);
            camera.set_shutter_speed(shutter_us);
            if (iso != 0) camera.set_iso(iso);
            break;
        case ExposureMode::M:
            camera.set_ae_enable(false);
            camera.set_shutter_speed(shutter_us);
            if (iso != 0) camera.set_iso(iso);
            break;
        }
    };

    // ---- Input callbacks ----
    InputCallbacks cbs;

    cbs.on_quit = [&]{ should_quit = true; };

    cbs.on_mode_cycle_fwd  = [&]{
        mode_idx = (mode_idx + 1) % 4;
        apply_mode(static_cast<ExposureMode>(mode_idx));
    };
    cbs.on_mode_cycle_back = [&]{
        mode_idx = (mode_idx + 3) % 4;
        apply_mode(static_cast<ExposureMode>(mode_idx));
    };
    cbs.on_mode_set = [&](int idx){
        mode_idx = idx;
        apply_mode(static_cast<ExposureMode>(idx));
    };

    cbs.on_shutter_up = [&]{
        if (mode == ExposureMode::S || mode == ExposureMode::M) {
            shutter_step = std::min(kShutterStepCount - 1, shutter_step + 1);
            shutter_us   = kShutterSteps[shutter_step];
            camera.set_shutter_speed(shutter_us);
        }
    };
    cbs.on_shutter_down = [&]{
        if (mode == ExposureMode::S || mode == ExposureMode::M) {
            shutter_step = std::max(0, shutter_step - 1);
            shutter_us   = kShutterSteps[shutter_step];
            camera.set_shutter_speed(shutter_us);
        }
    };

    cbs.on_aperture_up   = [&]{ /* fixed aperture on Pi cameras — no-op */ };
    cbs.on_aperture_down = [&]{ /* fixed aperture on Pi cameras — no-op */ };

    cbs.on_focus_up = [&]{
        CameraStatus st = camera.get_status();
        float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
        camera.set_lens_position(std::min(1.0f, pos + 0.05f));
        af_enabled = false;
    };
    cbs.on_focus_down = [&]{
        CameraStatus st = camera.get_status();
        float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
        camera.set_lens_position(std::max(0.0f, pos - 0.05f));
        af_enabled = false;
    };

    cbs.on_iso_up = [&]{
        if (iso == 0) {
            // Exit AUTO: snap to closest step from current camera ISO.
            CameraStatus st = camera.get_status();
            iso_step = (st.iso > 0) ? iso_index(st.iso) : 0;
        } else {
            iso_step = std::min(kIsoStepCount - 1, iso_step + 1);
        }
        iso = kIsoSteps[iso_step];
        camera.set_iso(iso);
    };
    cbs.on_iso_down = [&]{
        if (iso == 0) {
            CameraStatus st = camera.get_status();
            iso_step = (st.iso > 0) ? iso_index(st.iso) : 0;
        } else {
            iso_step = std::max(0, iso_step - 1);
        }
        iso = kIsoSteps[iso_step];
        camera.set_iso(iso);
    };

    cbs.on_toggle_af = [&]{
        af_enabled = !af_enabled;
        camera.set_af_enable(af_enabled);
    };

    cbs.on_toggle_crosshair = [&]{ show_crosshair = !show_crosshair; };

    cbs.on_still = [&]{
        std::string dir  = ensure_dir(cfg.stills_dir);
        std::string path = dir + "/" + timestamp_filename(".jpg");
        if (camera.capture_still(path)) ++still_count;
    };

    cbs.on_record_toggle = [&]{
        if (!recording) {
            std::string dir  = ensure_dir(cfg.video_dir);
            std::string path = dir + "/" + timestamp_filename(".mkv");
            if (encoder.open(path)) {
                recording    = true;
                record_start = SDL_GetTicks64();
            }
        } else {
            encoder.close();
            recording = false;
        }
    };

    InputHandler input(std::move(cbs), cfg.keys);

    // ---- Main loop ----
    while (!should_quit) {
        if (!input.process_events()) break;

        CameraFrame frame;
        if (!camera.get_frame(frame)) continue;

        if (recording)
            encoder.submit_frame(frame.y, frame.u, frame.v,
                                 frame.y_stride, frame.uv_stride);

        CameraStatus status         = camera.get_status();
        osd_state.aperture          = status.aperture;
        osd_state.exposure_us       = status.exposure_time;
        osd_state.lens_position     = status.lens_position;
        osd_state.af_enabled        = af_enabled;
        osd_state.exposure_mode     = mode_idx;
        osd_state.iso               = iso;
        osd_state.still_count       = still_count;
        osd_state.recording         = recording;
        osd_state.record_start_ms   = record_start;
        osd_state.record_hold_progress = input.record_hold_progress();
        osd_state.quit_hold_progress   = input.quit_hold_progress();
        osd_state.show_crosshair       = show_crosshair;
        osd_state.show_help            = input.help_visible();

        renderer.present_frame(frame.y, frame.u, frame.v,
                               frame.y_stride, frame.uv_stride,
                               &osd, osd_state);
        frame.release();
    }

    if (recording) encoder.close();
    camera.stop();
    return 0;
}
