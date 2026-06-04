#include "osd.h"
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Osd::Osd(SDL_Renderer* renderer,
         int display_w, int display_h,
         const std::string& icons_dir,
         const std::string& font_path,
         const std::string& cache_dir)
    : renderer_(renderer)
    , dw_(display_w), dh_(display_h)
    , icons_dir_(icons_dir)
    , font_path_(font_path)
{
    // All sizes are proportional to display height so the OSD looks the same
    // relative to the image regardless of resolution.
    bar_h_   = std::max(28, dh_ / 14);
    icon_sz_ = std::max(14, (int)(bar_h_ * 0.52f));
    font_sz_ = std::max( 9, (int)(bar_h_ * 0.38f));
    pad_     = std::max( 6, (int)(bar_h_ * 0.16f));

    // Cache directory is per display height so different resolutions get their
    // own correctly-sized PNGs.
    cache_dir_ = cache_dir + "/" + std::to_string(dh_);
    ensure_cache_dir();

    TTF_Init();
    font_ = TTF_OpenFont(font_path_.c_str(), font_sz_);

    // Pre-load all icons used in the OSD.
    for (const auto& name : {"aperture", "camera", "crosshair", "circle-dot", "sun"})
        icons_[name] = load_icon(name);
}

Osd::~Osd() {
    if (font_) TTF_CloseFont(font_);
    for (auto& [k, v] : icons_) if (v) SDL_DestroyTexture(v);
    TTF_Quit();
}

// ---------------------------------------------------------------------------
// SVG → PNG rendering with disk cache
// ---------------------------------------------------------------------------

void Osd::ensure_cache_dir() const {
    fs::create_directories(cache_dir_);
}

SDL_Texture* Osd::load_icon(const std::string& name) {
    std::string png_path = cache_dir_ + "/" + name + ".png";
    std::string svg_path = icons_dir_ + "/" + name + ".svg";

    // Render SVG → PNG if the cached file doesn't exist yet.
    if (!fs::exists(png_path)) {
        std::string cmd = "rsvg-convert"
                          " -w " + std::to_string(icon_sz_) +
                          " -h " + std::to_string(icon_sz_) +
                          " \"" + svg_path + "\""
                          " -o \"" + png_path + "\""
                          " 2>/dev/null";
        if (std::system(cmd.c_str()) != 0) return nullptr;
    }

    SDL_Surface* sur = IMG_Load(png_path.c_str());
    if (!sur) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, sur);
    SDL_FreeSurface(sur);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void Osd::draw_text(const std::string& text, int x, int y,
                    SDL_Color color, bool /*bold*/) {
    if (!font_ || text.empty()) return;
    SDL_Surface* sur = TTF_RenderUTF8_Blended(font_, text.c_str(), color);
    if (!sur) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, sur);
    SDL_Rect dst{x, y, sur->w, sur->h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(sur);
}

void Osd::draw_icon(SDL_Texture* tex, int x, int y, uint8_t alpha) {
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_Rect dst{x, y, icon_sz_, icon_sz_};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

void Osd::draw_record_arc(int cx, int cy, int radius, float progress) {
    if (progress <= 0.0f) return;
    SDL_SetRenderDrawColor(renderer_, 255, 60, 60, 255);
    int segments = (int)(progress * 60);
    float start  = -(float)M_PI_2;
    float end    = start + progress * 2.0f * (float)M_PI;
    float px     = cx + radius * cosf(start);
    float py     = cy + radius * sinf(start);
    for (int i = 1; i <= segments; ++i) {
        float a  = start + (end - start) * (float)i / segments;
        float nx = cx + radius * cosf(a);
        float ny = cy + radius * sinf(a);
        SDL_RenderDrawLine(renderer_, (int)px, (int)py, (int)nx, (int)ny);
        px = nx; py = ny;
    }
}

// ---------------------------------------------------------------------------
// Main draw — called every frame
// ---------------------------------------------------------------------------

void Osd::draw(const OsdState& state) {
    const SDL_Color kWhite = {255, 255, 255, 255};
    const SDL_Color kDim   = {150, 150, 150, 200};
    const SDL_Color kRed   = {220,  40,  40, 255};

    int bar_y   = dh_ - bar_h_;
    int icon_y  = bar_y + (bar_h_ - icon_sz_) / 2;
    int text_y  = bar_y + (bar_h_ - font_sz_ - 2) / 2;

    // Background bar
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
    SDL_Rect bar{0, bar_y, dw_, bar_h_};
    SDL_RenderFillRect(renderer_, &bar);

    auto text_w = [&](const std::string& s) {
        int w = 0, h = 0;
        if (font_) TTF_SizeUTF8(font_, s.c_str(), &w, &h);
        return w;
    };

    int cx = pad_;

    // ---- Aperture ----
    draw_icon(icons_["aperture"], cx, icon_y);
    cx += icon_sz_ + 4;
    std::string ap_str;
    if (state.aperture > 0.0f) {
        std::ostringstream ss;
        ss << "f/" << std::fixed << std::setprecision(1) << state.aperture;
        ap_str = ss.str();
    } else {
        ap_str = "f/--";
    }
    draw_text(ap_str, cx, text_y, kWhite);
    cx += text_w(ap_str) + pad_ * 2;

    // ---- Exposure ----
    {
        std::string exp_str;
        if (state.exposure_us > 0.0f) {
            float s = state.exposure_us / 1e6f;
            if (s < 0.5f) {
                int denom = (int)(1.0f / s + 0.5f);
                exp_str = "1/" + std::to_string(denom);
            } else {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(1) << s << "\"";
                exp_str = ss.str();
            }
        } else {
            exp_str = "---";
        }
        draw_icon(icons_["sun"], cx, icon_y, state.ae_enabled ? 255 : 100);
        cx += icon_sz_ + 4;
        draw_text(exp_str, cx, text_y, state.ae_enabled ? kWhite : kDim);
        cx += text_w(exp_str) + pad_ * 2;
    }

    // ---- Focus ----
    {
        bool af = state.af_enabled || std::isnan(state.lens_position);
        std::string focus_str;
        if (af) {
            focus_str = "AF";
        } else {
            std::ostringstream ss;
            ss << "MF " << std::fixed << std::setprecision(2) << state.lens_position;
            focus_str = ss.str();
        }
        draw_icon(icons_["crosshair"], cx, icon_y, af ? 255 : 100);
        cx += icon_sz_ + 4;
        draw_text(focus_str, cx, text_y, af ? kWhite : kDim);
        cx += text_w(focus_str) + pad_ * 2;
    }

    // ---- Stills count ----
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%03d", state.still_count);
        draw_icon(icons_["camera"], cx, icon_y);
        cx += icon_sz_ + 4;
        draw_text(buf, cx, text_y, kWhite);
    }

    // ---- Recording indicator (top-left corner) ----
    if (state.recording) {
        uint64_t ms    = SDL_GetTicks64() - state.record_start_ms;
        uint64_t total_s = ms / 1000;
        // Arc-seconds: 1/60 of a second. Divides evenly at 30fps and 60fps
        // using only integer arithmetic (no floating-point rounding).
        uint64_t arc_s = (ms % 1000) * 60 / 1000;  // 0–59
        uint64_t s     = total_s % 60;
        uint64_t m     = (total_s / 60) % 60;
        uint64_t h     = total_s / 3600;

        char ts[32];
        snprintf(ts, sizeof(ts), "%02llu:%02llu:%02llu'%02llu",
                 (unsigned long long)h,  (unsigned long long)m,
                 (unsigned long long)s,  (unsigned long long)arc_s);

        int dot_r  = bar_h_ / 4;
        int dot_cx = pad_ + dot_r;
        int dot_cy = pad_ + dot_r;

        bool visible = (SDL_GetTicks64() / 500) % 2 == 0;
        if (visible) {
            SDL_SetRenderDrawColor(renderer_, 210, 30, 30, 255);
            for (int dy = -dot_r; dy <= dot_r; ++dy)
                for (int dx = -dot_r; dx <= dot_r; ++dx)
                    if (dx * dx + dy * dy <= dot_r * dot_r)
                        SDL_RenderDrawPoint(renderer_, dot_cx + dx, dot_cy + dy);
        }
        draw_text(std::string("REC ") + ts,
                  dot_cx + dot_r + pad_, dot_cy - font_sz_ / 2, kRed);
    }

    // ---- Shift+R hold progress arc ----
    if (state.record_hold_progress > 0.0f) {
        int dot_r  = bar_h_ / 4;
        int dot_cx = pad_ + dot_r;
        int dot_cy = pad_ + dot_r;
        draw_record_arc(dot_cx, dot_cy, dot_r + 4, state.record_hold_progress);
    }
}
