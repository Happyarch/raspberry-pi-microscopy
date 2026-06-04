#include "resolution.h"
#include <array>
#include <algorithm>

// Supported modes ordered by descending pixel count.
// Aspect ratios: 16:9, 16:10, 4:3. Range: 640x480–1920x1080.
static constexpr std::array<Resolution, 9> kSupportedModes{{
    {1920, 1080},
    {1280,  800},
    {1280,  720},
    {1024,  768},
    {1024,  640},
    { 800,  600},
    { 854,  480},
    { 768,  480},
    { 640,  480},
}};

Resolution select_best_resolution(int display_index, Resolution fallback) {
    int n = SDL_GetNumDisplayModes(display_index);
    if (n < 1) return fallback;

    // Collect every mode the display advertises.
    for (const auto& supported : kSupportedModes) {
        for (int i = 0; i < n; ++i) {
            SDL_DisplayMode mode{};
            if (SDL_GetDisplayMode(display_index, i, &mode) != 0) continue;
            if (mode.w == supported.width && mode.h == supported.height) {
                return supported;
            }
        }
    }
    return fallback;
}
