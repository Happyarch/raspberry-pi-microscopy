#include "gallery.h"

#include <SDL2/SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void gallery_compute_layout(GalleryState& state, int dw, int dh) {
    // Tab bar occupies ~1/14 of display height (matches OSD bar_h).
    int tab_h = std::max(28, dh / 14);
    int usable_h = dh - tab_h * 2; // two bars: tab bar + bottom padding

    // Target tile size: cap by both width (1/5 of dw) and height (1/4 of dh)
    // so that taller displays don't end up with fewer rows than shorter ones.
    int tile_w = std::min(dw / 5, dh / 4);
    if (tile_w < 80) tile_w = 80;

    int tiles_per_row = dw / tile_w;
    if (tiles_per_row < 1) tiles_per_row = 1;

    // Pad to fill row: tiles fill exact width
    tile_w = dw / tiles_per_row;

    int tile_h   = tile_w; // square tiles
    int rows_vis = usable_h / tile_h;
    if (rows_vis < 1) rows_vis = 1;

    // If fewer than 6 tiles total, try fitting more per row.
    if (tiles_per_row * rows_vis < 6 && tiles_per_row > 2) {
        ++tiles_per_row;
        tile_w   = dw / tiles_per_row;
        tile_h   = tile_w;
        rows_vis = usable_h / tile_h;
        if (rows_vis < 1) rows_vis = 1; // re-clamp after adjustment
    }

    state.tiles_per_row = tiles_per_row;
    state.tile_w        = tile_w;
    state.tile_h        = tile_h;
    state.rows_visible  = rows_vis;
}

// ---------------------------------------------------------------------------
// Texture helpers
// ---------------------------------------------------------------------------

void gallery_free_textures(GalleryState& state) {
    for (SDL_Texture* t : state.thumbs) if (t) SDL_DestroyTexture(t);
    state.thumbs.clear();
    if (state.preview_tex) { SDL_DestroyTexture(state.preview_tex); state.preview_tex = nullptr; }
    state.fullscreen = false;
}

static SDL_Texture* load_thumbnail(SDL_Renderer* renderer, const std::string& path) {
    if (::access(path.c_str(), F_OK) != 0) return nullptr;
    SDL_Surface* sur = IMG_Load(path.c_str());
    if (!sur) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, sur);
    SDL_FreeSurface(sur);
    return tex;
}

// ---------------------------------------------------------------------------
// Page loading
// ---------------------------------------------------------------------------

void gallery_load_page(GalleryState& state, MediaDb* db, SDL_Renderer* renderer,
                       const std::string& thumb_cache_dir)
{
    // Free existing textures (but not preview_tex — that's freed separately).
    for (SDL_Texture* t : state.thumbs) if (t) SDL_DestroyTexture(t);
    state.thumbs.clear();
    state.items.clear();
    state.sessions.clear();

    if (!db) return;

    int per_page = state.tiles_per_row * state.rows_visible;
    if (per_page < 1) per_page = 20;
    int offset = state.page * per_page;

    switch (state.tab) {
    case GalleryState::Tab::Stills:
        state.items = db->list_stills(offset, per_page);
        break;
    case GalleryState::Tab::Videos:
        state.items = db->list_videos(offset, per_page);
        break;
    case GalleryState::Tab::Timelapses:
        if (state.tl_session_id > 0)
            state.items = db->list_timelapse_frames(state.tl_session_id, offset, per_page);
        else
            state.sessions = db->list_timelapses(offset, per_page);
        break;
    }

    // Load thumbnails for items.
    if (!state.items.empty()) {
        state.thumbs.reserve(state.items.size());
        for (auto& item : state.items) {
            std::string tpath = thumb_cache_dir + "/" + std::to_string(item.id) + ".jpg";
            state.thumbs.push_back(load_thumbnail(renderer, tpath));
        }
    } else {
        // Timelapse sessions: no thumbnails (show placeholder icons).
        state.thumbs.assign(state.sessions.size(), nullptr);
    }

    // Clamp selection to valid range.
    int total = (int)std::max(state.items.size(), state.sessions.size());
    if (state.selection >= total) state.selection = std::max(0, total - 1);
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

static void draw_rect_filled(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

static void draw_text_simple(SDL_Renderer* r, TTF_Font* font,
                              const std::string& text, int x, int y, SDL_Color col) {
    if (!font || text.empty()) return;
    SDL_Surface* sur = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!sur) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, sur);
    SDL_Rect dst{x, y, sur->w, sur->h};
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(sur);
}

// Render text clipped to max_w pixels; truncates with "…" if needed.
static void draw_text_clipped(SDL_Renderer* r, TTF_Font* font,
                               const std::string& text, int x, int y,
                               int max_w, SDL_Color col) {
    if (!font || text.empty()) return;
    std::string s = text;
    int w, h;
    TTF_SizeUTF8(font, s.c_str(), &w, &h);
    if (w > max_w && s.size() > 3) {
        // Binary-search for the longest substring that fits.
        while (s.size() > 1 && w > max_w) {
            s.pop_back();
            TTF_SizeUTF8(font, (s + "…").c_str(), &w, &h);
        }
        s += "…";
    }
    draw_text_simple(r, font, s, x, y, col);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void gallery_render(SDL_Renderer* renderer, int dw, int dh,
                    const GalleryState& state, TTF_Font* font)
{
    // Dims proportional to display height (same as OSD).
    int bar_h  = std::max(28, dh / 14);
    int pad    = std::max(4,  bar_h / 6);
    int fsz    = std::max(9,  (int)(bar_h * 0.38f));
    int fsz_sm = std::max(7,  fsz - 2);
    (void)fsz; // may warn if only fsz_sm used below

    // Semi-transparent background.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect bg{0, 0, dw, dh};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // ---- Full-screen still preview ----
    if (state.fullscreen && state.preview_tex) {
        SDL_RenderCopy(renderer, state.preview_tex, nullptr, nullptr);
        // Caption bar at bottom.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_Rect cap{0, dh - bar_h, dw, bar_h};
        SDL_RenderFillRect(renderer, &cap);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_Color white{255, 255, 255, 255};
        draw_text_clipped(renderer, font, state.preview_name, pad, dh - bar_h + pad,
                          dw - pad * 2, white);
        // "Esc to close" hint.
        SDL_Color grey{150, 150, 150, 255};
        draw_text_simple(renderer, font, "Esc: close", dw - 120, dh - bar_h + pad, grey);
        return;
    }

    // ---- Tab bar ----
    {
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_Rect tb{0, 0, dw, bar_h};
        SDL_RenderFillRect(renderer, &tb);

        const char* labels[] = {"Stills", "Timelapses", "Videos"};
        int tab_w = dw / 3;
        for (int i = 0; i < 3; ++i) {
            bool active = ((int)state.tab == i);
            SDL_Color col = active ? SDL_Color{255, 255, 255, 255}
                                   : SDL_Color{120, 120, 120, 255};
            int tx = i * tab_w + (tab_w - (int)strlen(labels[i]) * fsz / 2) / 2;
            draw_text_simple(renderer, font, labels[i], tx, (bar_h - fsz) / 2, col);
            if (active) {
                SDL_SetRenderDrawColor(renderer, 70, 130, 240, 255);
                SDL_Rect ul{i * tab_w, bar_h - 2, tab_w, 2};
                SDL_RenderFillRect(renderer, &ul);
            }
        }
    }

    int content_y = bar_h + pad;

    // ---- Timelapse breadcrumb ----
    if (state.tab == GalleryState::Tab::Timelapses && state.tl_session_id > 0) {
        SDL_Color acol{100, 160, 255, 255};
        draw_text_simple(renderer, font, "\xe2\x86\x90 Sessions | " + state.tl_session_name,
                         pad, content_y, acol);
        content_y += fsz + pad;
    }

    // ---- Tile grid ----
    int total = (int)std::max(state.items.size(), state.sessions.size());
    for (int i = 0; i < total; ++i) {
        int col = i % state.tiles_per_row;
        int row = i / state.tiles_per_row;
        int tx  = col * state.tile_w;
        int ty  = content_y + row * state.tile_h;

        bool selected = (i == state.selection);

        // Tile background.
        SDL_Color bg_col = selected ? SDL_Color{40, 60, 100, 255}
                                    : SDL_Color{25, 25, 25, 255};
        draw_rect_filled(renderer, tx, ty, state.tile_w, state.tile_h, bg_col);

        // Thumbnail or placeholder.
        int thumb_sz = state.tile_h - fsz * 2 - pad * 3;
        if (thumb_sz > 0) {
            SDL_Texture* tex = (i < (int)state.thumbs.size()) ? state.thumbs[i] : nullptr;
            if (tex) {
                SDL_Rect dst{tx + (state.tile_w - thumb_sz) / 2,
                             ty + pad, thumb_sz, thumb_sz};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            } else {
                // Placeholder rectangle.
                SDL_Color ph{50, 50, 60, 255};
                draw_rect_filled(renderer,
                                 tx + (state.tile_w - thumb_sz) / 2, ty + pad,
                                 thumb_sz, thumb_sz, ph);
            }
        }

        // Labels below thumbnail.
        int label_y = ty + pad + thumb_sz + pad;
        SDL_Color name_col{210, 210, 210, 255};
        SDL_Color date_col{110, 110, 110, 255};

        if (!state.sessions.empty() && i < (int)state.sessions.size()) {
            const auto& s = state.sessions[i];
            draw_text_clipped(renderer, font, s.session_name,
                              tx + pad, label_y, state.tile_w - pad * 2, name_col);
            std::string sub = s.started_at.substr(0, 10) +
                              " | " + std::to_string(s.frame_count) + " fr";
            draw_text_clipped(renderer, font, sub,
                              tx + pad, label_y + fsz_sm + 1, state.tile_w - pad * 2, date_col);
        } else if (i < (int)state.items.size()) {
            const auto& item = state.items[i];
            draw_text_clipped(renderer, font, item.filename,
                              tx + pad, label_y, state.tile_w - pad * 2, name_col);
            draw_text_clipped(renderer, font, item.captured_at.substr(0, 10),
                              tx + pad, label_y + fsz_sm + 1, state.tile_w - pad * 2, date_col);
        }

        // Selection border.
        if (selected) {
            SDL_Color sel_col{80, 140, 255, 255};
            draw_rect_outline(renderer, tx + 1, ty + 1,
                              state.tile_w - 2, state.tile_h - 2, sel_col);
        }
    }

    // ---- Page hint ----
    {
        int hint_y = dh - bar_h;
        SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
        SDL_Rect pb{0, hint_y, dw, bar_h};
        SDL_RenderFillRect(renderer, &pb);
        SDL_Color hint_col{100, 100, 100, 255};
        std::string hint = "Pg " + std::to_string(state.page + 1) +
                           " | Arrow:nav  Enter:select  Tab:tab  Esc/g:close";
        draw_text_simple(renderer, font, hint, pad, hint_y + (bar_h - fsz_sm) / 2, hint_col);
    }
}
