#include "config.h"
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

static Config defaults() {
    Config c;
    c.video_backend   = "builtin";
    c.builtin_bitrate = 5000000;
    c.ffmpeg_command  = "ffmpeg -f rawvideo -pix_fmt yuv420p -s {width}x{height} -r {fps} -i pipe:0 -c:v h264_v4l2m2m -b:v 5M -f matroska {output}";
    c.video_dir       = "/home/pi/videos";
    c.stills_dir      = "/home/pi/stills";
    c.camera_index    = 0;
    c.fps             = 30;
    c.fallback_width  = 1280;
    c.fallback_height = 720;
    return c;
}

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Config load_config(const std::string& path) {
    Config c = defaults();
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

        if (section == "video") {
            if (key == "video_backend")   c.video_backend   = val;
            else if (key == "builtin_bitrate") c.builtin_bitrate = std::stoi(val);
            else if (key == "ffmpeg_command")  c.ffmpeg_command  = val;
            else if (key == "video_dir")       c.video_dir       = val;
            else if (key == "stills_dir")      c.stills_dir      = val;
        } else if (section == "camera") {
            if (key == "camera_index") c.camera_index = std::stoi(val);
            else if (key == "fps")     c.fps          = std::stoi(val);
        } else if (section == "display") {
            if (key == "fallback_width")  c.fallback_width  = std::stoi(val);
            else if (key == "fallback_height") c.fallback_height = std::stoi(val);
        }
    }
    return c;
}
