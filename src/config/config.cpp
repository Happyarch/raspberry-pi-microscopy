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
        } else if (section == "display") {
            if      (key == "fallback_width")   c.fallback_width  = parse_int (key, val, c.fallback_width);
            else if (key == "fallback_height")  c.fallback_height = parse_int (key, val, c.fallback_height);
            else if (key == "show_crosshair")   c.show_crosshair  = parse_bool(key, val, c.show_crosshair);
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
                {"crosshair",       &KeyMap::crosshair},
                {"quit",            &KeyMap::quit},
                {"help",            &KeyMap::help},
                {"cam_mode",        &KeyMap::cam_mode},
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
# Smaller values give finer focus control; keyboard steps are 0.05.
focus_scroll_step = 0.01

[display]
# Fallback resolution if no supported EDID mode is detected via SDL2.
fallback_width = 1280
fallback_height = 720

# Whether the center guide overlay (circle + crosshair) is shown at startup.
# Toggle at runtime with the 'c' key.
show_crosshair = false

[keys]
# Key bindings. Use lowercase key names: letters (a-z), digits (0-9),
# or names: up, down, left, right, space, escape, return, tab.
# Prefix with "shift+" for shift-modified keys (e.g. "shift+t").
# "q" always acts as a secondary quit regardless of the quit binding.
mode_cycle_fwd  = t
mode_cycle_back = shift+t
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
toggle_af       = shift+a
still           = space
record          = shift+r
crosshair       = c
quit            = escape
help            = h
cam_mode        = v
)";
    std::cerr << "[config] wrote default config to " << path << "\n";
}
