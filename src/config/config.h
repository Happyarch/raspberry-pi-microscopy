#pragma once
#include <cstdint>
#include <string>

struct KeyMap {
    std::string mode_cycle_fwd  = "t";
    std::string mode_cycle_back = "shift+t";
    std::string timelapse       = "l";   // hold ≥ 500 ms to start/stop
    std::string mode_p          = "p";
    std::string mode_a          = "a";
    std::string mode_s          = "s";
    std::string mode_m          = "m";
    std::string iso_up          = "i";
    std::string iso_down        = "shift+i";
    std::string shutter_up      = "shift+up";
    std::string shutter_down    = "shift+down";
    std::string focus_up        = "up";
    std::string focus_down      = "down";
    std::string aperture_up     = "right";
    std::string aperture_down   = "left";
    std::string toggle_af       = "shift+a";
    std::string still           = "space";
    std::string record          = "shift+r";
    std::string crosshair       = "c";
    std::string quit            = "escape";  // q always acts as a secondary quit
    std::string help            = "h";       // hold 3 s to show key binding overlay
    std::string cam_mode        = "v";       // open/close camera mode list
};

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
    int   crop_top{0};          // pixels to trim from camera frame edges before display
    int   crop_bottom{0};
    int   crop_left{0};
    int   crop_right{0};
    float       focus_scroll_step{0.01f}; // lens-position delta (0–1) per mouse scroll notch
    float       focus_key_step{0.05f};   // lens-position delta per keyboard Up/Down press
    std::string capture_format{"jpeg"};  // "jpeg", "raw", or "jpeg+raw"

    // [display]
    int  fallback_width;
    int  fallback_height;
    bool show_crosshair;        // guide overlay visible at startup

    // [remote]
    std::string socket_path{"/run/microscopi/microscopi.sock"};

    // [stream]
    int         stream_port{8080};
    int         stream_quality{75};   // JPEG quality 1–100
    float       stream_scale{0.5f};   // linear scale factor (0.5 = half-res)
    int         stream_fps{15};       // max frames/s pushed to MJPEG clients
    bool        stream_https{false};  // serve HTTPS instead of HTTP
    std::string stream_cert{};        // path to PEM certificate (required when stream_https = true)
    std::string stream_key{};         // path to PEM private key  (required when stream_https = true)

    // [timelapse]
    std::string tl_dir           {"/home/microscopi/timelapses"};
    uint64_t    tl_base_ms       {5000};    // B — interval at n=0 for all functions
    std::string tl_fn            {"linear"};// interval schedule function name
    float       tl_rate_constant {0.05f};   // k — rate constant; ignored by linear
    float       tl_power         {2.0f};    // p — exponent for fn=power/quadratic/cubic/quintic
    float       tl_beta          {0.7f};    // β — stretch exponent for fn=stretched_exp
    int         tl_inflection    {50};      // frame at sigmoid midpoint for fn=logistic
    uint64_t    tl_floor_ms      {2000};    // hard minimum interval
    uint64_t    tl_ceil_ms       {300000};  // hard maximum interval (5 min)
    int         tl_max_frames    {0};       // 0 = unlimited
    bool        tl_use_rtc       {true};    // false = elapsed-time folder/file naming (no RTC needed)

    // [keys]
    KeyMap keys;
};

// Load config from path. Missing keys fall back to built-in defaults.
// Malformed values are ignored (default kept) and logged to stderr.
// Returns defaults if path is empty or file cannot be opened.
Config load_config(const std::string& path);

// Write a fully-commented default config file to path.
// Creates parent directories as needed. Used on first run.
void write_default_config(const std::string& path);
