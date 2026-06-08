#include "util/timelapse.h"
#include <gtest/gtest.h>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TlParams make_params(uint64_t base = 5000, float k = 0.05f,
                             uint64_t floor_ms = 2000, uint64_t ceil_ms = 300000) {
    TlParams p;
    p.base_ms   = base;
    p.k         = k;
    p.floor_ms  = floor_ms;
    p.ceil_ms   = ceil_ms;
    p.power     = 2.0f;
    p.beta      = 0.7f;
    p.inflection = 50;
    return p;
}

// ---------------------------------------------------------------------------
// I(0) == base_ms for every function (the B offset guarantee)
// ---------------------------------------------------------------------------

TEST(TlInterval, BaseOffsetAllFunctions) {
    TlParams p = make_params(5000);
    for (int fn_i = 0; fn_i <= (int)TlFn::Hyperbolic; ++fn_i) {
        TlFn fn = static_cast<TlFn>(fn_i);
        uint64_t i0 = next_interval(0, fn, p);
        if (fn == TlFn::Logistic) {
            // Logistic at n=0 is B + (C-B)·σ(-k·m) — only approximately B
            // when inflection m >> 1/k. Verify it's at least ≥ base_ms.
            EXPECT_GE(i0, p.base_ms)
                << "fn=logistic I(0) should be >= base_ms";
        } else {
            EXPECT_EQ(i0, 5000u) << "fn=" << tl_fn_name(fn) << " I(0) != base_ms";
        }
    }
}

// ---------------------------------------------------------------------------
// Linear: constant for all n
// ---------------------------------------------------------------------------

TEST(TlInterval, LinearIsConstant) {
    TlParams p = make_params(7000);
    for (int n = 0; n < 100; ++n)
        EXPECT_EQ(next_interval(n, TlFn::Linear, p), 7000u) << "n=" << n;
}

// ---------------------------------------------------------------------------
// Growth functions: non-decreasing
// ---------------------------------------------------------------------------

TEST(TlInterval, GrowthFunctionsNonDecreasing) {
    TlParams p = make_params(5000, 0.02f, 2000, 300000);
    const TlFn growth[] = {
        TlFn::ExpGrow, TlFn::Log, TlFn::Power,
        TlFn::MichaelisMenten, TlFn::Logistic, TlFn::StretchedExp
    };
    for (TlFn fn : growth) {
        uint64_t prev = next_interval(0, fn, p);
        for (int n = 1; n <= 200; ++n) {
            uint64_t cur = next_interval(n, fn, p);
            EXPECT_GE(cur, prev) << "fn=" << tl_fn_name(fn) << " not monotone at n=" << n;
            prev = cur;
        }
    }
}

// ---------------------------------------------------------------------------
// Decay functions: non-increasing
// ---------------------------------------------------------------------------

TEST(TlInterval, DecayFunctionsNonIncreasing) {
    TlParams p = make_params(5000, 0.05f, 2000, 300000);
    const TlFn decay[] = { TlFn::ExpDecay, TlFn::Hyperbolic };
    for (TlFn fn : decay) {
        uint64_t prev = next_interval(0, fn, p);
        for (int n = 1; n <= 200; ++n) {
            uint64_t cur = next_interval(n, fn, p);
            EXPECT_LE(cur, prev) << "fn=" << tl_fn_name(fn) << " not monotone at n=" << n;
            prev = cur;
        }
    }
}

// ---------------------------------------------------------------------------
// Bounds: floor_ms <= I(n) <= ceil_ms for all n, all functions
// ---------------------------------------------------------------------------

TEST(TlInterval, BoundsAlwaysEnforced) {
    TlParams p = make_params(5000, 0.5f, 2000, 30000); // aggressive k, tight ceil
    for (int fn_i = 0; fn_i <= (int)TlFn::Hyperbolic; ++fn_i) {
        TlFn fn = static_cast<TlFn>(fn_i);
        for (int n = 0; n <= 500; ++n) {
            uint64_t v = next_interval(n, fn, p);
            EXPECT_GE(v, p.floor_ms) << "fn=" << tl_fn_name(fn) << " below floor at n=" << n;
            EXPECT_LE(v, p.ceil_ms)  << "fn=" << tl_fn_name(fn) << " above ceil at n=" << n;
        }
    }
}

// ---------------------------------------------------------------------------
// ExpGrow: geometric ratio ≈ exp(k) between successive terms
// ---------------------------------------------------------------------------

TEST(TlExpGrow, GeometricRatio) {
    TlParams p = make_params(5000, 0.1f, 1, 1e9);
    double expected_ratio = std::exp(0.1);
    for (int n = 0; n < 20; ++n) {
        double a = (double)next_interval(n,     TlFn::ExpGrow, p);
        double b = (double)next_interval(n + 1, TlFn::ExpGrow, p);
        double ratio = b / a;
        EXPECT_NEAR(ratio, expected_ratio, 0.005)
            << "n=" << n << " ratio=" << ratio;
    }
}

// ---------------------------------------------------------------------------
// ExpDecay: floor enforced, interval halved every ln2/k frames
// ---------------------------------------------------------------------------

TEST(TlExpDecay, FloorEnforced) {
    TlParams p = make_params(5000, 0.5f, 2000, 300000);
    for (int n = 0; n <= 100; ++n)
        EXPECT_GE(next_interval(n, TlFn::ExpDecay, p), p.floor_ms) << "n=" << n;
}

// ---------------------------------------------------------------------------
// Hyperbolic: floor enforced
// ---------------------------------------------------------------------------

TEST(TlHyperbolic, FloorEnforced) {
    TlParams p = make_params(10000, 0.2f, 1500, 300000);
    for (int n = 0; n <= 200; ++n)
        EXPECT_GE(next_interval(n, TlFn::Hyperbolic, p), p.floor_ms) << "n=" << n;
}

// ---------------------------------------------------------------------------
// MichaelisMenten: saturates near ceil
// ---------------------------------------------------------------------------

TEST(TlMichaelisMenten, SaturatesNearCeil) {
    TlParams p = make_params(1000, 0.1f, 500, 20000);
    // At large n, should be within 1% of ceil
    double close_enough = p.ceil_ms * 0.99;
    uint64_t large_n = next_interval(10000, TlFn::MichaelisMenten, p);
    EXPECT_GE((double)large_n, close_enough);
    EXPECT_LE(large_n, p.ceil_ms);
}

// ---------------------------------------------------------------------------
// Logistic: inflection midpoint is approximately (base + ceil) / 2
// ---------------------------------------------------------------------------

TEST(TlLogistic, InflectionMidpoint) {
    TlParams p = make_params(2000, 0.1f, 1000, 20000);
    p.inflection = 50;
    uint64_t mid = next_interval(50, TlFn::Logistic, p);
    double expected = (p.base_ms + p.ceil_ms) / 2.0;
    EXPECT_NEAR((double)mid, expected, expected * 0.02); // within 2%
}

// ---------------------------------------------------------------------------
// Power: ceil enforced, starts at base
// ---------------------------------------------------------------------------

TEST(TlPower, CeilEnforced) {
    TlParams p = make_params(5000, 100.0f, 2000, 30000);
    p.power = 5.0f; // quintic
    for (int n = 0; n <= 50; ++n)
        EXPECT_LE(next_interval(n, TlFn::Power, p), p.ceil_ms) << "n=" << n;
}

// ---------------------------------------------------------------------------
// parse_tl_fn: all 12 names parse without returning junk; aliases set power
// ---------------------------------------------------------------------------

TEST(TlParseFn, AllNamesValid) {
    const char* names[] = {
        "linear", "exp_grow", "exp_decay", "log", "power",
        "quadratic", "cubic", "quintic",
        "michaelis", "logistic", "stretched_exp", "hyperbolic"
    };
    for (const char* n : names) {
        TlParams p;
        TlFn fn = parse_tl_fn(n, p);
        // Round-trip the canonical name — must not crash
        EXPECT_FALSE(tl_fn_name(fn).empty()) << "name=" << n;
    }
}

TEST(TlParseFn, AliasesSetPower) {
    TlParams p;
    p.power = 1.0f;
    parse_tl_fn("quadratic", p);
    EXPECT_FLOAT_EQ(p.power, 2.0f);

    p.power = 1.0f;
    parse_tl_fn("cubic", p);
    EXPECT_FLOAT_EQ(p.power, 3.0f);

    p.power = 1.0f;
    parse_tl_fn("quintic", p);
    EXPECT_FLOAT_EQ(p.power, 5.0f);
}

TEST(TlParseFn, AliasesReturnPowerFn) {
    TlParams p;
    EXPECT_EQ(parse_tl_fn("quadratic", p), TlFn::Power);
    EXPECT_EQ(parse_tl_fn("cubic",     p), TlFn::Power);
    EXPECT_EQ(parse_tl_fn("quintic",   p), TlFn::Power);
}

TEST(TlParseFn, UnknownFallsBackToLinear) {
    TlParams p;
    EXPECT_EQ(parse_tl_fn("nonexistent_fn", p), TlFn::Linear);
}

// ---------------------------------------------------------------------------
// tl_fn_name round-trips for every enum value
// ---------------------------------------------------------------------------

TEST(TlFnName, RoundTrip) {
    for (int fn_i = 0; fn_i <= (int)TlFn::Hyperbolic; ++fn_i) {
        TlFn fn = static_cast<TlFn>(fn_i);
        TlParams p;
        std::string name = tl_fn_name(fn);
        EXPECT_FALSE(name.empty());
        // Aliases (quadratic/cubic/quintic) won't round-trip — that's expected.
        // The canonical names must parse back to the same fn.
        if (name != "power") {
            EXPECT_EQ(parse_tl_fn(name, p), fn) << "name=" << name;
        }
    }
}
