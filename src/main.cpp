#include "camera/camera.h"
#include "camera/encoder.h"
#include "config/config.h"
#include "ui/input.h"
#include "ui/osd.h"
#include "ui/renderer.h"
#include "util/exif_writer.h"
#include "util/exposure.h"
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
    renderer.set_crop(cfg.crop_top, cfg.crop_bottom, cfg.crop_left, cfg.crop_right);
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

    // ---- Camera mode list (built once after camera starts) ----
    std::vector<CameraMode>  cam_modes     = camera.get_modes();
    std::vector<std::string> cam_mode_labels;
    for (const auto& m : cam_modes) cam_mode_labels.push_back(m.label());

    auto find_active_mode = [&]() -> int {
        CameraMode cur = camera.current_mode();
        for (int i = 0; i < (int)cam_modes.size(); ++i)
            if (cam_modes[i] == cur) return i;
        return 0;
    };
    int  cam_mode_active   = find_active_mode();
    int  cam_mode_selected = cam_mode_active;
    bool cam_mode_open     = false;

    // ---- Control ladders — built from camera-reported ranges ----
    // Falls back to the full master table if the camera doesn't expose that control.
    auto make_shutter_ladder = [&]() -> std::vector<float> {
        auto r = camera.shutter_range();
        return r.available
            ? build_shutter_ladder(r.min, r.max)
            : std::vector<float>(kShutterMaster.begin(), kShutterMaster.end());
    };
    auto make_iso_ladder = [&]() -> std::vector<int> {
        auto r = camera.gain_range();
        return r.available
            ? build_iso_ladder(r.min, r.max)
            : std::vector<int>(kIsoMaster.begin(), kIsoMaster.end());
    };
    auto make_aperture_ladder = [&]() -> std::vector<float> {
        auto r = camera.aperture_range();
        return r.available
            ? build_aperture_ladder(r.min, r.max)
            : std::vector<float>{}; // empty = fixed-aperture camera
    };
    std::vector<float> shutter_ladder  = make_shutter_ladder();
    std::vector<int>   iso_ladder      = make_iso_ladder();
    std::vector<float> aperture_ladder = make_aperture_ladder();

    // ---- Application state ----
    OsdState   osd_state{};
    bool       recording      = false;
    uint64_t   record_start   = 0;
    int        still_count    = 0;
    bool       af_enabled     = cfg.initial_af_enabled;
    bool       show_crosshair = cfg.show_crosshair;
    bool       should_quit    = false;

    // Exposure state
    ExposureMode mode      = ExposureMode::P;
    int          mode_idx  = 0;
    float        shutter_us   = 16667.0f; // 1/60 s starting point for S/M
    int          shutter_step = shutter_index(shutter_us, shutter_ladder);
    int          iso          = 0;        // 0 = AUTO
    int          iso_step     = 0;
    float        aperture_fs  = cfg.initial_aperture; // f-stop; 0 = unknown/not set
    int          aperture_step = aperture_ladder.empty() ? 0
                               : aperture_index(aperture_fs, aperture_ladder);

    if (aperture_fs > 0.0f) camera.set_aperture(aperture_fs);

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
            shutter_step = std::min((int)shutter_ladder.size() - 1, shutter_step + 1);
            shutter_us   = shutter_ladder[shutter_step];
            camera.set_shutter_speed(shutter_us);
        }
    };
    cbs.on_shutter_down = [&]{
        if (mode == ExposureMode::S || mode == ExposureMode::M) {
            shutter_step = std::max(0, shutter_step - 1);
            shutter_us   = shutter_ladder[shutter_step];
            camera.set_shutter_speed(shutter_us);
        }
    };

    cbs.on_aperture_up = [&]{
        if (!aperture_ladder.empty()) {
            aperture_step = std::min((int)aperture_ladder.size() - 1, aperture_step + 1);
            aperture_fs   = aperture_ladder[aperture_step];
            camera.set_aperture(aperture_fs);
        }
    };
    cbs.on_aperture_down = [&]{
        if (!aperture_ladder.empty()) {
            aperture_step = std::max(0, aperture_step - 1);
            aperture_fs   = aperture_ladder[aperture_step];
            camera.set_aperture(aperture_fs);
        }
    };

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
            CameraStatus st = camera.get_status();
            iso_step = (st.iso > 0) ? iso_index(st.iso, iso_ladder) : 0;
        } else {
            iso_step = std::min((int)iso_ladder.size() - 1, iso_step + 1);
        }
        iso = iso_ladder[iso_step];
        camera.set_iso(iso);
    };
    cbs.on_iso_down = [&]{
        if (iso == 0) {
            CameraStatus st = camera.get_status();
            iso_step = (st.iso > 0) ? iso_index(st.iso, iso_ladder) : 0;
        } else {
            iso_step = std::max(0, iso_step - 1);
        }
        iso = iso_ladder[iso_step];
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
        if (camera.capture_still(path)) {
            ++still_count;
            // Inject EXIF with current shooting parameters.
            CameraStatus st = camera.get_status();
            ExifParams exif;
            exif.exposure_us   = st.exposure_time;
            exif.fstop         = (st.aperture > 0) ? st.aperture : aperture_fs;
            exif.iso           = (iso != 0) ? iso : st.iso;
            exif.lens_position = st.lens_position;
            exif.exposure_mode = mode_idx;
            exif.camera_model  = camera.model_name();
            time_t now = time(nullptr);
            struct tm* tm_info = localtime(&now);
            char dtbuf[20];
            strftime(dtbuf, sizeof(dtbuf), "%Y:%m:%d %H:%M:%S", tm_info);
            exif.datetime = dtbuf;
            insert_exif(path, exif);
        }
    };

    cbs.on_focus_scroll = [&](int dir) {
        CameraStatus st = camera.get_status();
        float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
        camera.set_lens_position(std::clamp(pos + dir * cfg.focus_scroll_step, 0.0f, 1.0f));
        af_enabled = false;
    };

    cbs.on_cam_mode_toggle = [&]{
        cam_mode_open     = !cam_mode_open;
        cam_mode_selected = cam_mode_active; // reset selection to current
    };
    cbs.on_cam_mode_up = [&]{
        int n = (int)cam_modes.size();
        cam_mode_selected = (cam_mode_selected - 1 + n) % n;
    };
    cbs.on_cam_mode_down = [&]{
        int n = (int)cam_modes.size();
        cam_mode_selected = (cam_mode_selected + 1) % n;
    };
    cbs.on_cam_mode_cancel = [&]{
        cam_mode_open = false;
    };
    cbs.on_cam_mode_confirm = [&]{
        cam_mode_open = false;
        if (cam_mode_selected == cam_mode_active) return; // no change
        const CameraMode& m = cam_modes[cam_mode_selected];
        if (camera.restart_with_mode(m)) {
            renderer.update_texture_size(camera.width(), camera.height());
            cam_mode_active   = find_active_mode();
            cam_mode_selected = cam_mode_active;
            // Rebuild ladders — ranges can differ per mode on some cameras.
            shutter_ladder  = make_shutter_ladder();
            iso_ladder      = make_iso_ladder();
            aperture_ladder = make_aperture_ladder();
            shutter_step  = shutter_index(shutter_us, shutter_ladder);
            iso_step      = iso == 0 ? 0 : iso_index(iso, iso_ladder);
            aperture_step = aperture_ladder.empty() ? 0
                          : aperture_index(aperture_fs, aperture_ladder);
        }
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
        input.set_mode_list_open(cam_mode_open);
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
        osd_state.mode_list            = {cam_mode_open, cam_mode_selected,
                                          cam_mode_active, &cam_mode_labels};

        renderer.present_frame(frame.y, frame.u, frame.v,
                               frame.y_stride, frame.uv_stride,
                               &osd, osd_state);
        frame.release();
    }

    if (recording) encoder.close();
    camera.stop();
    return 0;
}
