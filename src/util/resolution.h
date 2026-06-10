#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct Resolution {
    int width;
    int height;
    bool operator==(const Resolution& o) const { return width == o.width && height == o.height; }
};

// Return a human-readable aspect ratio string, e.g. "16:9", "16:10", "4:3".
// 8:5 is normalised to "16:10".
std::string aspect_str(int w, int h);

// Pure filter: from a list of candidates return the one with the most pixels
// that matches target_ratio within 2% tolerance, height >= 480, width <= 1920.
// Returns fallback if nothing qualifies.
Resolution select_from_modes(const std::vector<Resolution>& candidates,
                              float target_ratio,
                              Resolution fallback);

// Query the EDID-advertised display modes via SDL2 and return the best
// supported mode for the given aspect ratio.
// target_ar: "16:9" | "16:10" | "4:3" (default "16:9").
// Falls back to `fallback` if no suitable mode is found.
Resolution select_best_resolution(int display_index, Resolution fallback,
                                  const std::string& target_ar = "16:9");
