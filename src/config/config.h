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
    int   camera_index;
    int   fps;
    bool  initial_ae_enabled;   // autoexposure on at startup
    bool  initial_af_enabled;   // autofocus on at startup
    float initial_aperture;     // f-number to set at startup; 0.0 = use camera default

    // [display]
    int  fallback_width;
    int  fallback_height;
    bool show_crosshair;        // guide overlay visible at startup
};

// Load config from path. Missing keys fall back to built-in defaults.
// Malformed values are ignored (default kept) and logged to stderr.
// Returns defaults if path is empty or file cannot be opened.
Config load_config(const std::string& path);

// Write a fully-commented default config file to path.
// Creates parent directories as needed. Used on first run.
void write_default_config(const std::string& path);
