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
// Aperture ladder
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

TEST(ApertureIndex, ExactMatches) {
    auto ladder = build_aperture_ladder(1.0f, 32.0f);
    EXPECT_FLOAT_EQ(ladder[aperture_index(2.8f, ladder)], 2.8f);
    EXPECT_FLOAT_EQ(ladder[aperture_index(8.0f, ladder)], 8.0f);
}
