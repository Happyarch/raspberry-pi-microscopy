#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Timelapse interval schedule functions
//
// All functions compute I(n) — the interval in milliseconds before the
// (n+1)-th capture, where n = frames already captured.
//
// Parameters (see TlParams below):
//   B   = base_ms     : offset; I(0) == B for all functions
//   k   = rate_const  : rate constant (units depend on fn)
//   p   = power       : exponent for fn=power (aliases: quadratic=2, cubic=3, quintic=5)
//   β   = beta        : stretch exponent for fn=stretched_exp  (0 < β ≤ 1)
//   m   = inflection  : frame at sigmoid midpoint for fn=logistic
//   F   = floor_ms    : hard minimum (essential for decay modes)
//   C   = ceil_ms     : hard maximum (essential for growth modes)
//
// Rate-law correspondence:
//   linear        zero-order  dI/dn = 0
//   exp_grow      first-order dI/dn = k·I          → I = B·eᵏⁿ
//   exp_decay     first-order dI/dn = -k·I         → I = B·e^(-kn)
//   hyperbolic    second-order integrated           → I = B/(1+kn)
//   log           integral of 1/(n+1) rate         → I = B + k·ln(n+1)
//   power         power-law rate dI/dn ∝ n^(p-1)  → I = B + k·nᵖ
//   michaelis     MM saturation (Vmax·n/(Km+n))    → I = B + (C-B)·kn/(1+kn)
//   logistic      sigmoid / Hill (cooperative)     → I = B + (C-B)·σ(k(n-m))
//   stretched_exp KWW dispersive kinetics          → I = B·exp(k·nᵝ)
// ---------------------------------------------------------------------------

enum class TlFn {
    Linear,
    ExpGrow,
    ExpDecay,
    Log,
    Power,          // aliases: quadratic (p=2), cubic (p=3), quintic (p=5)
    MichaelisMenten,
    Logistic,
    StretchedExp,
    Hyperbolic,
};

struct TlParams {
    uint64_t base_ms    = 5000;
    float    k          = 0.05f;
    float    power      = 2.0f;   // exponent p for TlFn::Power
    float    beta       = 0.7f;   // stretch exponent β for TlFn::StretchedExp
    int      inflection = 50;     // frame at sigmoid midpoint for TlFn::Logistic
    uint64_t floor_ms   = 2000;
    uint64_t ceil_ms    = 300000;
};

// Parse a function name string.  Aliases "quadratic", "cubic", "quintic" are
// accepted and set params.power to the corresponding exponent.
// Returns TlFn::Linear on unknown input (safe default).
inline TlFn parse_tl_fn(const std::string& name, TlParams& params) {
    if (name == "linear")        return TlFn::Linear;
    if (name == "exp_grow")      return TlFn::ExpGrow;
    if (name == "exp_decay")     return TlFn::ExpDecay;
    if (name == "log")           return TlFn::Log;
    if (name == "power")         return TlFn::Power;
    if (name == "quadratic")   { params.power = 2.0f; return TlFn::Power; }
    if (name == "cubic")       { params.power = 3.0f; return TlFn::Power; }
    if (name == "quintic")     { params.power = 5.0f; return TlFn::Power; }
    if (name == "michaelis")     return TlFn::MichaelisMenten;
    if (name == "logistic")      return TlFn::Logistic;
    if (name == "stretched_exp") return TlFn::StretchedExp;
    if (name == "hyperbolic")    return TlFn::Hyperbolic;
    return TlFn::Linear;
}

inline std::string tl_fn_name(TlFn fn) {
    switch (fn) {
        case TlFn::Linear:         return "linear";
        case TlFn::ExpGrow:        return "exp_grow";
        case TlFn::ExpDecay:       return "exp_decay";
        case TlFn::Log:            return "log";
        case TlFn::Power:          return "power";
        case TlFn::MichaelisMenten:return "michaelis";
        case TlFn::Logistic:       return "logistic";
        case TlFn::StretchedExp:   return "stretched_exp";
        case TlFn::Hyperbolic:     return "hyperbolic";
    }
    return "linear";
}

// Compute the interval before the (n+1)-th capture.
// Result is always in [floor_ms, ceil_ms].
// n must be >= 0 (0 = before first capture).
inline uint64_t next_interval(int n, TlFn fn, const TlParams& p) {
    const double B = static_cast<double>(p.base_ms);
    const double k = static_cast<double>(p.k);
    const double C = static_cast<double>(p.ceil_ms);
    const double F = static_cast<double>(p.floor_ms);
    const double dn = static_cast<double>(n);

    double raw = B;
    switch (fn) {
        case TlFn::Linear:
            raw = B;
            break;
        case TlFn::ExpGrow:
            raw = B * std::exp(k * dn);
            break;
        case TlFn::ExpDecay:
            raw = B * std::exp(-k * dn);
            break;
        case TlFn::Log:
            raw = B + k * std::log(dn + 1.0);
            break;
        case TlFn::Power: {
            const double pw = static_cast<double>(p.power);
            raw = B + k * std::pow(dn, pw);
            break;
        }
        case TlFn::MichaelisMenten:
            // MM saturation: starts at B, approaches C asymptotically.
            // kn/(1+kn) = 0 at n=0, → 1 as n→∞.
            raw = B + (C - B) * (k * dn) / (1.0 + k * dn);
            break;
        case TlFn::Logistic: {
            // Sigmoid centred at inflection frame m.
            // I(0) ≈ B when m >> 1/k (approaches B from above).
            const double m = static_cast<double>(p.inflection);
            const double sig = 1.0 / (1.0 + std::exp(-k * (dn - m)));
            raw = B + (C - B) * sig;
            break;
        }
        case TlFn::StretchedExp: {
            // KWW: I(n) = B · exp(k · n^β).  I(0) = B · exp(0) = B.
            const double beta = static_cast<double>(p.beta);
            raw = B * std::exp(k * std::pow(dn, beta));
            break;
        }
        case TlFn::Hyperbolic:
            raw = B / (1.0 + k * dn);
            break;
    }

    // Clamp to [floor_ms, ceil_ms].
    const double clamped = std::max(F, std::min(C, raw));
    return static_cast<uint64_t>(std::round(clamped));
}
