#pragma once
#include <cstdint>
#include <string>

// Compute a blurhash string for the given RGB pixel data (3 bytes/pixel, row-major).
// xC x yC are the DCT component counts (1–9 each); 4x3 is recommended for landscape.
// Returns empty string on failure.
std::string compute_blurhash(const uint8_t* rgb, int width, int height,
                              int xC = 4, int yC = 3);
