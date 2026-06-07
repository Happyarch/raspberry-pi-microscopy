#include "osd.h"
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <cmath>
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
         const std::string& cache_dir,
         const KeyMap& keys)
    : renderer_(renderer)
    , dw_(display_w), dh_(display_h)
    , icons_dir_(icons_dir)
    , font_path_(font_path)
    , keys_(keys)
{
    // All sizes are proportional to display height so the OSD looks the same
    // relative to the image regardless of resolution.
    bar_h_   = std::max(28, dh_ / 14);
    icon_sz_ = std::max(14, (int)(bar_h_ * 0.52f));
    font_sz_ = std::max( 9, (int)(bar_h_ * 0.38f));
    warn_sz_ = std::max(18, dh_ / 20);
    pad_     = std::max( 6, (int)(bar_h_ * 0.16f));

    // Cache directory is per display height so different resolutions get their
    // own correctly-sized PNGs.
    cache_dir_ = cache_dir + "/" + std::to_string(dh_);
    ensure_cache_dir();

    TTF_Init();
    font_      = TTF_OpenFont(font_path_.c_str(), font_sz_);
    warn_font_ = TTF_OpenFont(font_path_.c_str(), warn_sz_);

    // Pre-load all icons used in the OSD.
    for (const auto& name : {"aperture", "camera", "crosshair", "circle-dot", "sun"})
        icons_[name] = load_icon(name);
}

Osd::~Osd() {
    if (warn_font_) TTF_CloseFont(warn_font_);
    if (font_)      TTF_CloseFont(font_);
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
                    SDL_Color color, TTF_Font* font) {
    TTF_Font* f = font ? font : font_;
    if (!f || text.empty()) return;
    SDL_Surface* sur = TTF_RenderUTF8_Blended(f, text.c_str(), color);
    if (!sur) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, sur);
    SDL_Rect dst{x, y, sur->w, sur->h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(sur);
}

void Osd::draw_text_outlined(const std::string& text, int x, int y,
                              SDL_Color fg, SDL_Color outline, TTF_Font* font) {
    TTF_Font* f = font ? font : font_;
    if (!f || text.empty()) return;
    // Render outline by drawing in the outline color at 8 cardinal offsets.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            SDL_Surface* s = TTF_RenderUTF8_Blended(f, text.c_str(), outline);
            if (!s) continue;
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
            SDL_Rect dst{x + dx, y + dy, s->w, s->h};
            SDL_RenderCopy(renderer_, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_FreeSurface(s);
        }
    }
    // Foreground on top.
    draw_text(text, x, y, fg, f);
}

void Osd::draw_help_overlay() {
    struct Row { const char* label; const std::string& key; };
    const std::string mode_direct =
        keys_.mode_p + "  " + keys_.mode_a + "  " +
        keys_.mode_s + "  " + keys_.mode_m + "  (direct)";
    const std::string mode_cycle =
        keys_.mode_cycle_fwd + " / " + keys_.mode_cycle_back + "  (cycle)";
    const std::string shutter_str  = keys_.shutter_up  + " / " + keys_.shutter_down;
    const std::string focus_str    = keys_.focus_up    + " / " + keys_.focus_down;
    const std::string aperture_str = keys_.aperture_up + " / " + keys_.aperture_down;
    const std::string iso_str      = keys_.iso_up      + " / " + keys_.iso_down;
    const std::string record_str   = keys_.record      + "  (hold)";
    const std::string quit_str     = keys_.quit        + " or q  (hold 5 s)";
    const std::string help_str     = keys_.help        + "  (hold 3 s)";

    const Row rows[] = {
        {"Mode",        mode_direct},
        {"",            mode_cycle},
        {"Shutter ±",   shutter_str},
        {"Focus ±",     focus_str},
        {"Aperture ±",  aperture_str},
        {"ISO ±",       iso_str},
        {"Autofocus",   keys_.toggle_af},
        {"Still",       keys_.still},
        {"Record",      record_str},
        {"Crosshair",   keys_.crosshair},
        {"Help",        help_str},
        {"Quit",        quit_str},
    };
    const int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));

    const int row_h   = font_sz_ + pad_;
    const int title_h = warn_sz_ + pad_ * 2;
    const int panel_h = title_h + n_rows * row_h + pad_ * 2;
    const int col_gap = font_sz_ * 6;
    const int panel_w = dw_ / 2;
    const int px      = (dw_ - panel_w) / 2;
    const int py      = (dh_ - panel_h) / 2;

    // Background panel
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 210);
    SDL_Rect panel{px, py, panel_w, panel_h};
    SDL_RenderFillRect(renderer_, &panel);

    // Title
    const SDL_Color kWhite = {255, 255, 255, 255};
    const SDL_Color kDim   = {170, 170, 170, 255};
    int tw = 0, th = 0;
    if (warn_font_) TTF_SizeUTF8(warn_font_, "Key Bindings", &tw, &th);
    draw_text("Key Bindings", px + (panel_w - tw) / 2, py + pad_, kWhite, warn_font_);

    // Rows: label on left, binding on right
    int ry = py + title_h;
    for (int i = 0; i < n_rows; ++i, ry += row_h) {
        if (rows[i].label[0] != '\0')
            draw_text(rows[i].label, px + pad_, ry, kDim);
        draw_text(rows[i].key, px + pad_ + col_gap, ry, kWhite);
    }
}

void Osd::draw_quit_warning(float progress) {
    if (progress <= 0.5f) return;

    const std::string msg = "Keep holding to quit";
    int tw = 0, th = 0;
    if (warn_font_) TTF_SizeUTF8(warn_font_, msg.c_str(), &tw, &th);

    int x = (dw_ - tw) / 2;
    int y = (dh_ - th) / 2;

    SDL_Color fg      = {220, 60, 60, 255};
    SDL_Color outline = {0, 0, 0, 220};
    draw_text_outlined(msg, x, y, fg, outline, warn_font_);
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

void Osd::draw_crosshair() {
    // Center of the viewfinder area (above the OSD bar).
    int cx = dw_ / 2;
    int cy = (dh_ - bar_h_) / 2;

    // Circle radius: 1/8 of the shorter display dimension.
    int radius = std::min(dw_, dh_ - bar_h_) / 8;

    // Short tick marks at the four cardinal points of the circle
    // and a center dot — drawn with alpha so they don't dominate.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 160);

    // Circle approximated with line segments (64 segments → smooth enough).
    constexpr int kSegs = 64;
    float prev_x = cx + radius;
    float prev_y = (float)cy;
    for (int i = 1; i <= kSegs; ++i) {
        float a  = (float)i / kSegs * 2.0f * (float)M_PI;
        float nx = cx + radius * cosf(a);
        float ny = cy + radius * sinf(a);
        SDL_RenderDrawLine(renderer_, (int)prev_x, (int)prev_y, (int)nx, (int)ny);
        prev_x = nx; prev_y = ny;
    }

    // Short crosshair lines through the center.
    int tick = radius / 4;
    SDL_RenderDrawLine(renderer_, cx - tick, cy,        cx + tick, cy);
    SDL_RenderDrawLine(renderer_, cx,        cy - tick, cx,        cy + tick);
}

// ---------------------------------------------------------------------------
// Main draw — called every frame
// ---------------------------------------------------------------------------

static const char* mode_label(int m) {
    switch (m) {
    case 0: return "P";
    case 1: return "A";
    case 2: return "S";
    case 3: return "M";
    default: return "?";
    }
}

void Osd::draw(const OsdState& state) {
    const SDL_Color kWhite = {255, 255, 255, 255};
    const SDL_Color kDim   = {150, 150, 150, 200};
    const SDL_Color kRed   = {220,  40,  40, 255};

    // Guide overlay drawn first so the bottom bar sits on top of it.
    if (state.show_crosshair) draw_crosshair();

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

    // ---- Mode ----
    {
        std::string mode_str = mode_label(state.exposure_mode);
        // In P and A modes the camera drives exposure; dim the sun icon.
        bool ae_auto = (state.exposure_mode == 0 || state.exposure_mode == 1);
        draw_icon(icons_["sun"], cx, icon_y, ae_auto ? 255 : 100);
        cx += icon_sz_ + 4;
        draw_text(mode_str, cx, text_y, kWhite);
        cx += text_w(mode_str) + pad_;
    }

    // ---- Shutter speed ----
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
        bool shutter_manual = (state.exposure_mode == 2 || state.exposure_mode == 3);
        draw_text(exp_str, cx, text_y, shutter_manual ? kWhite : kDim);
        cx += text_w(exp_str) + pad_ * 2;
    }

    // ---- ISO ----
    {
        std::string iso_str = "ISO ";
        iso_str += (state.iso == 0) ? "AUTO" : std::to_string(state.iso);
        bool iso_manual = (state.iso != 0);
        draw_text(iso_str, cx, text_y, iso_manual ? kWhite : kDim);
        cx += text_w(iso_str) + pad_ * 2;
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

    // ---- Record hold progress arc ----
    if (state.record_hold_progress > 0.0f) {
        int dot_r  = bar_h_ / 4;
        int dot_cx = pad_ + dot_r;
        int dot_cy = pad_ + dot_r;
        draw_record_arc(dot_cx, dot_cy, dot_r + 4, state.record_hold_progress);
    }

    // ---- Quit hold warning (center screen, appears at 2.5 s) ----
    draw_quit_warning(state.quit_hold_progress);

    // ---- Help overlay (center screen, visible while H held ≥ 3 s) ----
    if (state.show_help) draw_help_overlay();
}
