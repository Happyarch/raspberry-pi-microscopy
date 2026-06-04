#pragma once
#include <string>

struct Config {
    // [video]
    std::string video_backend;      // "builtin" or "ffmpeg"
    int         builtin_bitrate;
    std::string ffmpeg_command;
    std::string video_dir;
    std::string stills_dir;

    // [camera]
    int camera_index;
    int fps;

    // [display]
    int  fallback_width;
    int  fallback_height;
    bool show_crosshair;  // initial crosshair/guide overlay state
};

// Load from path (falls back to defaults if file missing).
Config load_config(const std::string& path);
