#include "camera/camera.h"
#include "camera/encoder.h"
#include "config/config.h"
#include "ui/input.h"
#include "ui/osd.h"
#include "ui/renderer.h"
#include "util/exif_writer.h"
#include "util/exposure.h"
#include "util/mjpeg_server.h"
#include "util/resolution.h"
#include "util/socket_server.h"

#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <csignal>
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


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

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
        Resolution res = select_best_resolution(0, {cfg.fallback_width, cfg.fallback_height});
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
        osd      = std::make_unique<Osd>(renderer->sdl_renderer(), rw, rh,
                                         icons_dir, font_path, cache_dir, cfg.keys);
    }

    Camera camera(cfg.camera_index);
    if (!camera.start(rw, rh, cfg.fps)) {
        std::cerr << "Failed to start camera\n";
        return 1;
    }

    Encoder encoder(cfg.video_backend,
                    rw, rh, cfg.fps,
                    cfg.builtin_bitrate,
                    cfg.ffmpeg_command);

    SocketServer sock(cfg.socket_path);

    MjpegServer mjpeg(cfg.stream_port, cfg.stream_quality, cfg.stream_scale, cfg.stream_fps);

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
    bool       recording      = false;
    uint64_t   record_start   = 0;
    int        still_count    = 0;
    bool       af_enabled     = cfg.initial_af_enabled;
    bool       show_crosshair = cfg.show_crosshair;
    bool       should_quit    = false;

    // Exposure state
    ExposureMode mode      = ExposureMode::P;
    int          mode_idx  = 0;
    float        shutter_us   = 16667.0f;
    int          shutter_step = shutter_index(shutter_us, shutter_ladder);
    int          iso          = 0;
    int          iso_step     = 0;
    float        aperture_fs  = cfg.initial_aperture;
    int          aperture_step = aperture_ladder.empty() ? 0
                               : aperture_index(aperture_fs, aperture_ladder);

    if (aperture_fs > 0.0f) camera.set_aperture(aperture_fs);
    camera.set_ae_enable(true);
    camera.set_af_enable(af_enabled);

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
        iso_step      = iso == 0 ? 0 : iso_index(iso, iso_ladder);
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

    auto do_still = [&](std::string path) -> bool {
        if (!camera.capture_still(path, still_fmt)) return false;
        ++still_count;
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
        do_still(ensure_dir(cfg.stills_dir) + "/" + timestamp_filename(".jpg"));
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
            std::string path = ensure_dir(cfg.video_dir) + "/" + timestamp_filename(".mkv");
            if (encoder.open(path)) {
                recording    = true;
                record_start = now_ms();
            }
        } else {
            encoder.close();
            recording = false;
        }
    };

    // InputHandler only exists when SDL is available
    std::unique_ptr<InputHandler> input;
    if (!headless) {
        input = std::make_unique<InputHandler>(std::move(cbs), cfg.keys);
    }

    // ---- Socket + MJPEG command dispatch ----
    static const char* kModeNames[] = {"P", "A", "S", "M"};

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
                   "mode(p|a|s|m) af(on|off) ae(on|off) crosshair(on|off) quit";

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
            return "OK " + path;
        }

        if (verb == "record_start") {
            if (recording) return "ERR already recording";
            std::string path = ensure_dir(cfg.video_dir) + "/" + timestamp_filename(".mkv");
            if (!encoder.open(path)) return "ERR encoder open failed";
            recording    = true;
            record_start = now_ms();
            return "OK " + path;
        }

        if (verb == "record_stop") {
            if (!recording) return "ERR not recording";
            encoder.close();
            recording = false;
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
            if (args[1] == "auto") {
                iso = 0; iso_step = 0;
            } else {
                try {
                    int v = std::stoi(args[1]);
                    iso_step = iso_index(v, iso_ladder);
                    iso = iso_ladder[iso_step];
                    camera.set_iso(iso);
                } catch (...) { return "ERR invalid iso value"; }
            }
            return "OK";
        }

        if (verb == "shutter") {
            if (args.size() < 2) return "ERR usage: shutter <microseconds>";
            try {
                float v = std::stof(args[1]);
                shutter_us   = v;
                shutter_step = shutter_index(v, shutter_ladder);
                camera.set_shutter_speed(v);
            } catch (...) { return "ERR invalid value"; }
            return "OK";
        }

        if (verb == "mode") {
            if (args.size() < 2) return "ERR usage: mode p|a|s|m";
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(args[1][0])));
            int idx = (c == 'p') ? 0 : (c == 'a') ? 1 : (c == 's') ? 2 : (c == 'm') ? 3 : -1;
            if (idx < 0) return "ERR unknown mode — use p a s m";
            apply_mode(static_cast<ExposureMode>(idx));
            return "OK";
        }

        if (verb == "af") {
            if (args.size() < 2) return "ERR usage: af on|off";
            af_enabled = (args[1] == "on");
            camera.set_af_enable(af_enabled);
            return "OK";
        }

        if (verb == "ae") {
            if (args.size() < 2) return "ERR usage: ae on|off";
            camera.set_ae_enable(args[1] == "on");
            return "OK";
        }

        if (verb == "crosshair") {
            if (args.size() < 2) return "ERR usage: crosshair on|off";
            show_crosshair = (args[1] == "on");
            return "OK";
        }

        if (verb == "quit") {
            should_quit = true;
            return "OK";
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
        if (input) {
            osd_state.record_hold_progress = input->record_hold_progress();
            osd_state.quit_hold_progress   = input->quit_hold_progress();
            osd_state.show_help            = input->help_visible();
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
              << ",\"recording\":"  << (recording      ? "true" : "false")
              << ",\"still_count\":" << still_count
              << ",\"dual_stream\":" << (camera.dual_stream() ? "true" : "false")
              << "}";
            mjpeg.set_status(j.str());
        }

        if (renderer) {
            renderer->present_frame(frame.y, frame.u, frame.v,
                                    frame.y_stride, frame.uv_stride,
                                    osd.get(), osd_state);
        }
        frame.release();
    }

    if (recording) encoder.close();
    camera.stop();
    return 0;
}
