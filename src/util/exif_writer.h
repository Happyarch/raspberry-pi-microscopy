#pragma once
#include <limits>
#include <string>

struct ExifParams {
    float       exposure_us{0};     // µs; 0 = unknown
    float       fstop{0};           // f-number; 0 = unknown/fixed
    int         iso{0};             // actual ISO used; 0 = unknown
    float       lens_position{std::numeric_limits<float>::quiet_NaN()}; // 0–1; NaN = AF
    int         exposure_mode{-1};  // 0=P,1=A,2=S,3=M; -1 = unknown
    std::string datetime;           // "YYYY:MM:DD HH:MM:SS"
    std::string camera_model;       // e.g. "imx219"
};

// Inject an EXIF APP1 segment into a JPEG file produced by ffmpeg.
// Inserts after the SOI marker (and after any existing JFIF APP0).
// Replaces any existing APP1 block. Returns false if the file cannot
// be read or is not a valid JPEG.
bool insert_exif(const std::string& jpeg_path, const ExifParams& params);
