#include "config/config.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// Write text to a temp file and return its path.
static std::string write_tmp(const std::string& content) {
    auto p = fs::temp_directory_path() / "microscopi_test_config.conf";
    std::ofstream f(p);
    f << content;
    return p.string();
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

TEST(ConfigDefaults, EmptyPath) {
    Config c = load_config("");
    EXPECT_EQ(c.video_backend,   "builtin");
    EXPECT_EQ(c.builtin_bitrate, 5000000);
    EXPECT_EQ(c.camera_index,    0);
    EXPECT_EQ(c.fps,             30);
    EXPECT_TRUE(c.initial_ae_enabled);
    EXPECT_TRUE(c.initial_af_enabled);
    EXPECT_FLOAT_EQ(c.initial_aperture, 0.0f);
    EXPECT_EQ(c.fallback_width,  1280);
    EXPECT_EQ(c.fallback_height, 720);
    EXPECT_FALSE(c.show_crosshair);
}

TEST(ConfigDefaults, DefaultKeyMap) {
    Config c = load_config("");
    EXPECT_EQ(c.keys.mode_cycle_fwd,  "t");
    EXPECT_EQ(c.keys.mode_cycle_back, "shift+t");
    EXPECT_EQ(c.keys.focus_up,        "up");
    EXPECT_EQ(c.keys.focus_down,      "down");
    EXPECT_EQ(c.keys.quit,            "escape");
    EXPECT_EQ(c.keys.help,            "h");
}

// ---------------------------------------------------------------------------
// Valid value parsing
// ---------------------------------------------------------------------------

TEST(ConfigParsing, VideoSection) {
    auto path = write_tmp(
        "[video]\n"
        "video_backend = ffmpeg\n"
        "builtin_bitrate = 8000000\n"
        "video_dir = /tmp/videos\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.video_backend,   "ffmpeg");
    EXPECT_EQ(c.builtin_bitrate, 8000000);
    EXPECT_EQ(c.video_dir,       "/tmp/videos");
}

TEST(ConfigParsing, CameraSection) {
    auto path = write_tmp(
        "[camera]\n"
        "camera_index = 1\n"
        "fps = 60\n"
        "initial_ae_enabled = false\n"
        "initial_af_enabled = no\n"
        "initial_aperture = 2.8\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.camera_index,  1);
    EXPECT_EQ(c.fps,           60);
    EXPECT_FALSE(c.initial_ae_enabled);
    EXPECT_FALSE(c.initial_af_enabled);
    EXPECT_FLOAT_EQ(c.initial_aperture, 2.8f);
}

TEST(ConfigParsing, DisplaySection) {
    auto path = write_tmp(
        "[display]\n"
        "fallback_width = 1920\n"
        "fallback_height = 1080\n"
        "show_crosshair = true\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.fallback_width,  1920);
    EXPECT_EQ(c.fallback_height, 1080);
    EXPECT_TRUE(c.show_crosshair);
}

TEST(ConfigParsing, KeysSection) {
    auto path = write_tmp(
        "[keys]\n"
        "mode_cycle_fwd = g\n"
        "focus_up = w\n"
        "focus_down = s\n"
        "quit = q\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.keys.mode_cycle_fwd, "g");
    EXPECT_EQ(c.keys.focus_up,       "w");
    EXPECT_EQ(c.keys.focus_down,     "s");
    EXPECT_EQ(c.keys.quit,           "q");
    // Unspecified keys keep defaults.
    EXPECT_EQ(c.keys.mode_cycle_back, "shift+t");
}

// ---------------------------------------------------------------------------
// Comment and whitespace handling
// ---------------------------------------------------------------------------

TEST(ConfigParsing, InlineCommentStripped) {
    auto path = write_tmp(
        "[video]\n"
        "video_backend = ffmpeg # use subprocess\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.video_backend, "ffmpeg");
}

TEST(ConfigParsing, FullLineCommentIgnored) {
    auto path = write_tmp(
        "# top-level comment\n"
        "[camera]\n"
        "; semicolon comment\n"
        "fps = 60\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.fps, 60);
}

TEST(ConfigParsing, WhitespaceTrimmed) {
    auto path = write_tmp(
        "[camera]\n"
        "  fps  =  60  \n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.fps, 60);
}

// ---------------------------------------------------------------------------
// Malformed value fallback
// ---------------------------------------------------------------------------

TEST(ConfigParsing, BadIntKeepsDefault) {
    auto path = write_tmp(
        "[camera]\n"
        "fps = notanumber\n"
    );
    Config c = load_config(path);
    EXPECT_EQ(c.fps, 30); // default
}

TEST(ConfigParsing, BadFloatKeepsDefault) {
    auto path = write_tmp(
        "[camera]\n"
        "initial_aperture = abc\n"
    );
    Config c = load_config(path);
    EXPECT_FLOAT_EQ(c.initial_aperture, 0.0f);
}

TEST(ConfigParsing, BadBoolKeepsDefault) {
    auto path = write_tmp(
        "[camera]\n"
        "initial_ae_enabled = maybe\n"
    );
    Config c = load_config(path);
    EXPECT_TRUE(c.initial_ae_enabled); // default
}

// ---------------------------------------------------------------------------
// write_default_config round-trip
// ---------------------------------------------------------------------------

TEST(ConfigRoundTrip, WriteThenLoad) {
    auto p = fs::temp_directory_path() / "microscopi_roundtrip.conf";
    write_default_config(p.string());
    ASSERT_TRUE(fs::exists(p));

    Config c = load_config(p.string());
    EXPECT_EQ(c.video_backend,   "builtin");
    EXPECT_EQ(c.fps,             30);
    EXPECT_EQ(c.fallback_width,  1280);
    EXPECT_EQ(c.keys.focus_up,   "up");
    EXPECT_EQ(c.keys.quit,       "escape");

    fs::remove(p);
}
