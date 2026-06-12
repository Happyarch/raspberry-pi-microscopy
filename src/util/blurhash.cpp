#include "blurhash.h"

#include <algorithm>
#include <cmath>
#include <vector>

static const char kB83[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#$%*+,-.:;=?@[]^_{|}~";

static void enc83(long v, int len, char* out) {
    int div = 1;
    for (int i = 0; i < len - 1; ++i) div *= 83;
    for (int i = 0; i < len; ++i) { *out++ = kB83[(v / div) % 83]; div /= 83; }
}

static float to_linear(uint8_t v) {
    float x = v / 255.0f;
    return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static uint8_t to_srgb(float v) {
    v = std::max(0.0f, std::min(1.0f, v));
    return (uint8_t)(v <= 0.0031308f ? v * 12.92f * 255.0f + 0.5f
                                     : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f) * 255.0f + 0.5f);
}

struct C3 { float r, g, b; };

static C3 dct(int cx, int cy, int w, int h, const uint8_t* rgb) {
    float r = 0, g = 0, b = 0;
    float norm = (cx == 0 && cy == 0) ? 1.0f : 2.0f;
    for (int y = 0; y < h; ++y) {
        float cy_factor = std::cos((float)M_PI * cy * y / h);
        for (int x = 0; x < w; ++x) {
            float basis = norm * std::cos((float)M_PI * cx * x / w) * cy_factor;
            const uint8_t* p = rgb + (y * w + x) * 3;
            r += basis * to_linear(p[0]);
            g += basis * to_linear(p[1]);
            b += basis * to_linear(p[2]);
        }
    }
    float s = 1.0f / (w * h);
    return { r * s, g * s, b * s };
}

static long enc_dc(C3 c) {
    return ((long)to_srgb(c.r) << 16) | ((long)to_srgb(c.g) << 8) | to_srgb(c.b);
}

static long enc_ac(C3 c, float mx) {
    auto q = [&](float v) -> long {
        return (long)std::max(0.0f, std::min(18.0f,
            std::floor(std::copysign(std::pow(std::abs(v) / mx, 0.5f), v) * 9.0f + 9.5f)));
    };
    return q(c.r) * 361 + q(c.g) * 19 + q(c.b);
}

std::string compute_blurhash(const uint8_t* rgb, int w, int h, int xC, int yC) {
    if (xC < 1 || xC > 9 || yC < 1 || yC > 9 || w <= 0 || h <= 0 || !rgb) return "";

    std::vector<C3> comp;
    comp.reserve(xC * yC);
    for (int j = 0; j < yC; ++j)
        for (int i = 0; i < xC; ++i)
            comp.push_back(dct(i, j, w, h, rgb));

    float max_ac = 0.0f;
    for (int i = 1; i < (int)comp.size(); ++i)
        max_ac = std::max({max_ac, std::abs(comp[i].r), std::abs(comp[i].g), std::abs(comp[i].b)});

    int quant_max = max_ac > 0.0f
        ? std::max(0, std::min(82, (int)std::floor(max_ac * 166.0f - 0.5f)))
        : 0;
    float ac_scale = max_ac > 0.0f ? (float)(quant_max + 1) / 166.0f : 1.0f;

    int n_ac = xC * yC - 1;
    std::string out(1 + 1 + 4 + n_ac * 2, '\0');
    char* p = &out[0];

    enc83((xC - 1) + (yC - 1) * 9, 1, p); p += 1;
    enc83(quant_max,                1, p); p += 1;
    enc83(enc_dc(comp[0]),          4, p); p += 4;
    for (int i = 1; i < (int)comp.size(); ++i) {
        enc83(enc_ac(comp[i], ac_scale), 2, p); p += 2;
    }

    return out;
}
