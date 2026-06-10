#include "resolution.h"
#include <cmath>
#include <numeric>

static constexpr int   kMinHeight = 480;
static constexpr int   kMaxWidth  = 1920;
static constexpr float kArTol     = 0.02f;  // 2% tolerance for aspect-ratio matching

std::string aspect_str(int w, int h) {
    int g = std::gcd(w, h);
    int rw = w / g, rh = h / g;
    if (rw == 8 && rh == 5) { rw = 16; rh = 10; }  // normalise 8:5 → 16:10
    return std::to_string(rw) + ":" + std::to_string(rh);
}

Resolution select_from_modes(const std::vector<Resolution>& candidates,
                              float target_ratio,
                              Resolution fallback) {
    Resolution best{0, 0};
    for (const auto& c : candidates) {
        if (c.width  > kMaxWidth)  continue;
        if (c.height < kMinHeight) continue;
        float ratio = static_cast<float>(c.width) / static_cast<float>(c.height);
        if (std::abs(ratio - target_ratio) / target_ratio > kArTol) continue;
        if (c.width * c.height > best.width * best.height)
            best = c;
    }
    return (best.width > 0) ? best : fallback;
}

Resolution select_best_resolution(int display_index, Resolution fallback,
                                  const std::string& target_ar) {
    float ratio;
    if      (target_ar == "16:10") ratio = 16.0f / 10.0f;
    else if (target_ar == "4:3")   ratio =  4.0f /  3.0f;
    else                           ratio = 16.0f /  9.0f;  // default 16:9

    int n = SDL_GetNumDisplayModes(display_index);
    if (n < 1) return fallback;

    std::vector<Resolution> candidates;
    candidates.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        SDL_DisplayMode mode{};
        if (SDL_GetDisplayMode(display_index, i, &mode) != 0) continue;
        candidates.push_back({mode.w, mode.h});
    }
    return select_from_modes(candidates, ratio, fallback);
}
