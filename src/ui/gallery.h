#pragma once
#include "../util/media_db.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

struct GalleryState {
    enum class Tab { Stills, Timelapses, Videos } tab{Tab::Stills};

    // Timelapse drill-down: 0 = session list, >0 = showing frames of that session
    int64_t     tl_session_id{0};
    std::string tl_session_name;

    int selection{0};  // linear index of selected tile on current page
    int page{0};

    std::vector<MediaItem>        items;    // stills / videos / tl_frames on current page
    std::vector<TimelapseSession> sessions; // timelapse sessions on current page

    // Tile layout — recomputed each time gallery opens / display resizes
    int tiles_per_row{4};
    int tile_w{0}, tile_h{0};
    int rows_visible{0};

    // Per-tile thumbnail textures (parallel to items / sessions)
    std::vector<SDL_Texture*> thumbs;

    // Full-screen still preview
    bool         fullscreen{false};
    SDL_Texture* preview_tex{nullptr};
    std::string  preview_name;
};

// Compute tile layout from display dimensions; call when gallery opens.
void gallery_compute_layout(GalleryState& state, int dw, int dh);

// Load current page from the database and thumbnails from the cache directory.
// Frees any previously loaded thumbnail textures first.
void gallery_load_page(GalleryState& state, MediaDb* db, SDL_Renderer* renderer,
                       const std::string& thumb_cache_dir);

// Free all SDL textures owned by state (call before closing gallery).
void gallery_free_textures(GalleryState& state);

// Draw the gallery overlay onto the renderer (does not call SDL_RenderPresent).
void gallery_render(SDL_Renderer* renderer, int dw, int dh,
                    const GalleryState& state, TTF_Font* font);
