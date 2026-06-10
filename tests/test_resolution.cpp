#include <gtest/gtest.h>
#include "util/resolution.h"
#include <vector>

// ---------------------------------------------------------------------------
// aspect_str
// ---------------------------------------------------------------------------

TEST(AspectStr, Standard16x9)  { EXPECT_EQ(aspect_str(1920, 1080), "16:9"); }
TEST(AspectStr, Standard16x10) { EXPECT_EQ(aspect_str(1920, 1200), "16:10"); }
TEST(AspectStr, Standard4x3)   { EXPECT_EQ(aspect_str(1920, 1440), "4:3"); }
TEST(AspectStr, Small16x9)     { EXPECT_EQ(aspect_str(1280,  720), "16:9"); }
TEST(AspectStr, Small16x10)    { EXPECT_EQ(aspect_str(1280,  800), "16:10"); }
TEST(AspectStr, Small4x3)      { EXPECT_EQ(aspect_str(1024,  768), "4:3"); }
TEST(AspectStr, Tiny4x3)       { EXPECT_EQ(aspect_str( 640,  480), "4:3"); }

// ---------------------------------------------------------------------------
// select_from_modes
// ---------------------------------------------------------------------------

static const Resolution kFallback{640, 480};

TEST(SelectFromModes, PicksHighestPixelCount) {
    std::vector<Resolution> modes = {{1920,1080},{1280,720},{1024,576},{854,480}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1920,1080}));
}

TEST(SelectFromModes, RespectsMaxWidth) {
    std::vector<Resolution> modes = {{2560,1440},{1920,1080},{1280,720}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1920,1080}));
}

TEST(SelectFromModes, RespectsMinHeight) {
    // 640×360 excluded (h < 480); 854×480 and 1280×720 pass
    std::vector<Resolution> modes = {{1280,720},{854,480},{640,360}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1280,720}));
}

TEST(SelectFromModes, MinHeightBoundary) {
    // Only 854×480 qualifies (640×360 is below kMinHeight)
    std::vector<Resolution> modes = {{854,480},{640,360}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{854,480}));
}

TEST(SelectFromModes, ArFilter16x9Rejects16x10) {
    // 1920×1200 is 16:10, must be rejected when target is 16:9
    std::vector<Resolution> modes = {{1920,1200},{1920,1080}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1920,1080}));
}

TEST(SelectFromModes, ArFilter16x10Selects1920x1200) {
    std::vector<Resolution> modes = {{1920,1200},{1920,1080}};
    EXPECT_EQ(select_from_modes(modes, 16/10.0f, kFallback), (Resolution{1920,1200}));
}

TEST(SelectFromModes, ArFilter4x3) {
    std::vector<Resolution> modes = {{1920,1440},{1024,768},{640,480}};
    EXPECT_EQ(select_from_modes(modes, 4/3.0f, kFallback), (Resolution{1920,1440}));
}

TEST(SelectFromModes, FallbackWhenAllTooWide) {
    std::vector<Resolution> modes = {{2560,1440},{3840,2160}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), kFallback);
}

TEST(SelectFromModes, FallbackOnEmpty) {
    EXPECT_EQ(select_from_modes({}, 16/9.0f, kFallback), kFallback);
}

TEST(SelectFromModes, Near16x9Within2Pct) {
    // 854×480 = 1.7792 — within 2% of 16/9 = 1.7778
    std::vector<Resolution> modes = {{854,480}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{854,480}));
}

TEST(SelectFromModes, 1366x768Passes16x9) {
    // 1366×768 = 1.7786 — within 2% of 16/9
    std::vector<Resolution> modes = {{1366,768}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1366,768}));
}

TEST(SelectFromModes, ExactMaxWidthAllowed) {
    // 1920 wide should pass (≤ kMaxWidth)
    std::vector<Resolution> modes = {{1920,1080}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{1920,1080}));
}

TEST(SelectFromModes, ExactMinHeightAllowed) {
    // 480 tall should pass (≥ kMinHeight)
    std::vector<Resolution> modes = {{854,480}};
    EXPECT_EQ(select_from_modes(modes, 16/9.0f, kFallback), (Resolution{854,480}));
}
