#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <cstdint>

struct OsdState {
    float    aperture;            // f-number; 0 = unknown/fixed
    float    exposure_us;         // microseconds
    float    lens_position;       // 0–1; NaN = AF active
    bool     ae_enabled;
    bool     af_enabled;
    int      still_count;
    bool     recording;
    uint64_t record_start_ms;     // SDL_GetTicks64() when recording began
    float    record_hold_progress;// 0–1 while Shift+R held before trigger
    bool     show_crosshair;      // whether to draw the center guide overlay
};

class Osd {
public:
    // icons_dir: directory containing aperture.svg, camera.svg, crosshair.svg,
    //            circle-dot.svg, sun.svg.
    // font_path: path to RobotoCondensed-Regular.ttf.
    // cache_dir: base dir for rendered PNG cache; subdirs named by display height.
    Osd(SDL_Renderer* renderer,
        int display_w, int display_h,
        const std::string& icons_dir,
        const std::string& font_path,
        const std::string& cache_dir);
    ~Osd();

    void draw(const OsdState& state);

private:
    // Render an SVG icon at icon_sz_ × icon_sz_, caching the PNG to disk.
    SDL_Texture* load_icon(const std::string& name);

    void draw_text(const std::string& text, int x, int y,
                   SDL_Color color, bool bold = false);
    void draw_icon(SDL_Texture* tex, int x, int y, uint8_t alpha = 255);
    void draw_record_arc(int cx, int cy, int radius, float progress);
    void draw_crosshair();
    void ensure_cache_dir() const;

    SDL_Renderer* renderer_;
    int dw_, dh_;

    std::string icons_dir_;
    std::string font_path_;
    std::string cache_dir_; // e.g. /home/pi/.cache/microscopy/720

    // Sizes computed from display height at construction.
    int bar_h_;   // height of bottom OSD bar
    int icon_sz_; // icon width and height in pixels
    int font_sz_; // font size in points
    int pad_;     // horizontal padding between elements

    TTF_Font* font_{nullptr};

    // Icon texture cache keyed by name (e.g. "aperture").
    std::unordered_map<std::string, SDL_Texture*> icons_;
};
