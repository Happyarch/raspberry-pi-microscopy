#include "exif_writer.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------
// Little-endian write helpers (TIFF section uses Intel byte order "II")
// ---------------------------------------------------------------------------

static void w16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x & 0xFF);
    v.push_back(x >> 8);
}
static void w32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back(x >> 24);
}
// Write an IFD entry: tag(2) type(2) count(4) value_or_offset(4), all LE.
static void ifd_entry(std::vector<uint8_t>& v, uint16_t tag, uint16_t type,
                      uint32_t count, uint32_t val) {
    w16(v, tag); w16(v, type); w32(v, count); w32(v, val);
}

// ---------------------------------------------------------------------------
// Build the TIFF data block (everything after "Exif\0\0")
// ---------------------------------------------------------------------------

static std::vector<uint8_t> build_tiff(const ExifParams& p) {
    // EXIF TIFF type codes
    constexpr uint16_t ASCII    = 2;
    constexpr uint16_t SHORT    = 3;
    constexpr uint16_t LONG     = 4;
    constexpr uint16_t RATIONAL = 5;

    // ---- Determine which optional tags to include ----
    const bool has_exposure = p.exposure_us > 0;
    const bool has_fstop    = p.fstop > 0;
    const bool has_iso      = p.iso > 0;
    const bool has_lp       = !std::isnan(p.lens_position);

    // ---- Build strings ----
    std::string dt = (p.datetime.size() >= 19) ? p.datetime.substr(0, 19)
                                                : "0000:00:00 00:00:00";

    std::ostringstream desc;
    desc << "microscopi";
    if (p.exposure_mode >= 0 && p.exposure_mode < 4) {
        const char* modes[] = {"P", "A", "S", "M"};
        desc << " mode=" << modes[p.exposure_mode];
    }
    if (has_fstop) {
        desc << std::fixed << std::setprecision(1) << " f/" << p.fstop;
    }
    if (has_exposure) {
        float secs = p.exposure_us / 1e6f;
        if (secs >= 1.0f) desc << std::fixed << std::setprecision(1) << " " << secs << "s";
        else              desc << " 1/" << (int)(1.0f / secs + 0.5f) << "s";
    }
    if (p.iso > 0)  desc << " ISO" << p.iso;
    else            desc << " ISO-AUTO";
    if (has_lp)     desc << std::fixed << std::setprecision(3) << " LP=" << p.lens_position;
    if (!p.camera_model.empty()) desc << " cam=" << p.camera_model;
    const std::string img_desc = desc.str();

    // ASCII tag count = chars + null terminator (no padding counted here).
    // In the value area we pad to an even number of bytes for alignment.
    const uint32_t dt_count       = 20;                            // 19 chars + null
    const uint32_t img_desc_count = (uint32_t)img_desc.size() + 1;
    const uint32_t img_desc_alloc = (img_desc_count + 1) & ~1u;   // round up to even

    // ---- Count EXIF IFD entries ----
    // Always: DateTimeOriginal; optionally: ExposureTime, FNumber, ISO
    int exif_n = 1;
    if (has_exposure) ++exif_n;
    if (has_fstop)    ++exif_n;
    if (has_iso)      ++exif_n;

    // ---- Layout: all offsets are from the TIFF header start (offset 0) ----
    // IFD0: 3 fixed entries (ImageDescription, DateTime, ExifIFD)
    constexpr uint32_t IFD0_START     = 8;
    constexpr uint32_t IFD0_SIZE      = 2 + 3 * 12 + 4;  // count + entries + next
    const     uint32_t EXIF_IFD_START = IFD0_START + IFD0_SIZE;
    const     uint32_t EXIF_IFD_SIZE  = 2 + (uint32_t)exif_n * 12 + 4;
    const     uint32_t VALUES_START   = EXIF_IFD_START + EXIF_IFD_SIZE;

    // Value area: placed sequentially after the IFDs.
    uint32_t cur = VALUES_START;

    const uint32_t off_dt       = cur; cur += dt_count;
    const uint32_t off_imgdesc  = cur; cur += img_desc_alloc;
    const uint32_t off_dto      = cur; cur += dt_count; // DateTimeOriginal

    uint32_t off_exposure = 0;
    if (has_exposure) { off_exposure = cur; cur += 8; }

    uint32_t off_fstop = 0;
    if (has_fstop) { off_fstop = cur; cur += 8; }

    // ---- Serialize ----
    std::vector<uint8_t> t;
    t.reserve(cur);

    // TIFF header
    t.push_back('I'); t.push_back('I');
    w16(t, 0x002A);          // TIFF magic
    w32(t, IFD0_START);      // offset to IFD0

    // IFD0 (3 entries, ascending tag order)
    w16(t, 3);
    ifd_entry(t, 0x010E, ASCII,    img_desc_count, off_imgdesc);  // ImageDescription
    ifd_entry(t, 0x0132, ASCII,    dt_count,        off_dt);      // DateTime
    ifd_entry(t, 0x8769, LONG,     1,               EXIF_IFD_START); // ExifIFD pointer
    w32(t, 0); // no next IFD

    // EXIF IFD (ascending tag order)
    w16(t, (uint16_t)exif_n);
    if (has_exposure)
        ifd_entry(t, 0x829A, RATIONAL, 1, off_exposure);  // ExposureTime
    if (has_fstop)
        ifd_entry(t, 0x829D, RATIONAL, 1, off_fstop);     // FNumber
    if (has_iso)
        ifd_entry(t, 0x8827, SHORT, 1, (uint32_t)(uint16_t)p.iso); // ISOSpeedRatings inline
    ifd_entry(t, 0x9003, ASCII, dt_count, off_dto);        // DateTimeOriginal
    w32(t, 0); // no next IFD

    // Value area — DateTime
    for (int i = 0; i < 19; ++i) t.push_back((uint8_t)dt[i]);
    t.push_back(0);

    // Value area — ImageDescription (padded to even)
    for (char c : img_desc) t.push_back((uint8_t)c);
    t.push_back(0);
    if (img_desc_count % 2 == 1) t.push_back(0); // alignment pad

    // Value area — DateTimeOriginal (copy of DateTime)
    for (int i = 0; i < 19; ++i) t.push_back((uint8_t)dt[i]);
    t.push_back(0);

    // Value area — ExposureTime RATIONAL (numerator=µs, denominator=1000000)
    if (has_exposure) {
        w32(t, (uint32_t)p.exposure_us);
        w32(t, 1000000u);
    }

    // Value area — FNumber RATIONAL (×100 for integer precision)
    if (has_fstop) {
        w32(t, (uint32_t)(p.fstop * 100.0f + 0.5f));
        w32(t, 100u);
    }

    return t;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool insert_exif(const std::string& jpeg_path, const ExifParams& params) {
    // Read the JPEG into memory.
    std::vector<uint8_t> jpeg;
    {
        std::ifstream f(jpeg_path, std::ios::binary);
        if (!f) return false;
        jpeg.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) return false;

    // Build TIFF block and wrap it in an APP1 segment.
    std::vector<uint8_t> tiff = build_tiff(params);
    std::vector<uint8_t> app1;
    app1.push_back(0xFF);
    app1.push_back(0xE1); // APP1 marker
    // APP1 length (BE): includes the 2 length bytes + "Exif\0\0" + TIFF
    uint16_t app1_len = (uint16_t)(2 + 6 + tiff.size());
    app1.push_back(app1_len >> 8);
    app1.push_back(app1_len & 0xFF);
    for (const char c : {'E', 'x', 'i', 'f', '\0', '\0'}) app1.push_back((uint8_t)c);
    app1.insert(app1.end(), tiff.begin(), tiff.end());

    // Find the insertion point inside the JPEG (after SOI at bytes 0-1).
    size_t pos = 2;

    // Skip APP0 (JFIF/JFXX) if present.
    while (pos + 3 < jpeg.size() && jpeg[pos] == 0xFF
           && (jpeg[pos + 1] == 0xE0 || jpeg[pos + 1] == 0xE2)) {
        uint16_t seg_len = ((uint16_t)jpeg[pos + 2] << 8) | jpeg[pos + 3];
        pos += 2 + seg_len;
    }

    // Remove any existing APP1 (EXIF) block at this position.
    if (pos + 3 < jpeg.size() && jpeg[pos] == 0xFF && jpeg[pos + 1] == 0xE1) {
        uint16_t seg_len = ((uint16_t)jpeg[pos + 2] << 8) | jpeg[pos + 3];
        jpeg.erase(jpeg.begin() + (long)pos,
                   jpeg.begin() + (long)pos + 2 + seg_len);
    }

    // Insert our APP1.
    jpeg.insert(jpeg.begin() + (long)pos, app1.begin(), app1.end());

    // Write back to disk.
    std::ofstream out(jpeg_path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(jpeg.data()),
              (std::streamsize)jpeg.size());
    return out.good();
}
