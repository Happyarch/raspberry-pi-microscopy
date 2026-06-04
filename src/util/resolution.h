#pragma once
#include <SDL2/SDL.h>

struct Resolution {
    int width;
    int height;
};

// Query the EDID-advertised display modes via SDL2 and return the best
// supported mode (highest pixel count, aspect ratio 4:3 / 16:9 / 16:10,
// capped at 1920x1080, minimum 640x480). Falls back to `fallback` if
// no supported mode is found.
Resolution select_best_resolution(int display_index, Resolution fallback);
