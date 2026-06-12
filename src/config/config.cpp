#include "config.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Built-in defaults
// ---------------------------------------------------------------------------

static Config defaults() {
    Config c;
    c.video_backend      = "builtin";
    c.builtin_bitrate    = 5000000;
    c.ffmpeg_command     = "ffmpeg -f rawvideo -pix_fmt yuv420p -s {width}x{height}"
                           " -r {fps} -i pipe:0 -c:v h264_v4l2m2m -b:v 5M -f matroska {output}";
    c.video_dir          = "/home/microscopi/videos";
    c.stills_dir         = "/home/microscopi/stills";
    c.camera_index       = 0;
    c.fps                = 30;
    c.initial_ae_enabled = true;
    c.initial_af_enabled = true;
    c.initial_aperture   = 0.0f;
    c.fallback_width     = 1280;
    c.fallback_height    = 720;
    c.show_crosshair     = false;
    return c;
}

// ---------------------------------------------------------------------------
// Parsing helpers — all log a warning and return the default on bad input
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static int parse_int(const std::string& key, const std::string& val, int fallback) {
    try {
        size_t pos;
        int r = std::stoi(val, &pos);
        if (pos != val.size()) throw std::invalid_argument("trailing chars");
        return r;
    } catch (...) {
        std::cerr << "[config] bad integer for '" << key << "': \"" << val
                  << "\" — using default (" << fallback << ")\n";
        return fallback;
    }
}

static float parse_float(const std::string& key, const std::string& val, float fallback) {
    try {
        size_t pos;
        float r = std::stof(val, &pos);
        if (pos != val.size()) throw std::invalid_argument("trailing chars");
        return r;
    } catch (...) {
        std::cerr << "[config] bad float for '" << key << "': \"" << val
                  << "\" — using default (" << fallback << ")\n";
        return fallback;
    }
}

static bool parse_bool(const std::string& key, const std::string& val, bool fallback) {
    if (val == "true"  || val == "1" || val == "yes" || val == "on")  return true;
    if (val == "false" || val == "0" || val == "no"  || val == "off") return false;
    std::cerr << "[config] bad boolean for '" << key << "': \"" << val
              << "\" — using default (" << (fallback ? "true" : "false") << ")\n";
    return fallback;
}

// ---------------------------------------------------------------------------
// load_config
// ---------------------------------------------------------------------------

Config load_config(const std::string& path) {
    Config c = defaults();
    if (path.empty()) return c;

    std::ifstream f(path);
    if (!f.is_open()) return c;

    std::string section;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        // Strip inline comments.
        auto comment = val.find('#');
        if (comment != std::string::npos) val = trim(val.substr(0, comment));

        if (section == "video") {
            if      (key == "video_backend")    c.video_backend   = val;
            else if (key == "builtin_bitrate")  c.builtin_bitrate = parse_int(key, val, c.builtin_bitrate);
            else if (key == "ffmpeg_command")   c.ffmpeg_command  = val;
            else if (key == "video_dir")        c.video_dir       = val;
            else if (key == "stills_dir")       c.stills_dir      = val;
        } else if (section == "camera") {
            if      (key == "camera_index")       c.camera_index       = parse_int  (key, val, c.camera_index);
            else if (key == "fps")                c.fps                = parse_int  (key, val, c.fps);
            else if (key == "initial_ae_enabled") c.initial_ae_enabled = parse_bool (key, val, c.initial_ae_enabled);
            else if (key == "initial_af_enabled") c.initial_af_enabled = parse_bool (key, val, c.initial_af_enabled);
            else if (key == "initial_aperture")   c.initial_aperture   = parse_float(key, val, c.initial_aperture);
            else if (key == "crop_top")            c.crop_top    = parse_int  (key, val, c.crop_top);
            else if (key == "crop_bottom")         c.crop_bottom = parse_int  (key, val, c.crop_bottom);
            else if (key == "crop_left")           c.crop_left   = parse_int  (key, val, c.crop_left);
            else if (key == "crop_right")          c.crop_right  = parse_int  (key, val, c.crop_right);
            else if (key == "focus_scroll_step")   c.focus_scroll_step = parse_float(key, val, c.focus_scroll_step);
            else if (key == "focus_key_step")      c.focus_key_step    = parse_float(key, val, c.focus_key_step);
            else if (key == "capture_format")      c.capture_format    = val;
        } else if (section == "display") {
            if      (key == "fallback_width")   c.fallback_width  = parse_int (key, val, c.fallback_width);
            else if (key == "fallback_height")  c.fallback_height = parse_int (key, val, c.fallback_height);
            else if (key == "show_crosshair")   c.show_crosshair  = parse_bool(key, val, c.show_crosshair);
            else if (key == "viewfinder_ar")    c.viewfinder_ar   = val;
        } else if (section == "remote") {
            if (key == "socket_path") c.socket_path = val;
        } else if (section == "stream") {
            if      (key == "stream_port")    c.stream_port    = parse_int  (key, val, c.stream_port);
            else if (key == "stream_quality") c.stream_quality = parse_int  (key, val, c.stream_quality);
            else if (key == "stream_scale")   c.stream_scale   = parse_float(key, val, c.stream_scale);
            else if (key == "stream_fps")     c.stream_fps     = parse_int  (key, val, c.stream_fps);
            else if (key == "stream_https")   c.stream_https   = parse_bool (key, val, c.stream_https);
            else if (key == "stream_cert")           c.stream_cert           = val;
            else if (key == "stream_key")            c.stream_key            = val;
            else if (key == "download_queue_max")    c.download_queue_max    = parse_int(key, val, c.download_queue_max);
        } else if (section == "timelapse") {
            if      (key == "tl_dir")           c.tl_dir           = val;
            else if (key == "tl_base_ms")       c.tl_base_ms       = (uint64_t)parse_int(key, val, (int)c.tl_base_ms);
            else if (key == "tl_fn")            c.tl_fn            = val;
            else if (key == "tl_rate_constant") c.tl_rate_constant = parse_float(key, val, c.tl_rate_constant);
            else if (key == "tl_power")         c.tl_power         = parse_float(key, val, c.tl_power);
            else if (key == "tl_beta")          c.tl_beta          = parse_float(key, val, c.tl_beta);
            else if (key == "tl_inflection")    c.tl_inflection    = parse_int  (key, val, c.tl_inflection);
            else if (key == "tl_floor_ms")      c.tl_floor_ms      = (uint64_t)parse_int(key, val, (int)c.tl_floor_ms);
            else if (key == "tl_ceil_ms")       c.tl_ceil_ms       = (uint64_t)parse_int(key, val, (int)c.tl_ceil_ms);
            else if (key == "tl_max_frames")    c.tl_max_frames    = parse_int  (key, val, c.tl_max_frames);
            else if (key == "tl_use_rtc")       c.tl_use_rtc       = parse_bool (key, val, c.tl_use_rtc);
        } else if (section == "keys") {
            using KF = std::string KeyMap::*;
            static const std::unordered_map<std::string, KF> key_fields = {
                {"mode_cycle_fwd",  &KeyMap::mode_cycle_fwd},
                {"mode_cycle_back", &KeyMap::mode_cycle_back},
                {"mode_p",          &KeyMap::mode_p},
                {"mode_a",          &KeyMap::mode_a},
                {"mode_s",          &KeyMap::mode_s},
                {"mode_m",          &KeyMap::mode_m},
                {"iso_up",          &KeyMap::iso_up},
                {"iso_down",        &KeyMap::iso_down},
                {"shutter_up",      &KeyMap::shutter_up},
                {"shutter_down",    &KeyMap::shutter_down},
                {"focus_up",        &KeyMap::focus_up},
                {"focus_down",      &KeyMap::focus_down},
                {"aperture_up",     &KeyMap::aperture_up},
                {"aperture_down",   &KeyMap::aperture_down},
                {"toggle_af",       &KeyMap::toggle_af},
                {"still",           &KeyMap::still},
                {"record",          &KeyMap::record},
                {"timelapse",       &KeyMap::timelapse},
                {"crosshair",       &KeyMap::crosshair},
                {"quit",            &KeyMap::quit},
                {"help",            &KeyMap::help},
                {"cam_mode",        &KeyMap::cam_mode},
                {"gallery",         &KeyMap::gallery},
            };
            auto it = key_fields.find(key);
            if (it != key_fields.end())
                c.keys.*(it->second) = val;
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// write_default_config
// ---------------------------------------------------------------------------

void write_default_config(const std::string& path) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[config] could not write default config to " << path << "\n";
        return;
    }
    f << R"(# microscopi.conf — Microscopy camera configuration
# Generated on first run. Edit as needed.
# Boolean values accept: true/false, 1/0, yes/no, on/off
# Bad values are ignored and the built-in default is used instead.

[video]
# Encoding backend: builtin (V4L2 h264_v4l2m2m + MKV muxer)
#                   ffmpeg  (subprocess; uses ffmpeg_command below)
video_backend = builtin

# Bitrate for the builtin backend (bits per second).
builtin_bitrate = 5000000

# FFmpeg command used when video_backend = ffmpeg.
# Substitutions: {width} {height} {fps} {output}
# Raw YUV420 frames are written to ffmpeg's stdin (pipe:0).
ffmpeg_command = ffmpeg -f rawvideo -pix_fmt yuv420p -s {width}x{height} -r {fps} -i pipe:0 -c:v h264_v4l2m2m -b:v 5M -f matroska {output}

# Directories for saved stills and videos (created automatically).
video_dir = /home/microscopi/videos
stills_dir = /home/microscopi/stills

[camera]
# Index of the camera to use (0 = first detected).
camera_index = 0

# Target frame rate.
fps = 30

# Initial autoexposure state at startup.
initial_ae_enabled = true

# Initial autofocus state at startup.
initial_af_enabled = true

# Initial aperture f-number at startup (cosmetic on fixed-aperture lenses).
# 0.0 = leave at camera default and show f/-- in the OSD.
initial_aperture = 0.0

# Crop margins (pixels in camera-frame coordinates).
# Use these to hide the physical mounting frame of the microscope.
# The cropped region is scaled to fill the display.
crop_top    = 0
crop_bottom = 0
crop_left   = 0
crop_right  = 0

# Lens-position delta per mouse scroll notch (range 0.0–1.0).
focus_scroll_step = 0.01

# Lens-position delta per keyboard Up/Down press (range 0.0–1.0).
focus_key_step = 0.05

# Still capture format: jpeg, raw, or jpeg+raw.
# raw saves Bayer data as <name>.raw with a <name>.raw.meta sidecar (width,
# height, pixel format, stride) so the bytes can be decoded by dcraw,
# darktable, rawtherapee, or numpy.  jpeg+raw saves both simultaneously.
capture_format = jpeg

[display]
# Fallback resolution if no supported EDID mode is detected via SDL2.
fallback_width = 1280
fallback_height = 720

# Whether the center guide overlay (circle + crosshair) is shown at startup.
# Toggle at runtime with the 'c' key.
show_crosshair = false

# Aspect ratio used when selecting the viewfinder resolution from the display's
# advertised EDID modes. The best matching mode up to 1920 wide and at least 480
# tall is chosen. Supported values: 16:9 (default), 16:10, 4:3.
viewfinder_ar = 16:9

[keys]
# Key bindings. Use lowercase key names: letters (a-z), digits (0-9),
# or names: up, down, left, right, space, escape, return, tab.
# Prefix with "shift+" for shift-modified keys (e.g. "shift+t").
# "q" always acts as a secondary quit regardless of the quit binding.
mode_cycle_fwd  = c
mode_cycle_back = shift+c
mode_p          = p
mode_a          = a
mode_s          = s
mode_m          = m
iso_up          = i
iso_down        = shift+i
shutter_up      = shift+up
shutter_down    = shift+down
focus_up        = up
focus_down      = down
aperture_up     = right
aperture_down   = left
toggle_af       = f
still           = space
record          = shift+r
timelapse       = t
crosshair       = x
quit            = escape
help            = h
cam_mode        = v
gallery         = g

[remote]
# Unix socket for headless remote control (e.g. from SSH or scripts).
# Connect with: socat - UNIX-CONNECT:/run/microscopi/microscopi.sock
# Set to empty to disable: socket_path =
socket_path = /run/microscopi/microscopi.sock

[stream]
# HTTP port for the MJPEG live stream and web UI (http://<pi-ip>:8080/).
# Open in any browser or with: mpv http://<pi-ip>:8080/stream
stream_port = 8080

# JPEG quality for the MJPEG stream (1–100).
# Lower values reduce CPU load and network bandwidth. 75 is a good default.
stream_quality = 75

# Linear scale factor applied before JPEG encoding (0.5 = half resolution).
# At 1080p input, 0.5 gives 960×540 — roughly 30 % of full-res CPU cost.
# 1.0 encodes at full camera resolution.
stream_scale = 0.5

# Maximum frames per second pushed to MJPEG clients.
# Actual rate is also limited by camera fps and encoder speed.
# Set to 0 for unlimited (encode every viewfinder frame).
stream_fps = 15

# Enable HTTPS (TLS). Default is off — plain HTTP is fine for local LAN use.
# When enabled, stream_cert and stream_key must point to valid PEM files.
# Generate a self-signed cert with:
#   openssl req -x509 -newkey rsa:2048 -keyout /etc/microscopi/server.key \
#     -out /etc/microscopi/server.crt -days 3650 -nodes -subj '/CN=microscopi'
stream_https = false
stream_cert  =
stream_key   =

# Maximum number of simultaneous file downloads served to web clients.
# Requests over this limit receive HTTP 429. Default 2 is safe on a Pi 3.
download_queue_max = 2

[timelapse]
# Root directory for timelapse sessions. Each run creates a subdirectory named
# YYYYMMDD_HHMMSS--YYYYMMDD_HHMMSS (start time to end time). On crash the
# end-time suffix is absent. Frames are frame_000001.jpg, frame_000002.jpg, ...
# Post-process with: ffmpeg -r 24 -i 'frame_%06d.jpg' -c:v libx264 -crf 18 out.mp4
tl_dir = /home/microscopi/timelapses

# B — base offset in milliseconds. This is the interval before the first capture
# and the starting point for all non-linear functions. I(0) == tl_base_ms.
tl_base_ms = 5000

# Interval schedule function.
# linear        — constant interval (tl_base_ms forever; tl_rate_constant ignored)
# exp_grow      — first-order growth: I(n) = B·exp(k·n)        [ceil required]
# exp_decay     — first-order decay:  I(n) = B·exp(-k·n)       [floor required]
# log           — sublinear growth:   I(n) = B + k·ln(n+1)     [ceil required]
# power         — power-law:          I(n) = B + k·n^p          [ceil required]
# quadratic     — power alias, p=2
# cubic         — power alias, p=3
# quintic       — power alias, p=5
# michaelis     — MM saturation:      I(n) = B+(C-B)·kn/(1+kn) [soft ceil]
# logistic      — sigmoid / Hill:     I(n) = B+(C-B)·σ(k(n-m)) [soft floor+ceil]
# stretched_exp — KWW dispersive:     I(n) = B·exp(k·n^β)      [ceil required]
# hyperbolic    — 2nd-order decay:    I(n) = B/(1+k·n)          [floor required]
tl_fn = linear

# k — rate constant. Units and safe ranges depend on tl_fn:
#   exp_grow/exp_decay  nepers/frame  (0.01–0.3; k=0.693 doubles interval every frame)
#   log/power           ms/frame^p    (> 0)
#   michaelis/hyperbolic 1/frame      (0.01–1.0; k=0.1 → midpoint at n=10)
#   logistic            1/frame       (steepness at inflection; 0.05–0.5)
#   stretched_exp       nepers/frame^β
# Ignored by fn=linear.
tl_rate_constant = 0.05

# p — power exponent for fn=power (real-valued, e.g. 0.5, 1.5, 2, 3, 5).
# Overridden to 2/3/5 when fn=quadratic/cubic/quintic.
tl_power = 2.0

# β — stretch exponent for fn=stretched_exp. Range (0, 1]. β=1 is pure exponential.
# Typical biological value: 0.5–0.8.
tl_beta = 0.7

# m — frame number at the sigmoid midpoint for fn=logistic.
# At this frame, I(m) = (tl_base_ms + tl_ceil_ms) / 2.
tl_inflection = 50

# Hard floor: interval is never shorter than this (ms). Critical for decay modes.
# Must be >= camera still capture time (~2000 ms on Pi 3 single-stream).
tl_floor_ms = 2000

# Hard ceiling: interval is never longer than this (ms). Critical for growth modes.
# Default: 300000 (5 minutes).
tl_ceil_ms = 300000

# Maximum frames per session. 0 = run until manually stopped.
tl_max_frames = 0

# Whether to use wall-clock time for folder and file names.
# true  (default) — requires RTC or NTP sync at boot.
#   Folders: YYYYMMDD_HHMMSS  while running, renamed YYYYMMDD_HHMMSS--YYYYMMDD_HHMMSS on stop.
#   Files:   frame_000001.jpg, frame_000002.jpg ... (sequential; compatible with ffmpeg -i 'frame_%06d.jpg')
# false — no wall clock needed; uses monotonic elapsed time.
#   Folders: tl_{start_mono_ms}  while running, renamed to duration on stop (e.g. 01h23m45s678ms).
#   Files:   t{elapsed_ms:010d}.jpg  (e.g. t0000005123.jpg — sortable by capture time)
# Set to false on Pi units without an RTC and no network access on boot.
tl_use_rtc = true
)";
    std::cerr << "[config] wrote default config to " << path << "\n";
}
