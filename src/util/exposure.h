#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Master reference tables — all standard photographic stops.
// Runtime ladders are filtered subsets of these, bounded by the camera's
// actual supported range (queried via Camera::shutter_range() etc.).
// ---------------------------------------------------------------------------

// Standard shutter speeds in µs, slowest first (descending).
inline constexpr std::array<float, 17> kShutterMaster = {{
    2000000.0f, // 2"
    1000000.0f, // 1"
     500000.0f, // 1/2
     250000.0f, // 1/4
     125000.0f, // 1/8
      66667.0f, // 1/15
      33333.0f, // 1/30
      16667.0f, // 1/60
       8333.0f, // 1/120
       4000.0f, // 1/250
       2000.0f, // 1/500
       1000.0f, // 1/1000
        500.0f, // 1/2000
        250.0f, // 1/4000
        125.0f, // 1/8000
         62.5f, // 1/16000
         31.25f,// 1/32000
}};

// Standard ISO values, ascending.
inline constexpr std::array<int, 8> kIsoMaster = {{
    100, 200, 400, 800, 1600, 3200, 6400, 12800
}};

// Standard f-stop aperture values, ascending (wider → narrower aperture).
inline constexpr std::array<float, 11> kApertureMaster = {{
    1.0f, 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f, 32.0f
}};

// ---------------------------------------------------------------------------
// Runtime ladder builders
// ---------------------------------------------------------------------------

// Build a shutter ladder from the master list keeping steps within [min_us, max_us].
// Returns at least one entry. Ladder is descending (slowest = index 0).
inline std::vector<float> build_shutter_ladder(float min_us, float max_us) {
    std::vector<float> result;
    for (float s : kShutterMaster)
        if (s >= min_us && s <= max_us)
            result.push_back(s);
    if (result.empty())
        result.push_back(std::clamp(16667.0f, min_us, max_us)); // 1/60 fallback
    return result;
}

// Build an ISO ladder from the master list.
// gain_to_iso approximation: ISO ≈ gain × 100.
// Ladder is ascending (lowest ISO = index 0).
inline std::vector<int> build_iso_ladder(float min_gain, float max_gain) {
    int min_iso = (int)(min_gain * 100.0f);
    int max_iso = (int)(max_gain * 100.0f);
    std::vector<int> result;
    for (int iso : kIsoMaster)
        if (iso >= min_iso && iso <= max_iso)
            result.push_back(iso);
    if (result.empty())
        result.push_back(std::max(min_iso, 100)); // fallback
    return result;
}

// Build an aperture ladder from the master list within [min_fstop, max_fstop].
// Ladder is ascending (widest aperture = smallest f-number = index 0).
inline std::vector<float> build_aperture_ladder(float min_fstop, float max_fstop) {
    std::vector<float> result;
    for (float f : kApertureMaster)
        if (f >= min_fstop && f <= max_fstop)
            result.push_back(f);
    if (result.empty())
        result.push_back(std::clamp(2.8f, min_fstop, max_fstop)); // fallback
    return result;
}

// ---------------------------------------------------------------------------
// Index lookup helpers (nearest-neighbour, all ladders)
// ---------------------------------------------------------------------------

// Index in a shutter ladder (descending µs) closest to current_us.
inline int shutter_index(float current_us, const std::vector<float>& ladder) {
    if (ladder.empty()) return 0;
    int best = 0;
    float best_d = std::abs(ladder[0] - current_us);
    for (int i = 1; i < (int)ladder.size(); ++i) {
        float d = std::abs(ladder[i] - current_us);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// Index in an ISO ladder (ascending) closest to current ISO.
inline int iso_index(int current, const std::vector<int>& ladder) {
    if (ladder.empty()) return 0;
    int best = 0;
    int best_d = std::abs(ladder[0] - current);
    for (int i = 1; i < (int)ladder.size(); ++i) {
        int d = std::abs(ladder[i] - current);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// Index in an aperture ladder (ascending f-stops) closest to current f-stop.
inline int aperture_index(float current_fstop, const std::vector<float>& ladder) {
    if (ladder.empty()) return 0;
    int best = 0;
    float best_d = std::abs(ladder[0] - current_fstop);
    for (int i = 1; i < (int)ladder.size(); ++i) {
        float d = std::abs(ladder[i] - current_fstop);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}
