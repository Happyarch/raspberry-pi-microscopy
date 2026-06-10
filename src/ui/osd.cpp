#include "osd.h"
#include "../util/resolution.h"
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

static uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

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

    // Cache directory is per display height. The "w" suffix means white-stroke icons;
    // changing it forces regeneration if an older black-icon cache exists.
    cache_dir_ = cache_dir + "/" + std::to_string(dh_) + "w";
    ensure_cache_dir();

    TTF_Init();
    font_      = TTF_OpenFont(font_path_.c_str(), font_sz_);
    warn_font_ = TTF_OpenFont(font_path_.c_str(), warn_sz_);

    // Pre-load all icons used in the OSD.
    for (const auto& name : {"aperture", "camera", "crosshair", "circle-dot", "sun", "gauge"})
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
    // Lucide icons use stroke="currentColor"; supply a stylesheet so rsvg-convert
    // renders them white instead of the default black.
    if (!fs::exists(png_path)) {
        std::string css_path = cache_dir_ + "/icons.css";
        if (!fs::exists(css_path)) {
            std::ofstream css(css_path);
            css << "svg{color:white;stroke:white;}";
        }
        std::string cmd = "rsvg-convert"
                          " --stylesheet=\"" + css_path + "\""
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
    const std::string tl_str       = keys_.timelapse   + "  (tap / hold 3s to stop)";
    const std::string quit_str     = keys_.quit        + " or q  (hold 5 s)";

    const Row rows[] = {
        {"Mode",          mode_direct},
        {"",              mode_cycle},
        {"Shutter ±",     shutter_str},
        {"Focus ±",       focus_str},
        {"Aperture ±",    aperture_str},
        {"ISO ±",         iso_str},
        {"Autofocus",     keys_.toggle_af},
        {"Still",         keys_.still},
        {"Record",        record_str},
        {"Timelapse",     tl_str},
        {"Crosshair",     keys_.crosshair},
        {"Help",          keys_.help},
        {"Quit",          quit_str},
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

void Osd::draw_mode_list(const OsdState::ModeList& ml) {
    if (!ml.open || !ml.labels || ml.labels->empty()) return;

    const auto& items = *ml.labels;
    const int   n     = (int)items.size();

    const SDL_Color kWhite   = {255, 255, 255, 255};
    const SDL_Color kDim     = {160, 160, 160, 255};
    const SDL_Color kActive  = {100, 210, 100, 255}; // green tint for running mode

    // Layout constants
    const int kMaxVisible = 10;
    const int visible     = std::min(n, kMaxVisible);
    const int row_h       = font_sz_ + pad_ + 2;
    const int title_h     = warn_sz_ + pad_ * 2;
    const int hint_h      = font_sz_ + pad_;
    const int panel_h     = title_h + visible * row_h + hint_h + pad_;
    const int panel_w     = dw_ * 6 / 10;
    const int px          = (dw_ - panel_w) / 2;
    const int py          = (dh_ - panel_h) / 2;

    // Scroll window: keep selected item visible
    int scroll = 0;
    if (ml.selected >= visible)
        scroll = ml.selected - visible + 1;

    // Background panel
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 10, 10, 220);
    SDL_Rect panel{px, py, panel_w, panel_h};
    SDL_RenderFillRect(renderer_, &panel);

    // Title
    {
        int tw = 0, th = 0;
        if (warn_font_) TTF_SizeUTF8(warn_font_, "Camera Resolution", &tw, &th);
        draw_text("Camera Resolution",
                  px + (panel_w - tw) / 2, py + pad_, kWhite, warn_font_);
    }

    // Items
    int ry = py + title_h;
    for (int i = 0; i < visible; ++i) {
        int idx = i + scroll;
        if (idx >= n) break;
        const std::string& label = items[idx];

        bool is_sel    = (idx == ml.selected);
        bool is_active = (idx == ml.active);

        // Cursor and active marker
        std::string prefix = "  ";
        if (is_sel && is_active) prefix = "> ";
        else if (is_sel)         prefix = "> ";
        else if (is_active)      prefix = "* ";

        SDL_Color col = is_active ? kActive : (is_sel ? kWhite : kDim);
        if (is_sel) {
            // Highlight bar behind selected row
            SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 200);
            SDL_Rect bar{px + 2, ry - 1, panel_w - 4, row_h};
            SDL_RenderFillRect(renderer_, &bar);
        }
        draw_text(prefix + label, px + pad_, ry, col);
        ry += row_h;
    }

    // Scroll indicator dots when list overflows
    if (n > kMaxVisible) {
        std::string dots = std::to_string(ml.selected + 1) + "/" + std::to_string(n);
        draw_text(dots, px + panel_w - pad_ - (int)dots.size() * font_sz_,
                  py + title_h, kDim);
    }

    // Hint bar at bottom
    const std::string hint = "  \xe2\x86\x91\xe2\x86\x93 navigate     Enter confirm     Esc cancel";
    draw_text(hint, px + pad_, py + panel_h - hint_h, kDim);
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

void Osd::draw_record_arc(int cx, int cy, int radius, float progress,
                           SDL_Color color) {
    if (progress <= 0.0f) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
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
// Top bar
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

void Osd::draw_top_bar(const OsdState& state) {
    const SDL_Color kWhite = {255, 255, 255, 255};
    const SDL_Color kDim   = {120, 120, 120, 200};
    const SDL_Color kRed   = {220,  40,  40, 255};
    const SDL_Color kAmber = {230, 160,   0, 255};

    int top_icon_y = (bar_h_ - icon_sz_) / 2;
    int top_text_y = (bar_h_ - font_sz_ - 2) / 2;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
    SDL_Rect bar{0, 0, dw_, bar_h_};
    SDL_RenderFillRect(renderer_, &bar);

    // Column centers at dw/8, 3dw/8, 5dw/8, 7dw/8
    int col1 = dw_ / 8;
    int col2 = 3 * dw_ / 8;
    int col3 = 5 * dw_ / 8;
    int col4 = 7 * dw_ / 8;

    auto draw_col_text = [&](const std::string& text, int cx, SDL_Color col,
                              TTF_Font* f = nullptr) {
        TTF_Font* ff = f ? f : font_;
        int tw = 0, th = 0;
        if (ff) TTF_SizeUTF8(ff, text.c_str(), &tw, &th);
        draw_text(text, cx - tw / 2, top_text_y, col, f);
    };

    // ---- Col 1: Exposure mode (large) ----
    draw_col_text(mode_label(state.exposure_mode), col1, kWhite, warn_font_);

    // ---- Col 2: Camera icon + still count ----
    {
        char buf[8];
        snprintf(buf, sizeof(buf), " %03d", state.still_count);
        std::string label = buf;
        int tw = 0, th = 0;
        if (font_) TTF_SizeUTF8(font_, label.c_str(), &tw, &th);
        int total_w = icon_sz_ + tw;
        int sx = col2 - total_w / 2;
        draw_icon(icons_["camera"], sx, top_icon_y);
        draw_text(label, sx + icon_sz_, top_text_y, kWhite);
    }

    // ---- Col 3: Aspect ratio + resolution ----
    if (state.cam_width > 0 && state.cam_height > 0) {
        std::string ar  = aspect_str(state.cam_width, state.cam_height);
        // × = U+00D7, UTF-8: C3 97
        std::string res = ar + "  " + std::to_string(state.cam_width)
                        + "\xc3\x97" + std::to_string(state.cam_height);
        draw_col_text(res, col3, kWhite);
    }

    // ---- Col 4: Recording / TL active / Idle ----
    if (state.recording) {
        uint64_t ms      = now_ms() - state.record_start_ms;
        uint64_t total_s = ms / 1000;
        uint64_t arc_s   = (ms % 1000) * 60 / 1000;
        uint64_t s = total_s % 60, m = (total_s / 60) % 60, h = total_s / 3600;
        char ts[40];
        // ● = U+25CF, UTF-8: E2 97 8F
        snprintf(ts, sizeof(ts), "\xe2\x97\x8f %02llu:%02llu:%02llu'%02llu",
                 (unsigned long long)h, (unsigned long long)m,
                 (unsigned long long)s, (unsigned long long)arc_s);
        draw_col_text(ts, col4, kRed);
    } else if (state.tl_active) {
        char buf[24];
        snprintf(buf, sizeof(buf), "TL  %03d", state.tl_count);
        draw_col_text(buf, col4, kAmber);
    } else {
        draw_col_text("--:--:--", col4, kDim);
    }

    // ---- Hold arcs (top-left of top bar) ----
    int dot_r  = bar_h_ / 4;
    int dot_cx = pad_ + dot_r;
    int dot_cy = bar_h_ / 2;

    if (state.record_hold_progress > 0.0f)
        draw_record_arc(dot_cx, dot_cy, dot_r + 4, state.record_hold_progress);

    if (state.tl_hold_progress > 0.0f && state.tl_active)
        draw_record_arc(dot_cx, dot_cy, dot_r + 4, state.tl_hold_progress, kAmber);
}

// ---------------------------------------------------------------------------
// Timelapse config dialog
// ---------------------------------------------------------------------------

void Osd::draw_tl_dialog(const OsdState& state) {
    if (!state.tl_dialog_open) return;

    const SDL_Color kWhite = {255, 255, 255, 255};
    const SDL_Color kDim   = {150, 150, 150, 200};

    const int title_h  = warn_sz_ + pad_ * 2;
    const int blank_h  = font_sz_ + pad_;
    const int field_h  = font_sz_ + pad_ * 2;
    const int footer_h = font_sz_ + pad_ * 2;
    const int panel_w  = dw_ / 2;
    const int panel_h  = title_h + blank_h + field_h * 2 + pad_ + footer_h + pad_;
    const int px = (dw_ - panel_w) / 2;
    const int py = (dh_ - panel_h) / 2;

    // Measure label column width from the longer label
    int label_col_w = font_sz_ * 10;
    if (font_) {
        int w1 = 0, w2 = 0, tmp = 0;
        TTF_SizeUTF8(font_, "Interval (s):", &w1, &tmp);
        TTF_SizeUTF8(font_, "Max frames:",   &w2, &tmp);
        label_col_w = std::max(w1, w2) + pad_;
    }

    const int box_x     = px + pad_ + label_col_w;
    const int box_w     = panel_w - pad_ * 2 - label_col_w;
    const int box_tx    = box_x + pad_;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 10, 10, 220);
    SDL_Rect panel{px, py, panel_w, panel_h};
    SDL_RenderFillRect(renderer_, &panel);

    // Title
    {
        int tw = 0, th = 0;
        if (warn_font_) TTF_SizeUTF8(warn_font_, "Timelapse", &tw, &th);
        draw_text("Timelapse", px + (panel_w - tw) / 2, py + pad_, kWhite, warn_font_);
    }

    bool blink_on = (SDL_GetTicks64() / 500) % 2 == 0;

    auto draw_field = [&](int idx, const char* label, const std::string& value, int fy) {
        bool active = (state.tl_dialog_field == idx);
        SDL_Color text_col = active ? kWhite : kDim;
        draw_text(label, px + pad_, fy + pad_, text_col);

        SDL_Color border = active ? SDL_Color{200, 200, 200, 255}
                                  : SDL_Color{ 80,  80,  80, 200};
        SDL_SetRenderDrawColor(renderer_, border.r, border.g, border.b, border.a);
        SDL_Rect box{box_x, fy, box_w, field_h};
        SDL_RenderDrawRect(renderer_, &box);

        std::string display = value + (active && blink_on ? "_" : "");
        draw_text(display, box_tx, fy + pad_, text_col);
    };

    int fy = py + title_h + blank_h;
    draw_field(0, "Interval (s):", state.tl_dialog_interval, fy);
    fy += field_h + pad_;
    draw_field(1, "Max frames:",   state.tl_dialog_frames,   fy);

    // Footer
    const char* footer = "Tab switch  \xc2\xb7  Enter start  \xc2\xb7  Esc cancel";
    {
        int tw = 0, th = 0;
        if (font_) TTF_SizeUTF8(font_, footer, &tw, &th);
        draw_text(footer, px + (panel_w - tw) / 2,
                  py + panel_h - footer_h + pad_, kDim);
    }
}

// ---------------------------------------------------------------------------
// Main draw — called every frame
// ---------------------------------------------------------------------------

void Osd::draw(const OsdState& state) {
    const SDL_Color kWhite   = {255, 255, 255, 255};
    const SDL_Color kInactive = {175, 175, 175, 255};

    // Guide overlay drawn first so bars sit on top.
    if (state.show_crosshair) draw_crosshair();

    // Top bar: mode, stills, resolution, recording/TL status + hold arcs
    draw_top_bar(state);

    // ---- Bottom bar ----
    int bar_y  = dh_ - bar_h_;
    int icon_y = bar_y + (bar_h_ - icon_sz_) / 2;
    int text_y = bar_y + (bar_h_ - font_sz_ - 2) / 2;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
    SDL_Rect bot_bar{0, bar_y, dw_, bar_h_};
    SDL_RenderFillRect(renderer_, &bot_bar);

    auto tw = [&](const char* s) {
        int w = 0, h = 0;
        if (font_) TTF_SizeUTF8(font_, s, &w, &h);
        return w;
    };

    // Four items centered at quarter columns: shutter | AE/AF | aperture | ISO
    int col1 = dw_ / 8;
    int col2 = 3 * dw_ / 8;
    int col3 = 5 * dw_ / 8;
    int col4 = 7 * dw_ / 8;

    // ---- Col 1: Shutter ----
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
        int x = col1 - tw(exp_str.c_str()) / 2;
        draw_text(exp_str, x, text_y, kWhite);
    }

    // ---- Col 2: AF / MF status ----
    {
        const char* af_lbl = state.af_enabled ? "AF" : "MF";
        SDL_Color af_col   = state.af_enabled ? kWhite : kInactive;
        int x = col2 - tw(af_lbl) / 2;
        draw_text(af_lbl, x, text_y, af_col);
    }

    // ---- Col 3: Aperture ----
    {
        std::string ap_str;
        if (state.aperture > 0.0f) {
            std::ostringstream ss;
            ss << "f/" << std::fixed << std::setprecision(1) << state.aperture;
            ap_str = ss.str();
        } else {
            ap_str = "f/--";
        }
        int total = icon_sz_ + 4 + tw(ap_str.c_str());
        int sx = col3 - total / 2;
        draw_icon(icons_["aperture"], sx, icon_y);
        draw_text(ap_str, sx + icon_sz_ + 4, text_y, kWhite);
    }

    // ---- Col 4: ISO ----
    {
        std::string iso_val = "ISO " + ((state.iso == 0) ? std::string("AUTO") : std::to_string(state.iso));
        int total = icon_sz_ + 4 + tw(iso_val.c_str());
        int sx = col4 - total / 2;
        draw_icon(icons_["gauge"], sx, icon_y);
        draw_text(iso_val, sx + icon_sz_ + 4, text_y, kWhite);
    }

    // ---- Quit hold warning (center screen, appears at 2.5 s) ----
    draw_quit_warning(state.quit_hold_progress);

    // ---- Help overlay ----
    if (state.show_help) draw_help_overlay();

    // ---- Timelapse dialog ----
    draw_tl_dialog(state);

    // ---- Camera mode list ----
    draw_mode_list(state.mode_list);
}
