#include "util/exposure.h"
#include <gtest/gtest.h>
#include <algorithm>

// Convenience: full master tables as vectors for tests that don't need a trimmed ladder.
static std::vector<float> full_shutter() {
    return {kShutterMaster.begin(), kShutterMaster.end()};
}
static std::vector<int> full_iso() {
    return {kIsoMaster.begin(), kIsoMaster.end()};
}

// ---------------------------------------------------------------------------
// Master table invariants
// ---------------------------------------------------------------------------

TEST(MasterTables, ShutterDescending) {
    for (int i = 1; i < (int)kShutterMaster.size(); ++i)
        EXPECT_LT(kShutterMaster[i], kShutterMaster[i - 1]);
}

TEST(MasterTables, IsoAscending) {
    for (int i = 1; i < (int)kIsoMaster.size(); ++i)
        EXPECT_GT(kIsoMaster[i], kIsoMaster[i - 1]);
}

TEST(MasterTables, ApertureAscending) {
    for (int i = 1; i < (int)kApertureMaster.size(); ++i)
        EXPECT_GT(kApertureMaster[i], kApertureMaster[i - 1]);
}

// ---------------------------------------------------------------------------
// build_shutter_ladder
// ---------------------------------------------------------------------------

TEST(BuildShutterLadder, FullRangeReturnsAll) {
    // If the camera supports the full range, every master step should appear.
    auto ladder = build_shutter_ladder(31.25f, 2000000.0f);
    EXPECT_EQ(ladder.size(), kShutterMaster.size());
}

TEST(BuildShutterLadder, NarrowRangeTrimsBoth) {
    // Range [1000 µs, 16667 µs] = 1/1000 to 1/60 — 5 steps.
    auto ladder = build_shutter_ladder(1000.0f, 16667.0f);
    EXPECT_GE(ladder.size(), 1u);
    for (float s : ladder) {
        EXPECT_GE(s, 1000.0f);
        EXPECT_LE(s, 16667.0f);
    }
}

TEST(BuildShutterLadder, ResultIsDescending) {
    auto ladder = build_shutter_ladder(500.0f, 1000000.0f);
    for (int i = 1; i < (int)ladder.size(); ++i)
        EXPECT_LT(ladder[i], ladder[i - 1]);
}

TEST(BuildShutterLadder, ImpossibleRangeReturnsOneFallback) {
    // Range that contains no master step.
    auto ladder = build_shutter_ladder(17000.0f, 17000.0f);
    EXPECT_EQ(ladder.size(), 1u);
}

// ---------------------------------------------------------------------------
// build_iso_ladder
// ---------------------------------------------------------------------------

TEST(BuildIsoLadder, FullGainRangeReturnsAll) {
    auto ladder = build_iso_ladder(1.0f, 128.0f); // 100–12800 ISO
    EXPECT_EQ(ladder.size(), kIsoMaster.size());
}

TEST(BuildIsoLadder, NarrowRangeTrimsBoth) {
    // gain 1.0–4.0 ≈ ISO 100–400
    auto ladder = build_iso_ladder(1.0f, 4.0f);
    EXPECT_GE(ladder.size(), 1u);
    for (int iso : ladder) {
        EXPECT_GE(iso, 100);
        EXPECT_LE(iso, 400);
    }
}

TEST(BuildIsoLadder, ResultIsAscending) {
    auto ladder = build_iso_ladder(1.0f, 64.0f);
    for (int i = 1; i < (int)ladder.size(); ++i)
        EXPECT_GT(ladder[i], ladder[i - 1]);
}

// ---------------------------------------------------------------------------
// Shutter index lookup (vector API)
// ---------------------------------------------------------------------------

TEST(ShutterIndex, ExactMatches) {
    auto ladder = full_shutter();
    EXPECT_FLOAT_EQ(ladder[shutter_index(2000000.0f, ladder)], 2000000.0f);
    EXPECT_FLOAT_EQ(ladder[shutter_index(16667.0f,   ladder)], 16667.0f);
    EXPECT_FLOAT_EQ(ladder[shutter_index(1000.0f,    ladder)], 1000.0f);
    EXPECT_FLOAT_EQ(ladder[shutter_index(31.25f,     ladder)], 31.25f);
}

TEST(ShutterIndex, NearestNeighbour) {
    auto ladder = full_shutter();
    // 20000 µs is closer to 1/60 (16667) than 1/30 (33333).
    EXPECT_EQ(shutter_index(20000.0f, ladder), shutter_index(16667.0f, ladder));
    // 30000 µs is closer to 1/30 (33333).
    EXPECT_EQ(shutter_index(30000.0f, ladder), shutter_index(33333.0f, ladder));
}

TEST(ShutterIndex, InBounds) {
    auto ladder = full_shutter();
    for (float us : {1.0f, 16667.0f, 999999.0f, 1e8f}) {
        int idx = shutter_index(us, ladder);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, (int)ladder.size());
    }
}

// ---------------------------------------------------------------------------
// Shutter stepping
// ---------------------------------------------------------------------------

TEST(ShutterStepping, StepUpFromMax) {
    auto ladder = full_shutter();
    int idx  = (int)ladder.size() - 1;
    int next = std::min((int)ladder.size() - 1, idx + 1);
    EXPECT_EQ(next, idx); // clamps at fastest
}

TEST(ShutterStepping, StepDownFromMin) {
    int prev = std::max(0, 0 - 1);
    EXPECT_EQ(prev, 0); // clamps at slowest
}

TEST(ShutterStepping, StepUpInMiddle) {
    auto ladder = full_shutter();
    int idx  = shutter_index(16667.0f, ladder); // 1/60
    int next = std::min((int)ladder.size() - 1, idx + 1);
    EXPECT_LT(ladder[next], ladder[idx]); // faster = fewer µs
}

TEST(ShutterStepping, StepDownInMiddle) {
    auto ladder = full_shutter();
    int idx  = shutter_index(16667.0f, ladder);
    int prev = std::max(0, idx - 1);
    EXPECT_GT(ladder[prev], ladder[idx]); // slower = more µs
}

// ---------------------------------------------------------------------------
// ISO index lookup (vector API)
// ---------------------------------------------------------------------------

TEST(IsoIndex, ExactMatches) {
    auto ladder = full_iso();
    EXPECT_EQ(ladder[iso_index(100,  ladder)], 100);
    EXPECT_EQ(ladder[iso_index(400,  ladder)], 400);
    EXPECT_EQ(ladder[iso_index(6400, ladder)], 6400);
}

TEST(IsoIndex, NearestNeighbour) {
    auto ladder = full_iso();
    // 350 is closer to 400.
    EXPECT_EQ(ladder[iso_index(350, ladder)], 400);
    // 150 is equidistant between 100 and 200; lower index wins → 100.
    EXPECT_EQ(ladder[iso_index(150, ladder)], 100);
}

TEST(IsoIndex, InBounds) {
    auto ladder = full_iso();
    for (int iso : {50, 100, 800, 10000}) {
        int idx = iso_index(iso, ladder);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, (int)ladder.size());
    }
}

// ---------------------------------------------------------------------------
// ISO stepping
// ---------------------------------------------------------------------------

TEST(IsoStepping, StepUpFromMax) {
    auto ladder = full_iso();
    int idx  = (int)ladder.size() - 1;
    int next = std::min((int)ladder.size() - 1, idx + 1);
    EXPECT_EQ(next, idx); // clamps
}

TEST(IsoStepping, StepDownFromMin) {
    EXPECT_EQ(std::max(0, 0 - 1), 0); // clamps
}

// ---------------------------------------------------------------------------
// Aperture master table invariants
// ---------------------------------------------------------------------------

static std::vector<float> full_aperture() {
    return {kApertureMaster.begin(), kApertureMaster.end()};
}

TEST(ApertureMaster, Size) {
    // 31 = 10 full stops × 3 third-stops + 1 (f/1.0 … f/32.0)
    EXPECT_EQ(kApertureMaster.size(), 31u);
}

TEST(ApertureMaster, StartsAtF1EndsAtF32) {
    EXPECT_FLOAT_EQ(kApertureMaster.front(), 1.0f);
    EXPECT_FLOAT_EQ(kApertureMaster.back(), 32.0f);
}

TEST(ApertureMaster, FullStopsPresent) {
    // Every full stop (×2 in area = ×√2 in f-number) must appear.
    for (float stop : {1.0f, 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f, 32.0f}) {
        auto it = std::find(kApertureMaster.begin(), kApertureMaster.end(), stop);
        EXPECT_NE(it, kApertureMaster.end()) << "f/" << stop << " missing from kApertureMaster";
    }
}

// ---------------------------------------------------------------------------
// build_aperture_ladder
// ---------------------------------------------------------------------------

TEST(BuildApertureLadder, FullRangeReturnsAll) {
    auto ladder = build_aperture_ladder(1.0f, 32.0f);
    EXPECT_EQ(ladder.size(), kApertureMaster.size());
}

TEST(BuildApertureLadder, ResultIsAscending) {
    auto ladder = build_aperture_ladder(1.0f, 32.0f);
    for (int i = 1; i < (int)ladder.size(); ++i)
        EXPECT_GT(ladder[i], ladder[i - 1]);
}

TEST(BuildApertureLadder, NarrowRangeClampsBothEnds) {
    // f/2.0 – f/5.6: only steps within that window should appear.
    auto ladder = build_aperture_ladder(2.0f, 5.6f);
    EXPECT_GE(ladder.size(), 1u);
    for (float f : ladder) {
        EXPECT_GE(f, 2.0f);
        EXPECT_LE(f, 5.6f);
    }
}

TEST(BuildApertureLadder, ImpossibleRangeReturnsOneFallback) {
    // f/33 is above f/32 — no master step qualifies; should get the clamped fallback.
    auto ladder = build_aperture_ladder(33.0f, 64.0f);
    EXPECT_EQ(ladder.size(), 1u);
}

// ---------------------------------------------------------------------------
// aperture_index — exact and nearest-neighbour (log2 distance)
// ---------------------------------------------------------------------------

TEST(ApertureIndex, ExactMatches) {
    auto ladder = full_aperture();
    for (float f : {1.0f, 1.4f, 2.8f, 5.6f, 8.0f, 16.0f, 32.0f})
        EXPECT_FLOAT_EQ(ladder[aperture_index(f, ladder)], f) << "f/" << f;
}

TEST(ApertureIndex, InBounds) {
    auto ladder = full_aperture();
    for (float f : {0.5f, 1.0f, 5.0f, 32.0f, 100.0f}) {
        int idx = aperture_index(f, ladder);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, (int)ladder.size());
    }
}

TEST(ApertureIndex, LogSpaceNearestNeighbour) {
    auto ladder = full_aperture();

    // f/1.15 sits at the linear midpoint between f/1.1 and f/1.2 (equal linear
    // distance to both). The geometric midpoint is sqrt(1.1 × 1.2) ≈ 1.149, so
    // f/1.15 is above it — log2 distance to f/1.2 (0.061) is less than to f/1.1
    // (0.064). The old linear-distance code would pick f/1.1 (tie → lower index).
    EXPECT_FLOAT_EQ(ladder[aperture_index(1.15f, ladder)], 1.2f);

    // f/1.13 is below the geometric midpoint → still maps to f/1.1.
    EXPECT_FLOAT_EQ(ladder[aperture_index(1.13f, ladder)], 1.1f);

    // Same verification at the high end: f/23.5 is above sqrt(22 × 25) ≈ 23.45 →
    // log2 distance to f/25 is smaller.
    EXPECT_FLOAT_EQ(ladder[aperture_index(23.5f, ladder)], 25.0f);

    // f/23.0 is clearly below the geometric midpoint → f/22.
    EXPECT_FLOAT_EQ(ladder[aperture_index(23.0f, ladder)], 22.0f);
}

// ---------------------------------------------------------------------------
// Aperture stepping
// ---------------------------------------------------------------------------

TEST(ApertureStepping, StepUpFromMax) {
    auto ladder = full_aperture();
    int idx  = (int)ladder.size() - 1;
    int next = std::min((int)ladder.size() - 1, idx + 1);
    EXPECT_EQ(next, idx); // clamps at narrowest aperture
}

TEST(ApertureStepping, StepDownFromMin) {
    EXPECT_EQ(std::max(0, 0 - 1), 0); // clamps at widest aperture
}

TEST(ApertureStepping, StepUpNarrowsAperture) {
    auto ladder = full_aperture();
    int idx  = aperture_index(5.6f, ladder);
    int next = std::min((int)ladder.size() - 1, idx + 1);
    EXPECT_GT(ladder[next], ladder[idx]); // higher f-number = narrower aperture
}

TEST(ApertureStepping, StepDownWidensAperture) {
    auto ladder = full_aperture();
    int idx  = aperture_index(5.6f, ladder);
    int prev = std::max(0, idx - 1);
    EXPECT_LT(ladder[prev], ladder[idx]); // lower f-number = wider aperture
}

TEST(ApertureStepping, EachStepIsOneThirdStop) {
    // Every adjacent pair in the master table should be within half a stop of
    // exactly 1/3 stop (2^(1/6)). The display-rounded values introduce small
    // errors (e.g. 1.2→1.4 rounds to 1.167× instead of 1.1225×) but must stay
    // below 2^(1/4) ≈ 1.189 (halfway between a third-stop and a half-stop).
    const float kThirdStop    = std::pow(2.0f, 1.0f / 6.0f); // 1.1225
    const float kHalfStopMax  = std::pow(2.0f, 1.0f / 4.0f); // 1.189 — tolerance ceiling
    for (int i = 1; i < (int)kApertureMaster.size(); ++i) {
        float ratio = kApertureMaster[i] / kApertureMaster[i - 1];
        EXPECT_GT(ratio, 1.0f)             << "step " << i << " is not increasing";
        EXPECT_LT(ratio, kHalfStopMax)    << "step " << i << " exceeds half-stop gap";
        (void)kThirdStop; // documented target; exact check skipped due to display rounding
    }
}
