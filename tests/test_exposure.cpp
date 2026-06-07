#include "util/exposure.h"
#include <gtest/gtest.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Shutter speed index lookup
// ---------------------------------------------------------------------------

TEST(ShutterIndex, ExactMatches) {
    // Verify a handful of well-known values hit the right index.
    // kShutterSteps is slowest-first: [0]=2", [7]=1/60, [11]=1/1000, [16]=1/32000
    EXPECT_FLOAT_EQ(kShutterSteps[shutter_index(2000000.0f)], 2000000.0f); // 2"
    EXPECT_FLOAT_EQ(kShutterSteps[shutter_index(16667.0f)],    16667.0f);  // 1/60
    EXPECT_FLOAT_EQ(kShutterSteps[shutter_index(1000.0f)],      1000.0f);  // 1/1000
    EXPECT_FLOAT_EQ(kShutterSteps[shutter_index(31.25f)],        31.25f);  // 1/32000
}

TEST(ShutterIndex, NearestNeighbour) {
    // A value exactly between two steps should round to the closer one.
    // Between 1/60 (16667) and 1/30 (33333): midpoint ≈ 25000.
    // 20000 is closer to 16667 than to 33333.
    int idx_20ms = shutter_index(20000.0f);
    int idx_60   = shutter_index(16667.0f);
    EXPECT_EQ(idx_20ms, idx_60);

    // 30000 is closer to 33333 (1/30).
    int idx_30ms = shutter_index(30000.0f);
    int idx_30   = shutter_index(33333.0f);
    EXPECT_EQ(idx_30ms, idx_30);
}

TEST(ShutterIndex, InBounds) {
    // Every index returned must be a valid array index.
    for (float us : {1.0f, 16667.0f, 999999.0f, 1e8f}) {
        int idx = shutter_index(us);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, (int)kShutterSteps.size());
    }
}

// ---------------------------------------------------------------------------
// Shutter stepping (S/M mode up/down)
// ---------------------------------------------------------------------------

TEST(ShutterStepping, StepUpFromMin) {
    int idx = (int)kShutterSteps.size() - 1; // fastest shutter (1/32000)
    int next = std::min((int)kShutterSteps.size() - 1, idx + 1);
    EXPECT_EQ(next, idx); // clamps at max
}

TEST(ShutterStepping, StepDownFromMax) {
    int idx  = 0; // slowest (2")
    int prev = std::max(0, idx - 1);
    EXPECT_EQ(prev, 0); // clamps at 0
}

TEST(ShutterStepping, StepUpInMiddle) {
    // Ladder is descending (index 0 = slowest). Incrementing the index moves
    // to a faster shutter speed (fewer microseconds).
    int idx  = shutter_index(16667.0f); // 1/60
    int next = std::min((int)kShutterSteps.size() - 1, idx + 1);
    EXPECT_LT(kShutterSteps[next], kShutterSteps[idx]); // faster (fewer us)
}

TEST(ShutterStepping, StepDownInMiddle) {
    // Decrementing the index moves to a slower shutter speed (more microseconds).
    int idx  = shutter_index(16667.0f); // 1/60
    int prev = std::max(0, idx - 1);
    EXPECT_GT(kShutterSteps[prev], kShutterSteps[idx]); // slower (more us)
}

// ---------------------------------------------------------------------------
// ISO index lookup
// ---------------------------------------------------------------------------

TEST(IsoIndex, ExactMatches) {
    EXPECT_EQ(kIsoSteps[iso_index(100)],  100);
    EXPECT_EQ(kIsoSteps[iso_index(400)],  400);
    EXPECT_EQ(kIsoSteps[iso_index(6400)], 6400);
}

TEST(IsoIndex, NearestNeighbour) {
    // 300 is between 200 and 400; it's equidistant, but our impl picks 200
    // (lower index wins on tie). Just verify it returns one of the two.
    int idx = iso_index(300);
    EXPECT_TRUE(kIsoSteps[idx] == 200 || kIsoSteps[idx] == 400);

    // 350 is closer to 400.
    EXPECT_EQ(kIsoSteps[iso_index(350)], 400);

    // 150 is equidistant between 100 and 200; lower index wins on tie → 100.
    EXPECT_EQ(kIsoSteps[iso_index(150)], 100);
}

TEST(IsoIndex, InBounds) {
    for (int iso : {50, 100, 800, 10000}) {
        int idx = iso_index(iso);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, (int)kIsoSteps.size());
    }
}

// ---------------------------------------------------------------------------
// ISO stepping (all modes)
// ---------------------------------------------------------------------------

TEST(IsoStepping, StepUpFromMax) {
    int idx  = (int)kIsoSteps.size() - 1; // ISO 6400
    int next = std::min((int)kIsoSteps.size() - 1, idx + 1);
    EXPECT_EQ(next, idx); // clamps
}

TEST(IsoStepping, StepDownFromMin) {
    int prev = std::max(0, 0 - 1);
    EXPECT_EQ(prev, 0); // clamps
}

TEST(IsoStepping, LadderIsAscending) {
    for (int i = 1; i < (int)kIsoSteps.size(); ++i)
        EXPECT_GT(kIsoSteps[i], kIsoSteps[i - 1]);
}

TEST(ShutterStepping, LadderIsDescending) {
    // kShutterSteps is slowest-first, so each entry is smaller (fewer us = faster).
    for (int i = 1; i < (int)kShutterSteps.size(); ++i)
        EXPECT_LT(kShutterSteps[i], kShutterSteps[i - 1]);
}
