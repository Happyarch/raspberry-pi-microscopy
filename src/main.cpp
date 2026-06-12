#include "camera/camera.h"
#include "camera/encoder.h"
#include "config/config.h"
#include "ui/gallery.h"
#include "ui/input.h"
#include "ui/osd.h"
#include "ui/renderer.h"
#include "util/exif_writer.h"
#include "util/exposure.h"
#include "util/media_db.h"
#include "util/mjpeg_server.h"
#include "util/resolution.h"
#include "util/socket_server.h"
#include "util/timelapse.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static std::atomic<bool> g_reload_config{false};
static std::atomic<bool> g_quit{false};

static void handle_sighup(int)  { g_reload_config.store(true,  std::memory_order_relaxed); }
static void handle_sigterm(int) { g_quit.store(true, std::memory_order_relaxed); }

// Monotonic millisecond timestamp — shared clock for recording timer, replaces SDL_GetTicks64
static uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());

}

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

static std::string resolve_config_path() {
    std::string path = find_config();
    if (path.empty()) {
        std::string user = user_config_path();
        if (!user.empty()) {
            write_default_config(user);
            path = user;
        }
    }
    return path;
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

static std::string iso8601_now() {
    time_t t  = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    return buf;
}

// Minimal JSON array serialisation for MediaItem / TimelapseSession vectors.
static std::string json_str(const std::string& s) {
    // Escape backslash and double-quote; all other chars are safe in our filenames.
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '"') { out += "\\\""; }
        else { out += c; }
    }
    out += '"';
    return out;
}

static std::string serialize_json(const std::vector<MediaItem>& items) {
    std::string j = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& m = items[i];
        if (i) j += ",";
        j += "{\"id\":" + std::to_string(m.id)
           + ",\"type\":" + json_str(m.type)
           + ",\"path\":" + json_str(m.path)
           + ",\"filename\":" + json_str(m.filename)
           + ",\"captured_at\":" + json_str(m.captured_at)
           + ",\"size_bytes\":" + std::to_string(m.size_bytes)
           + ",\"timelapse_id\":" + std::to_string(m.timelapse_id)
           + ",\"blurhash\":" + json_str(m.blurhash)
           + "}";
    }
    j += "]";
    return j;
}

static std::string serialize_json(const std::vector<TimelapseSession>& sessions) {
    std::string j = "[";
    for (size_t i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];
        if (i) j += ",";
        j += "{\"id\":" + std::to_string(s.id)
           + ",\"session_dir\":" + json_str(s.session_dir)
           + ",\"session_name\":" + json_str(s.session_name)
           + ",\"started_at\":" + json_str(s.started_at)
           + ",\"stopped_at\":" + json_str(s.stopped_at)
           + ",\"fn_name\":" + json_str(s.fn_name)
           + ",\"frame_count\":" + std::to_string(s.frame_count)
           + ",\"first_frame_id\":" + std::to_string(s.first_frame_id)
           + ",\"first_frame_blurhash\":" + json_str(s.first_frame_blurhash)
           + "}";
    }
    j += "]";
    return j;
}

static std::string serialize_json(const std::vector<std::string>& strs) {
    std::string j = "[";
    for (size_t i = 0; i < strs.size(); ++i) {
        if (i) j += ",";
        j += json_str(strs[i]);
    }
    j += "]";
    return j;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// Known fixed apertures for official Raspberry Pi camera modules.
// Returns 0 if the sensor has an interchangeable lens (HQ, Global Shutter).
static float pi_camera_aperture(const std::string& model) {
    if (model == "ov5647")      return 2.9f;  // Camera Module 1
    if (model == "imx219")      return 2.0f;  // Camera Module 2
    if (model == "imx708")      return 1.8f;  // Camera Module 3
    if (model == "imx708_wide") return 2.2f;  // Camera Module 3 Wide
    return 0.0f;                              // imx477 (HQ), imx296 (GS) — interchangeable
}

int main() {
    std::signal(SIGHUP,  handle_sighup);
    std::signal(SIGTERM, handle_sigterm);

    std::string config_path = resolve_config_path();
    Config cfg = load_config(config_path);

    // ---- Display initialisation (non-fatal — falls back to headless) ----
    bool headless = false;
    int  rw = cfg.fallback_width;
    int  rh = cfg.fallback_height;

    setenv("SDL_VIDEODRIVER", "kmsdrm", 1);
    setenv("SDL_AUDIODRIVER", "dummy",  1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "[display] SDL_Init: " << SDL_GetError()
                  << " — running headless (MJPEG + socket control only)\n";
        headless = true;
    } else {
        Resolution res = select_best_resolution(0, {cfg.fallback_width, cfg.fallback_height},
                                                cfg.viewfinder_ar);
        rw = res.width;
        rh = res.height;
    }

    std::unique_ptr<Renderer>     renderer;
    std::unique_ptr<Osd>          osd;

    if (!headless) {
        const char* home      = std::getenv("HOME");
        std::string home_str  = home ? home : "/root";
        std::string cache_dir = ensure_dir(home_str + "/.cache/microscopi");
        std::string icons_dir = "/usr/local/share/microscopi/icons";
        std::string font_path = "/usr/local/share/microscopi/fonts/RobotoCondensed-Regular.ttf";

        renderer = std::make_unique<Renderer>(rw, rh);
        renderer->set_crop(cfg.crop_top, cfg.crop_bottom, cfg.crop_left, cfg.crop_right);
        // Use the renderer's actual output dimensions (SDL_GetRendererOutputSize), not
        // rw/rh from select_best_resolution. With FULLSCREEN_DESKTOP the window adopts the
        // display's native mode, which can differ from the camera/selection resolution.
        osd      = std::make_unique<Osd>(renderer->sdl_renderer(),
                                         renderer->width(), renderer->height(),
                                         icons_dir, font_path, cache_dir, cfg.keys);
    }

    Camera camera(cfg.camera_index);
    if (!camera.start(rw, rh, cfg.fps)) {
        std::cerr << "Failed to start camera\n";
        return 1;
    }
    // Resize texture to actual camera output — libcamera may snap to a
    // different resolution than the display (e.g. viewfinder fixed at 1080p
    // on a 1200p display).  Without this, SDL_UpdateYUVTexture reads past the
    // end of the camera buffer and corrupts/crashes.
    if (renderer) renderer->update_texture_size(camera.width(), camera.height());

    Encoder encoder(cfg.video_backend,
                    rw, rh, cfg.fps,
                    cfg.builtin_bitrate,
                    cfg.ffmpeg_command);

    SocketServer sock(cfg.socket_path);

    MjpegServer mjpeg(cfg.stream_port, cfg.stream_quality, cfg.stream_scale, cfg.stream_fps,
                      cfg.stream_https, cfg.stream_cert, cfg.stream_key);

    // ---- Media database ----
    const char* home_env  = std::getenv("HOME");
    std::string home_str  = home_env ? home_env : "/home/microscopi";
    std::string db_dir    = ensure_dir(home_str + "/.local/share/microscopi");
    std::string thumb_dir = ensure_dir(home_str + "/.cache/microscopi/thumbs");
    auto db = std::make_unique<MediaDb>(db_dir + "/media.db",
                                        cfg.stills_dir, cfg.video_dir, cfg.tl_dir);
    mjpeg.set_media_db(db.get(), thumb_dir);

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

    // ---- Control ladders ----
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
            : std::vector<float>{};
    };
    std::vector<float> shutter_ladder  = make_shutter_ladder();
    std::vector<int>   iso_ladder      = make_iso_ladder();
    std::vector<float> aperture_ladder = make_aperture_ladder();

    // ---- Application state ----
    StillFormat still_fmt = StillFormat::JPEG;
    if      (cfg.capture_format == "raw")      still_fmt = StillFormat::RAW;
    else if (cfg.capture_format == "jpeg+raw") still_fmt = StillFormat::JPEG_RAW;

    OsdState   osd_state{};
    bool       recording           = false;
    uint64_t   record_start        = 0;
    int        still_count         = 0;
    std::string current_video_path;       // path of the video being recorded

    // Gallery state (headed mode only)
    GalleryState gallery_state;
    bool         gallery_open = false;

    // Timelapse state
    bool        tl_dialog_open      = false;
    std::string tl_dialog_interval  = "5";
    std::string tl_dialog_frames    = "0";
    int         tl_dialog_field     = 0;
    bool        tl_active           = false;
    int64_t     tl_session_id       = 0;    // DB id of the active timelapse session
    uint64_t    tl_start_mono       = 0;    // now_ms() at session start
    uint64_t    tl_next_ms          = 0;    // absolute time of next capture
    int         tl_count            = 0;    // frames captured this session
    uint64_t    tl_interval_ms      = 0;    // current I(n)
    int         tl_max_frames_run   = 0;    // per-run override of cfg.tl_max_frames
    std::string tl_session_dir;
    TlFn        tl_fn_run           = TlFn::Linear;
    TlParams    tl_params_run;
    bool       af_enabled     = cfg.initial_af_enabled;
    bool       show_crosshair = cfg.show_crosshair;
    bool       should_quit    = false;

    // Exposure state
    ExposureMode mode      = ExposureMode::P;
    int          mode_idx  = 0;
    float        shutter_us   = 16667.0f;
    int          shutter_step = shutter_index(shutter_us, shutter_ladder);
    int          iso          = 0;
    int          iso_step     = -1;  // -1 = AUTO; 0..N-1 = index into iso_ladder
    float        aperture_fs  = cfg.initial_aperture;
    if (aperture_fs <= 0.0f)
        aperture_fs = pi_camera_aperture(camera.model_name());
    int          aperture_step = aperture_ladder.empty() ? 0
                               : aperture_index(aperture_fs, aperture_ladder);

    if (aperture_fs > 0.0f) camera.set_aperture(aperture_fs);
    camera.set_ae_enable(true);
    camera.set_af_enable(af_enabled);

    // ---- Timelapse helpers ----

    auto stop_timelapse = [&]() {
        if (!tl_active) return;
        tl_active = false;
        uint64_t elapsed = now_ms() - tl_start_mono;

        std::string new_name;
        if (cfg.tl_use_rtc) {
            // Append end timestamp to the start-timestamp dir name
            new_name = fs::path(tl_session_dir).filename().string()
                       + "--" + timestamp_filename("");
        } else {
            // Rename to human-readable elapsed duration
            uint64_t h  = elapsed / 3600000;
            uint64_t m  = (elapsed % 3600000) / 60000;
            uint64_t s  = (elapsed % 60000) / 1000;
            uint64_t ms = elapsed % 1000;
            char buf[32];
            snprintf(buf, sizeof(buf), "%02lluh%02llum%02llus%03llums",
                     (unsigned long long)h, (unsigned long long)m,
                     (unsigned long long)s, (unsigned long long)ms);
            new_name = buf;
        }

        std::string new_path = ensure_dir(cfg.tl_dir) + "/" + new_name;
        std::error_code ec;
        fs::rename(tl_session_dir, new_path, ec);
        if (ec) {
            std::cerr << "[tl] rename failed: " << ec.message() << "\n";
        } else {
            tl_session_dir = new_path;
            db->rename_timelapse_session(tl_session_id, new_path);
        }
        db->finish_timelapse(tl_session_id, iso8601_now());

        std::cerr << "[tl] stopped: " << tl_count
                  << " frames, dir: " << tl_session_dir << "\n";
    };

    auto start_timelapse = [&](TlFn fn, TlParams params, int max_frames) -> bool {
        if (tl_active || recording) return false;

        tl_fn_run         = fn;
        tl_params_run     = params;
        tl_max_frames_run = max_frames;
        tl_count          = 0;
        tl_start_mono     = now_ms();
        tl_interval_ms    = next_interval(0, tl_fn_run, tl_params_run);
        tl_next_ms        = tl_start_mono + tl_interval_ms;

        std::string dir_name;
        if (cfg.tl_use_rtc) {
            dir_name = timestamp_filename("");   // YYYYMMDD_HHMMSS
        } else {
            dir_name = "tl_" + std::to_string(tl_start_mono);
        }
        tl_session_dir = ensure_dir(cfg.tl_dir) + "/" + dir_name;
        fs::create_directories(tl_session_dir);

        // Write session metadata (for ffmpeg post-processing reference)
        std::string params_json;
        {
            std::ostringstream pj;
            pj << "{\"fn\":\"" << tl_fn_name(fn) << "\""
               << ",\"base_ms\":"    << params.base_ms
               << ",\"k\":"          << params.k
               << ",\"floor_ms\":"   << params.floor_ms
               << ",\"ceil_ms\":"    << params.ceil_ms
               << ",\"max_frames\":" << max_frames
               << ",\"use_rtc\":"    << (cfg.tl_use_rtc ? "true" : "false")
               << "}";
            params_json = pj.str();
        }
        std::ofstream jf(tl_session_dir + "/session.json");
        if (jf) jf << params_json;

        tl_session_id = db->add_timelapse_session(tl_session_dir, iso8601_now(),
                                                   tl_fn_name(fn), params_json);
        tl_active = true;
        std::cerr << "[tl] started: fn=" << tl_fn_name(fn)
                  << " base=" << params.base_ms << "ms"
                  << " dir=" << tl_session_dir << "\n";
        return true;
    };

    auto apply_mode = [&](ExposureMode m) {
        mode     = m;
        mode_idx = (int)m;
        switch (m) {
        case ExposureMode::P:
            camera.set_ae_enable(true);
            break;
        case ExposureMode::A:
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

    auto rebuild_ladders = [&]{
        shutter_ladder  = make_shutter_ladder();
        iso_ladder      = make_iso_ladder();
        aperture_ladder = make_aperture_ladder();
        shutter_step  = shutter_index(shutter_us, shutter_ladder);
        iso_step      = iso == 0 ? -1 : iso_index(iso, iso_ladder);
        aperture_step = aperture_ladder.empty() ? 0
                      : aperture_index(aperture_fs, aperture_ladder);
    };

    // ---- Input callbacks (defined for both headed and headless; only wired in headed mode) ----
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
        camera.set_lens_position(std::clamp(pos + cfg.focus_key_step, 0.0f, 1.0f));
        af_enabled = false;
    };
    cbs.on_focus_down = [&]{
        CameraStatus st = camera.get_status();
        float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
        camera.set_lens_position(std::clamp(pos - cfg.focus_key_step, 0.0f, 1.0f));
        af_enabled = false;
    };

    cbs.on_iso_up = [&]{
        if (iso_ladder.empty()) return;
        if (iso_step >= (int)iso_ladder.size() - 1) {
            iso_step = -1;   // wrap: max ISO → AUTO
            iso = 0;
        } else {
            ++iso_step;
            iso = iso_ladder[iso_step];
            camera.set_iso(iso);
        }
    };
    cbs.on_iso_down = [&]{
        if (iso_ladder.empty()) return;
        if (iso_step < 0) {
            iso_step = (int)iso_ladder.size() - 1;  // wrap: AUTO → max ISO
            iso = iso_ladder[iso_step];
            camera.set_iso(iso);
        } else if (iso_step == 0) {
            iso_step = -1;   // min ISO → AUTO
            iso = 0;
        } else {
            --iso_step;
            iso = iso_ladder[iso_step];
            camera.set_iso(iso);
        }
    };

    cbs.on_toggle_af = [&]{
        af_enabled = !af_enabled;
        camera.set_af_enable(af_enabled);
    };

    cbs.on_toggle_crosshair = [&]{ show_crosshair = !show_crosshair; };

    auto do_still = [&](std::string path, bool count = true) -> bool {
        if (!camera.capture_still(path, still_fmt)) return false;
        if (count) ++still_count;
        if (still_fmt != StillFormat::RAW) {
            CameraStatus st = camera.get_status();
            ExifParams exif;
            exif.exposure_us   = st.exposure_time;
            exif.fstop         = (st.aperture > 0) ? st.aperture : aperture_fs;
            exif.iso           = (iso != 0) ? iso : st.iso;
            exif.lens_position = st.lens_position;
            exif.exposure_mode = mode_idx;
            exif.camera_model  = camera.model_name();
            time_t now_t = time(nullptr);
            struct tm* tm_info = localtime(&now_t);
            char dtbuf[20];
            strftime(dtbuf, sizeof(dtbuf), "%Y:%m:%d %H:%M:%S", tm_info);
            exif.datetime = dtbuf;
            insert_exif(path, exif);
        }
        return true;
    };

    cbs.on_still = [&]{
        std::string path = ensure_dir(cfg.stills_dir) + "/" + timestamp_filename(".jpg");
        if (do_still(path)) db->add_still(path);
    };

    cbs.on_focus_scroll = [&](int dir) {
        CameraStatus st = camera.get_status();
        float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
        camera.set_lens_position(std::clamp(pos + dir * cfg.focus_scroll_step, 0.0f, 1.0f));
        af_enabled = false;
    };

    cbs.on_cam_mode_toggle = [&]{
        cam_mode_open     = !cam_mode_open;
        cam_mode_selected = cam_mode_active;
    };
    cbs.on_cam_mode_up = [&]{
        int n = (int)cam_modes.size();
        cam_mode_selected = (cam_mode_selected - 1 + n) % n;
    };
    cbs.on_cam_mode_down = [&]{
        int n = (int)cam_modes.size();
        cam_mode_selected = (cam_mode_selected + 1) % n;
    };
    cbs.on_cam_mode_cancel = [&]{ cam_mode_open = false; };
    cbs.on_cam_mode_confirm = [&]{
        cam_mode_open = false;
        if (cam_mode_selected == cam_mode_active) return;
        const CameraMode& m = cam_modes[cam_mode_selected];
        if (camera.restart_with_mode(m)) {
            if (renderer) renderer->update_texture_size(camera.width(), camera.height());
            cam_mode_active   = find_active_mode();
            cam_mode_selected = cam_mode_active;
            rebuild_ladders();
        }
    };

    cbs.on_record_toggle = [&]{
        if (!recording) {
            if (tl_active) return; // mutual exclusion
            current_video_path = ensure_dir(cfg.video_dir) + "/" + timestamp_filename(".mkv");
            if (encoder.open(current_video_path)) {
                recording    = true;
                record_start = now_ms();
            }
        } else {
            encoder.close();
            recording = false;
            if (!current_video_path.empty()) db->add_video(current_video_path);
        }
    };

    cbs.on_timelapse_tap = [&]{
        if (!recording && !tl_active) tl_dialog_open = true;
    };
    cbs.on_timelapse_stop = [&]{
        if (tl_active) stop_timelapse();
    };
    cbs.on_tl_dialog_char = [&](char c) {
        auto& s = (tl_dialog_field == 0) ? tl_dialog_interval : tl_dialog_frames;
        if (s.size() < 6) s += c;
    };
    cbs.on_tl_dialog_backspace = [&]{
        auto& s = (tl_dialog_field == 0) ? tl_dialog_interval : tl_dialog_frames;
        if (!s.empty()) s.pop_back();
    };
    cbs.on_tl_dialog_tab = [&]{ tl_dialog_field ^= 1; };
    cbs.on_tl_dialog_confirm = [&]{
        double iv_s = 0.5;
        try {
            if (!tl_dialog_interval.empty())
                iv_s = std::max(0.5, std::stod(tl_dialog_interval));
        } catch (...) {}
        int max_f = 0;
        try {
            if (!tl_dialog_frames.empty())
                max_f = std::stoi(tl_dialog_frames);
        } catch (...) {}
        TlParams params;
        params.base_ms    = static_cast<uint64_t>(iv_s * 1000);
        params.k          = cfg.tl_rate_constant;
        params.power      = cfg.tl_power;
        params.beta       = cfg.tl_beta;
        params.inflection = cfg.tl_inflection;
        params.floor_ms   = cfg.tl_floor_ms;
        params.ceil_ms    = cfg.tl_ceil_ms;
        TlFn fn = parse_tl_fn(cfg.tl_fn, params);
        start_timelapse(fn, params, max_f);
        tl_dialog_open = false;
        tl_dialog_field = 0;
    };
    cbs.on_tl_dialog_cancel = [&]{
        tl_dialog_open = false;
        tl_dialog_field = 0;
    };

    // ---- Gallery callbacks (headed mode only; db is always available) ----
    auto gallery_per_page = [&]() -> int {
        return std::max(1, gallery_state.tiles_per_row * gallery_state.rows_visible);
    };
    auto gallery_reload = [&]{
        gallery_load_page(gallery_state, db.get(),
                          renderer ? renderer->sdl_renderer() : nullptr,
                          thumb_dir);
    };
    cbs.on_gallery_toggle = [&]{
        if (gallery_open) {
            gallery_free_textures(gallery_state);
            gallery_open = false;
        } else {
            gallery_state = GalleryState{};
            if (renderer)
                gallery_compute_layout(gallery_state,
                                       renderer->width(), renderer->height());
            gallery_reload();
            gallery_open = true;
        }
    };
    cbs.on_gallery_next_tab = [&]{
        if (!gallery_open) return;
        gallery_state.tab = static_cast<GalleryState::Tab>(
            ((int)gallery_state.tab + 1) % 3);
        gallery_state.page = 0;
        gallery_state.selection = 0;
        gallery_state.tl_session_id = 0;
        gallery_state.tl_session_name.clear();
        gallery_reload();
    };
    cbs.on_gallery_nav_left = [&]{
        if (!gallery_open) return;
        if (gallery_state.selection > 0) --gallery_state.selection;
    };
    cbs.on_gallery_nav_right = [&]{
        if (!gallery_open) return;
        int total = (int)std::max(gallery_state.items.size(), gallery_state.sessions.size());
        if (gallery_state.selection < total - 1) ++gallery_state.selection;
    };
    cbs.on_gallery_nav_up = [&]{
        if (!gallery_open) return;
        if (gallery_state.selection >= gallery_state.tiles_per_row)
            gallery_state.selection -= gallery_state.tiles_per_row;
        else if (gallery_state.page > 0) {
            --gallery_state.page;
            gallery_reload();
            int per = gallery_per_page();
            gallery_state.selection = std::max(0, per - 1);
        }
    };
    cbs.on_gallery_nav_down = [&]{
        if (!gallery_open) return;
        int total = (int)std::max(gallery_state.items.size(), gallery_state.sessions.size());
        int next = gallery_state.selection + gallery_state.tiles_per_row;
        if (next < total) {
            gallery_state.selection = next;
        } else if (total == gallery_per_page()) {
            ++gallery_state.page;
            gallery_state.selection = 0;
            gallery_reload();
        }
    };
    cbs.on_gallery_select = [&]{
        if (!gallery_open) return;
        if (gallery_state.fullscreen) return; // already in fullscreen
        int idx = gallery_state.selection;
        if (gallery_state.tab == GalleryState::Tab::Timelapses &&
            gallery_state.tl_session_id == 0) {
            // Drill into timelapse session.
            if (idx < (int)gallery_state.sessions.size()) {
                gallery_state.tl_session_id   = gallery_state.sessions[idx].id;
                gallery_state.tl_session_name = gallery_state.sessions[idx].session_name;
                gallery_state.page      = 0;
                gallery_state.selection = 0;
                gallery_reload();
            }
        } else {
            // Open still/frame fullscreen.
            if (idx < (int)gallery_state.items.size()) {
                const auto& item = gallery_state.items[idx];
                if (item.type == "still" || item.type == "tl_frame") {
                    SDL_Surface* sur = IMG_Load(item.path.c_str());
                    if (sur && renderer) {
                        if (gallery_state.preview_tex)
                            SDL_DestroyTexture(gallery_state.preview_tex);
                        gallery_state.preview_tex  = SDL_CreateTextureFromSurface(
                            renderer->sdl_renderer(), sur);
                        SDL_FreeSurface(sur);
                        gallery_state.preview_name = item.filename;
                        gallery_state.fullscreen   = true;
                    }
                }
            }
        }
    };
    cbs.on_gallery_back = [&]{
        if (!gallery_open) return;
        if (gallery_state.fullscreen) {
            if (gallery_state.preview_tex) {
                SDL_DestroyTexture(gallery_state.preview_tex);
                gallery_state.preview_tex = nullptr;
            }
            gallery_state.fullscreen = false;
        } else if (gallery_state.tl_session_id > 0) {
            gallery_state.tl_session_id = 0;
            gallery_state.tl_session_name.clear();
            gallery_state.page = 0;
            gallery_state.selection = 0;
            gallery_reload();
        } else {
            gallery_free_textures(gallery_state);
            gallery_open = false;
        }
    };

    // InputHandler only exists when SDL is available
    std::unique_ptr<InputHandler> input;
    if (!headless) {
        input = std::make_unique<InputHandler>(std::move(cbs), cfg.keys);
    }

    // ---- Socket + MJPEG command dispatch ----
    static const char* kModeNames[] = {"P", "A", "S", "M"};

    // Returns 1 (true), 0 (false), or -1 (invalid) for bool socket args.
    // Accepts: true/false, 1/0, on/off, yes/no (case-insensitive).
    auto parse_bool = [](const std::string& s) -> int {
        std::string lo = s;
        for (auto& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lo == "true"  || lo == "1" || lo == "on"  || lo == "yes") return 1;
        if (lo == "false" || lo == "0" || lo == "off" || lo == "no")  return 0;
        return -1;
    };

    auto dispatch_cmd = [&](const std::string& line) -> std::string {
        std::istringstream ss(line);
        std::vector<std::string> args;
        std::string tok;
        while (ss >> tok) args.push_back(tok);
        if (args.empty()) return "ERR empty command";

        const auto& verb = args[0];

        if (verb == "ping") return "PONG";

        if (verb == "help")
            return "OK ping status still record_start record_stop "
                   "focus(<pos>|up|down) iso(<val>|auto) shutter(<us>) "
                   "mode(p|a|s|m) af(<bool>) crosshair(<bool>) timelapse(start|stop|status) "
                   "gallery(list|scan|verify) quit";

        if (verb == "status") {
            CameraStatus st = camera.get_status();
            std::ostringstream j;
            j << "{"
              << "\"mode\":\"" << kModeNames[mode_idx] << "\""
              << ",\"iso\":"        << (iso != 0 ? iso : st.iso)
              << ",\"shutter_us\":" << st.exposure_time
              << ",\"aperture\":"   << st.aperture
              << ",\"lens_pos\":"   << (std::isnan(st.lens_position) ? -1.0f : st.lens_position)
              << ",\"af\":"         << (af_enabled  ? "true" : "false")
              << ",\"ae\":"         << (st.ae_enabled ? "true" : "false")
              << ",\"recording\":"  << (recording   ? "true" : "false")
              << ",\"still_count\":" << still_count
              << ",\"dual_stream\":" << (camera.dual_stream() ? "true" : "false")
              << "}";
            return j.str();
        }

        if (verb == "still") {
            std::string path = ensure_dir(cfg.stills_dir) + "/" + timestamp_filename(".jpg");
            if (!do_still(path)) return "ERR capture failed";
            db->add_still(path);
            return "OK " + path;
        }

        if (verb == "record_start") {
            if (recording) return "ERR already recording";
            current_video_path = ensure_dir(cfg.video_dir) + "/" + timestamp_filename(".mkv");
            if (!encoder.open(current_video_path)) return "ERR encoder open failed";
            recording    = true;
            record_start = now_ms();
            return "OK " + current_video_path;
        }

        if (verb == "record_stop") {
            if (!recording) return "ERR not recording";
            encoder.close();
            recording = false;
            if (!current_video_path.empty()) db->add_video(current_video_path);
            return "OK";
        }

        if (verb == "focus") {
            if (args.size() < 2) return "ERR usage: focus <0.0-1.0>|up|down";
            CameraStatus st = camera.get_status();
            float pos = std::isnan(st.lens_position) ? 0.5f : st.lens_position;
            if (args[1] == "up") {
                camera.set_lens_position(std::clamp(pos + cfg.focus_key_step, 0.0f, 1.0f));
            } else if (args[1] == "down") {
                camera.set_lens_position(std::clamp(pos - cfg.focus_key_step, 0.0f, 1.0f));
            } else {
                try {
                    float v = std::stof(args[1]);
                    if (v < 0.0f || v > 1.0f) return "ERR value out of range 0.0-1.0";
                    camera.set_lens_position(v);
                } catch (...) { return "ERR invalid value"; }
            }
            af_enabled = false;
            return "OK";
        }

        if (verb == "iso") {
            if (args.size() < 2) return "ERR usage: iso <value>|auto";
            std::string iso_arg = args[1];
            // case-insensitive "auto"
            for (auto& ch : iso_arg) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (iso_arg == "auto") {
                iso = 0; iso_step = -1;
            } else {
                try {
                    size_t pos = 0;
                    long v = std::stol(args[1], &pos);
                    if (pos != args[1].size()) return "ERR invalid iso value";  // reject "1.5", "1x", etc.
                    if (v <= 0)     return "ERR iso must be a positive integer";
                    if (v > 102400) return "ERR iso value out of range (max 102400)";
                    iso_step = iso_index((int)v, iso_ladder);
                    iso = iso_ladder[iso_step];
                    camera.set_iso(iso);
                } catch (...) { return "ERR invalid iso value"; }
            }
            return "OK";
        }

        if (verb == "shutter") {
            if (args.size() < 2) return "ERR usage: shutter <microseconds>";
            try {
                size_t pos = 0;
                long v = std::stol(args[1], &pos);
                if (pos != args[1].size()) return "ERR invalid value";   // reject "1.5", "1x", etc.
                if (v <= 0)       return "ERR shutter must be positive (µs)";
                if (v > 2000000)  return "ERR shutter exceeds maximum (2000000 µs = 2 s)";
                shutter_us   = static_cast<float>(v);
                shutter_step = shutter_index(shutter_us, shutter_ladder);
                camera.set_shutter_speed(shutter_us);
            } catch (...) { return "ERR invalid value"; }
            return "OK";
        }

        if (verb == "mode") {
            if (args.size() < 2) return "ERR usage: mode p|a|s|m";
            if (args.size() > 2) return "ERR usage: mode p|a|s|m";
            if (args[1].size() != 1) return "ERR unknown mode — use p a s m";
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(args[1][0])));
            int idx = (c == 'p') ? 0 : (c == 'a') ? 1 : (c == 's') ? 2 : (c == 'm') ? 3 : -1;
            if (idx < 0) return "ERR unknown mode — use p a s m";
            apply_mode(static_cast<ExposureMode>(idx));
            return "OK";
        }

        if (verb == "af") {
            if (args.size() < 2) return "ERR usage: af <true|false>";
            int b = parse_bool(args[1]);
            if (b < 0) return "ERR usage: af <true|false>";
            af_enabled = (b == 1);
            camera.set_af_enable(af_enabled);
            return "OK";
        }

        if (verb == "crosshair") {
            if (args.size() < 2) return "ERR usage: crosshair <true|false>";
            int b = parse_bool(args[1]);
            if (b < 0) return "ERR usage: crosshair <true|false>";
            show_crosshair = (b == 1);
            return "OK";
        }

        if (verb == "quit") {
            should_quit = true;
            return "OK";
        }

        if (verb == "timelapse") {
            if (args.size() < 2)
                return "ERR usage: timelapse start|stop|status";
            const auto& sub = args[1];

            if (sub == "stop") {
                if (!tl_active) return "ERR not running";
                stop_timelapse();
                return "OK";
            }

            if (sub == "status") {
                std::ostringstream j;
                j << "{"
                  << "\"active\":"    << (tl_active ? "true" : "false")
                  << ",\"count\":"    << tl_count
                  << ",\"fn\":\""     << tl_fn_name(tl_fn_run) << "\""
                  << ",\"base_ms\":"  << tl_params_run.base_ms
                  << ",\"k\":"        << tl_params_run.k
                  << ",\"floor_ms\":" << tl_params_run.floor_ms
                  << ",\"ceil_ms\":"  << tl_params_run.ceil_ms
                  << ",\"interval_ms\":" << tl_interval_ms;
                if (tl_active) {
                    uint64_t now_t = now_ms();
                    uint64_t rem = (now_t < tl_next_ms) ? (tl_next_ms - now_t) : 0;
                    j << ",\"next_in_ms\":" << rem;
                }
                j << "}";
                return j.str();
            }

            if (sub == "start") {
                if (tl_active)  return "ERR already running";
                if (recording)  return "ERR cannot timelapse while recording";

                TlParams params;
                params.base_ms    = cfg.tl_base_ms;
                params.k          = cfg.tl_rate_constant;
                params.power      = cfg.tl_power;
                params.beta       = cfg.tl_beta;
                params.inflection = cfg.tl_inflection;
                params.floor_ms   = cfg.tl_floor_ms;
                params.ceil_ms    = cfg.tl_ceil_ms;
                std::string fn_name = cfg.tl_fn;
                int max_frames = cfg.tl_max_frames;

                for (size_t i = 2; i < args.size(); ++i) {
                    auto eq = args[i].find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = args[i].substr(0, eq);
                    std::string v = args[i].substr(eq + 1);
                    try {
                        if (k == "base") {
                            long lv = std::stol(v);
                            if (lv < 100) return "ERR base must be >= 100 ms";
                            params.base_ms = static_cast<uint64_t>(lv);
                        } else if (k == "fn") {
                            fn_name = v;
                        } else if (k == "k") {
                            params.k = std::stof(v);
                        } else if (k == "power") {
                            params.power = std::stof(v);
                        } else if (k == "beta") {
                            params.beta = std::stof(v);
                        } else if (k == "inflection") {
                            params.inflection = std::stoi(v);
                        } else if (k == "floor") {
                            params.floor_ms = (uint64_t)std::stoull(v);
                        } else if (k == "ceil") {
                            params.ceil_ms = (uint64_t)std::stoull(v);
                        } else if (k == "max") {
                            max_frames = std::stoi(v);
                        } else {
                            return "ERR unknown parameter: " + k;
                        }
                    } catch (...) { return "ERR invalid value for " + k; }
                }

                // Validate fn name before starting
                static const char* kValidFns[] = {
                    "linear","exp_grow","exp_decay","log","power",
                    "quadratic","cubic","quintic","michaelis",
                    "logistic","stretched_exp","hyperbolic"
                };
                bool fn_valid = false;
                for (const char* n : kValidFns) if (fn_name == n) { fn_valid = true; break; }
                if (!fn_valid) return "ERR unknown timelapse function: " + fn_name;

                TlFn fn = parse_tl_fn(fn_name, params);
                if (!start_timelapse(fn, params, max_frames)) return "ERR start failed";
                return "OK fn=" + tl_fn_name(fn) + " base=" + std::to_string(params.base_ms) + "ms";
            }

            return "ERR usage: timelapse start|stop|status";
        }

        if (verb == "gallery") {
            if (args.size() < 2) return "ERR usage: gallery list|scan|verify";

            if (args[1] == "scan") {
                auto r = db->rebuild_from_disk();
                return "{\"added\":" + std::to_string(r.added) +
                       ",\"removed\":" + std::to_string(r.removed) + "}";
            }

            if (args[1] == "verify") {
                return serialize_json(db->verify());
            }

            if (args[1] == "list") {
                std::string type;
                int page = 0, limit = 20;
                int64_t session_id = 0;
                for (size_t i = 2; i < args.size(); ++i) {
                    auto eq = args[i].find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = args[i].substr(0, eq);
                    std::string v = args[i].substr(eq + 1);
                    try {
                        if      (k == "type")    type       = v;
                        else if (k == "page")    page       = std::stoi(v);
                        else if (k == "limit")   limit      = std::stoi(v);
                        else if (k == "session") session_id = std::stoll(v);
                    } catch (...) {}
                }
                limit  = std::max(1, std::min(limit, 100));
                int offset = page * limit;

                if (type == "stills")     return serialize_json(db->list_stills(offset, limit));
                if (type == "videos")     return serialize_json(db->list_videos(offset, limit));
                if (type == "timelapses") return serialize_json(db->list_timelapses(offset, limit));
                if (type == "frames")     return serialize_json(db->list_timelapse_frames(session_id, offset, limit));
                return "ERR unknown type — use stills|videos|timelapses|frames";
            }

            return "ERR usage: gallery list|scan|verify";
        }

        return "ERR unknown command: " + verb;
    };

    int cam_fail_count = 0;

    // ---- Main loop ----
    while (!should_quit && !g_quit.load()) {
        // ---- SIGHUP config hot-reload ----
        if (g_reload_config.exchange(false)) {
            Config nc = load_config(config_path);
            cfg.crop_top          = nc.crop_top;
            cfg.crop_bottom       = nc.crop_bottom;
            cfg.crop_left         = nc.crop_left;
            cfg.crop_right        = nc.crop_right;
            cfg.focus_scroll_step = nc.focus_scroll_step;
            cfg.focus_key_step    = nc.focus_key_step;
            cfg.show_crosshair    = nc.show_crosshair;
            cfg.stills_dir        = nc.stills_dir;
            cfg.video_dir         = nc.video_dir;
            cfg.stream_quality    = nc.stream_quality;
            cfg.stream_fps        = nc.stream_fps;
            cfg.stream_scale      = nc.stream_scale;
            if (renderer) renderer->set_crop(cfg.crop_top, cfg.crop_bottom, cfg.crop_left, cfg.crop_right);
            show_crosshair = cfg.show_crosshair;
            std::cerr << "[config] reloaded from " << config_path << "\n";
        }

        // ---- Unix socket remote control ----
        {
            std::string cmd;
            if (sock.poll(cmd))
                sock.reply(dispatch_cmd(cmd));
        }

        // ---- MJPEG REST command queue ----
        {
            std::string cmd;
            std::function<void(const std::string&)> reply_fn;
            while (mjpeg.pop_command(cmd, reply_fn))
                reply_fn(dispatch_cmd(cmd));
        }

        // ---- SDL input (headed only) ----
        if (input) {
            input->set_mode_list_open(cam_mode_open);
            input->set_tl_dialog_open(tl_dialog_open);
            input->set_tl_active(tl_active);
            input->set_gallery_open(gallery_open);
            if (!input->process_events()) break;
        }

        CameraFrame frame;
        if (!camera.get_frame(frame)) {
            // ---- Graceful camera re-init ----
            if (++cam_fail_count < 30) continue;
            cam_fail_count = 0;
            std::cerr << "[camera] no frames — attempting reconnect\n";
            if (recording) { encoder.close(); recording = false; }
            camera.stop();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (camera.reconnect() && camera.start(rw, rh, cfg.fps)) {
                if (renderer) renderer->update_texture_size(camera.width(), camera.height());
                rebuild_ladders();
                cam_mode_active = find_active_mode();
                std::cerr << "[camera] reconnected\n";
            } else {
                std::cerr << "[camera] reconnect failed — will retry\n";
            }
            continue;
        }
        cam_fail_count = 0;

        if (recording)
            encoder.submit_frame(frame.y, frame.u, frame.v,
                                 frame.y_stride, frame.uv_stride);

        // Push raw viewfinder frame to MJPEG server before OSD is composited
        mjpeg.push_frame(frame.y, frame.u, frame.v,
                         frame.width, frame.height,
                         frame.y_stride, frame.uv_stride);

        // ---- Timelapse frame capture ----
        if (tl_active) {
            uint64_t now = now_ms();
            if (now >= tl_next_ms) {
                std::string fname;
                if (cfg.tl_use_rtc) {
                    char buf[24];
                    snprintf(buf, sizeof(buf), "frame_%06d.jpg", tl_count + 1);
                    fname = buf;
                } else {
                    uint64_t elapsed = now - tl_start_mono;
                    char buf[24];
                    snprintf(buf, sizeof(buf), "t%010llu.jpg", (unsigned long long)elapsed);
                    fname = buf;
                }
                std::string frame_path = tl_session_dir + "/" + fname;
                if (do_still(frame_path, false)) {
                    ++tl_count;
                    db->add_timelapse_frame(tl_session_id, frame_path);
                    if (tl_max_frames_run > 0 && tl_count >= tl_max_frames_run) {
                        stop_timelapse();
                    } else {
                        tl_interval_ms = next_interval(tl_count, tl_fn_run, tl_params_run);
                        tl_next_ms     = now_ms() + tl_interval_ms;
                    }
                }
            }
        }

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
        osd_state.show_crosshair    = show_crosshair;
        osd_state.mode_list         = {cam_mode_open, cam_mode_selected,
                                       cam_mode_active, &cam_mode_labels};
        osd_state.cam_width          = camera.width();
        osd_state.cam_height         = camera.height();
        osd_state.tl_active          = tl_active;
        osd_state.tl_count           = tl_count;
        osd_state.tl_next_ms         = tl_next_ms;
        osd_state.tl_interval_ms     = tl_interval_ms;
        osd_state.tl_dialog_open     = tl_dialog_open;
        osd_state.tl_dialog_field    = tl_dialog_field;
        osd_state.tl_dialog_interval = tl_dialog_interval;
        osd_state.tl_dialog_frames   = tl_dialog_frames;
        if (input) {
            osd_state.record_hold_progress = input->record_hold_progress();
            osd_state.quit_hold_progress   = input->quit_hold_progress();
            osd_state.show_help            = input->help_visible();
            osd_state.tl_hold_progress     = input->timelapse_hold_progress();
        }

        // Build status JSON for MJPEG /api/status
        {
            std::ostringstream j;
            j << "{"
              << "\"mode\":\"" << kModeNames[mode_idx] << "\""
              << ",\"iso\":"        << (iso != 0 ? iso : status.iso)
              << ",\"shutter_us\":" << status.exposure_time
              << ",\"aperture\":"   << status.aperture
              << ",\"lens_pos\":"   << (std::isnan(status.lens_position) ? -1.0f : status.lens_position)
              << ",\"af\":"         << (af_enabled     ? "true" : "false")
              << ",\"ae\":"         << (status.ae_enabled ? "true" : "false")
              << ",\"recording\":"   << (recording      ? "true" : "false")
              << ",\"still_count\":" << still_count
              << ",\"dual_stream\":" << (camera.dual_stream() ? "true" : "false")
              << ",\"tl_active\":"   << (tl_active ? "true" : "false")
              << ",\"tl_count\":"    << tl_count
              << ",\"tl_fn\":\""     << tl_fn_name(tl_fn_run) << "\""
              << "}";
            mjpeg.set_status(j.str());
        }

        osd_state.gallery_open = gallery_open;
        osd_state.gallery      = gallery_open ? &gallery_state : nullptr;

        if (renderer) {
            renderer->present_frame(frame.y, frame.u, frame.v,
                                    frame.y_stride, frame.uv_stride,
                                    osd.get(), osd_state);
        }
        frame.release();
    }

    if (tl_active) stop_timelapse();
    if (recording) encoder.close();
    if (gallery_open) gallery_free_textures(gallery_state);
    camera.stop();
    return 0;
}
