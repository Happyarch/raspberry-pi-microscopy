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
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string user_config_path() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/microscopi.conf" : "";
}

static std::string find_config() {
    // If the user config exists, use it.
    std::string user = user_config_path();
    if (!user.empty() && fs::exists(user)) return user;
    // Fall back to system config.
    if (fs::exists("/etc/microscopi.conf")) return "/etc/microscopi.conf";
    return "";
}

// Write a default user config on first run, then load from it.
static Config bootstrap_config() {
    std::string path = find_config();
    if (path.empty()) {
        // No config anywhere — write one so the user has something to edit.
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

int main() {
    // ---- Config ----
    Config cfg = bootstrap_config();

    // ---- SDL2 init (needed before querying display modes) ----
    setenv("SDL_VIDEODRIVER", "kmsdrm",  1);
    setenv("SDL_AUDIODRIVER", "dummy",   1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    // ---- Resolution selection ----
    Resolution res = select_best_resolution(0, {cfg.fallback_width, cfg.fallback_height});

    // ---- Renderer ----
    Renderer renderer(res.width, res.height);

    // ---- OSD ----
    const char* home       = std::getenv("HOME");
    std::string home_str   = home ? home : "/root";
    std::string cache_dir  = ensure_dir(home_str + "/.cache/microscopi");
    std::string icons_dir  = "/usr/local/share/microscopi/icons";
    std::string font_path  = "/usr/local/share/microscopi/fonts/RobotoCondensed-Regular.ttf";

    Osd osd(renderer.sdl_renderer(), res.width, res.height,
            icons_dir, font_path, cache_dir);

    // ---- Camera ----
    Camera camera(cfg.camera_index);
    if (!camera.start(res.width, res.height, cfg.fps)) {
        std::cerr << "Failed to start camera\n";
        return 1;
    }

    // ---- Encoder ----
    Encoder encoder(cfg.video_backend,
                    res.width, res.height, cfg.fps,
                    cfg.builtin_bitrate,
                    cfg.ffmpeg_command);

    // ---- Application state ----
    OsdState  osd_state{};
    bool      recording      = false;
    uint64_t  record_start   = 0;
    int       still_count    = 0;
    bool      ae_enabled     = cfg.initial_ae_enabled;
    bool      af_enabled     = cfg.initial_af_enabled;
    float     aperture       = cfg.initial_aperture;
    float     lens_position  = 0.5f;
    bool      show_crosshair = cfg.show_crosshair;
    bool      should_quit    = false;

    // Apply config-specified initial camera state.
    camera.set_ae_enable(ae_enabled);
    camera.set_af_enable(af_enabled);
    if (aperture > 0.0f) camera.set_aperture(aperture);

    // ---- Input callbacks ----
    InputCallbacks cbs;

    cbs.on_quit = [&]{ should_quit = true; };

    cbs.on_aperture_up   = [&]{ aperture += 1.0f; camera.set_aperture(aperture); };
    cbs.on_aperture_down = [&]{ aperture = std::max(0.0f, aperture - 1.0f);
                                camera.set_aperture(aperture); };

    cbs.on_focus_up   = [&]{
        lens_position = std::min(1.0f, lens_position + 0.05f);
        camera.set_lens_position(lens_position);
        af_enabled = false;
    };
    cbs.on_focus_down = [&]{
        lens_position = std::max(0.0f, lens_position - 0.05f);
        camera.set_lens_position(lens_position);
        af_enabled = false;
    };

    cbs.on_toggle_ae = [&]{
        ae_enabled = !ae_enabled;
        camera.set_ae_enable(ae_enabled);
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

    InputHandler input(std::move(cbs));

    // ---- Main loop ----
    while (!should_quit) {
        // Process events first so input is responsive even if camera stalls.
        if (!input.process_events()) break;

        CameraFrame frame;
        if (!camera.get_frame(frame)) continue;

        // Feed encoder if recording.
        if (recording)
            encoder.submit_frame(frame.y, frame.u, frame.v,
                                 frame.y_stride, frame.uv_stride);

        // Sync OSD state from camera metadata.
        CameraStatus status        = camera.get_status();
        osd_state.aperture         = status.aperture;
        osd_state.exposure_us      = status.exposure_time;
        osd_state.lens_position    = status.lens_position;
        osd_state.ae_enabled       = status.ae_enabled;
        osd_state.af_enabled       = status.af_enabled;
        osd_state.still_count      = still_count;
        osd_state.recording        = recording;
        osd_state.record_start_ms  = record_start;
        osd_state.record_hold_progress = input.record_hold_progress();
        osd_state.show_crosshair       = show_crosshair;

        renderer.present_frame(frame.y, frame.u, frame.v,
                               frame.y_stride, frame.uv_stride,
                               &osd, osd_state);
        frame.release();
    }

    if (recording) encoder.close();
    camera.stop();
    return 0;
}
