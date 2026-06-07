#pragma once
#include <array>
#include <cmath>

// Shutter speed ladder in microseconds, slowest to fastest.
inline constexpr std::array<float, 17> kShutterSteps = {{
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

// ISO ladder.
inline constexpr std::array<int, 7> kIsoSteps = {{
    100, 200, 400, 800, 1600, 3200, 6400
}};

// Index of the shutter step closest to current_us.
inline int shutter_index(float current_us) {
    int best = 0;
    float best_d = std::abs(current_us - kShutterSteps[0]);
    for (int i = 1; i < (int)kShutterSteps.size(); ++i) {
        float d = std::abs(current_us - kShutterSteps[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// Index of the ISO step closest to current.
inline int iso_index(int current) {
    int best = 0;
    int best_d = std::abs(current - kIsoSteps[0]);
    for (int i = 1; i < (int)kIsoSteps.size(); ++i) {
        int d = std::abs(current - kIsoSteps[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}
