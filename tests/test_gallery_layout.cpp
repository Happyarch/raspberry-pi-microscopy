//
// Tests for gallery_compute_layout() — the pure-integer tile geometry function.
// gallery_render/gallery_load_page (which need SDL2 runtime) are not exercised.
//
#include "ui/gallery.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Helper: call gallery_compute_layout with a default-initialised state.
// ---------------------------------------------------------------------------

static GalleryState layout(int dw, int dh) {
    GalleryState s;
    gallery_compute_layout(s, dw, dh);
    return s;
}

// ---------------------------------------------------------------------------
// Invariants that must hold for every display size
// ---------------------------------------------------------------------------

static void check_invariants(const GalleryState& s, int dw, int dh, const char* label) {
    SCOPED_TRACE(label);
    EXPECT_GE(s.tiles_per_row, 1)    << "tiles_per_row must be >= 1";
    EXPECT_GE(s.rows_visible,  1)    << "rows_visible must be >= 1";
    EXPECT_GE(s.tile_w,        1)    << "tile_w must be >= 1";
    EXPECT_GE(s.tile_h,        1)    << "tile_h must be >= 1";
    EXPECT_EQ(s.tile_w, s.tile_h)    << "tiles must be square";
    // Tiles must fit horizontally (integer division means they may not fill exactly,
    // but they must not overflow).
    EXPECT_LE(s.tiles_per_row * s.tile_w, dw + s.tiles_per_row)
        << "tiles overflow display width";
    (void)dh; // rows_visible bound checked via tile_h > 0 + rows_visible >= 1
}

// ---------------------------------------------------------------------------
// Common display resolutions
// ---------------------------------------------------------------------------

TEST(GalleryLayout, FullHD_1920x1080) {
    auto s = layout(1920, 1080);
    check_invariants(s, 1920, 1080, "1920x1080");
    // At 1080p: tile_w = 1920/5 = 384, tiles_per_row = 5
    EXPECT_EQ(s.tiles_per_row, 5);
    EXPECT_EQ(s.tile_w,        384);
    EXPECT_EQ(s.tile_h,        384);
    // tab_h = max(28, 1080/14) = 77; usable_h = 1080 - 154 = 926; rows = 926/384 = 2
    EXPECT_EQ(s.rows_visible,  2);
}

TEST(GalleryLayout, HD_1280x720) {
    auto s = layout(1280, 720);
    check_invariants(s, 1280, 720, "1280x720");
    EXPECT_EQ(s.tiles_per_row, 5);
    EXPECT_EQ(s.tile_w,        256);
    EXPECT_EQ(s.tile_h,        256);
    // tab_h = max(28, 51) = 51; usable_h = 618; rows = 618/256 = 2
    EXPECT_EQ(s.rows_visible, 2);
}

TEST(GalleryLayout, SVGA_800x600) {
    auto s = layout(800, 600);
    check_invariants(s, 800, 600, "800x600");
    // tile_w = 800/5 = 160; tiles_per_row = 5; tab_h = max(28,42)=42
    // usable_h = 600-84=516; rows = 516/160 = 3
    EXPECT_EQ(s.tiles_per_row, 5);
    EXPECT_EQ(s.tile_w, 160);
    EXPECT_EQ(s.rows_visible, 3);
}

// ---------------------------------------------------------------------------
// Minimum tile width enforcement (80 px floor)
// ---------------------------------------------------------------------------

TEST(GalleryLayout, TileWidthFloorAt80) {
    // dw/5 = 60 < 80 for dw=300, so tile_w is clamped to 80,
    // then tiles_per_row = 300/80 = 3, tile_w re-padded to 300/3 = 100.
    auto s = layout(300, 500);
    check_invariants(s, 300, 500, "300x500");
    EXPECT_GE(s.tile_w, 80) << "tile_w must not be < 80";
}

TEST(GalleryLayout, VerySmallWidth_SingleColumn) {
    // dw=50: dw/5=10 < 80 → tile_w=80; tiles_per_row=50/80=0 → clamped to 1
    // tile_w re-padded to 50/1=50.
    auto s = layout(50, 200);
    check_invariants(s, 50, 200, "50x200");
    EXPECT_EQ(s.tiles_per_row, 1);
}

// ---------------------------------------------------------------------------
// Square-tile invariant across several sizes
// ---------------------------------------------------------------------------

TEST(GalleryLayout, TilesAreAlwaysSquare) {
    int sizes[][2] = {{640,480},{1024,768},{1920,1080},{1280,720},{320,240},{800,600}};
    for (auto& sz : sizes) {
        auto s = layout(sz[0], sz[1]);
        EXPECT_EQ(s.tile_w, s.tile_h) << "not square at " << sz[0] << "x" << sz[1];
    }
}

// ---------------------------------------------------------------------------
// rows_visible >= 1 even when display is shorter than one tile
// ---------------------------------------------------------------------------

TEST(GalleryLayout, ShortDisplay_RowsVisibleAtLeast1) {
    // dh=40: tab_h = max(28,2)=28; usable_h = 40-56 = -16 < 0
    // rows_vis = -16/tile_h → 0 → clamped to 1.
    auto s = layout(800, 40);
    check_invariants(s, 800, 40, "800x40");
    EXPECT_GE(s.rows_visible, 1);
}

// ---------------------------------------------------------------------------
// Adjustment for < 6 visible tiles
// ---------------------------------------------------------------------------

TEST(GalleryLayout, FewTilesAdjustmentIncreasesPerRow) {
    // 500x200: initial tiles_per_row=5, rows_vis=1 → 5<6 → bumped to 6.
    // tab_h = max(28, 14) = 28; usable_h = 200-56=144; initial tile_w=100
    // rows_vis = 144/100 = 1; 5*1=5 < 6 && 5 > 2 → ++tiles_per_row=6
    auto s = layout(500, 200);
    check_invariants(s, 500, 200, "500x200 (adjustment)");
    // After adjustment tiles_per_row should be 6.
    EXPECT_EQ(s.tiles_per_row, 6);
}

TEST(GalleryLayout, NoAdjustmentWhenTilesPerRowLeq2) {
    // If tiles_per_row <= 2 (small display) the adjustment is skipped to
    // avoid making tiles impossibly thin.
    auto s = layout(50, 200); // forces tiles_per_row=1
    EXPECT_EQ(s.tiles_per_row, 1); // must not be bumped past 1
}

// ---------------------------------------------------------------------------
// Tile count grows with display size
// ---------------------------------------------------------------------------

TEST(GalleryLayout, LargerDisplayFitsMoreTiles) {
    auto small = layout(640, 480);
    auto large = layout(1920, 1080);
    int small_tiles = small.tiles_per_row * small.rows_visible;
    int large_tiles = large.tiles_per_row * large.rows_visible;
    EXPECT_GT(large_tiles, small_tiles)
        << "1080p should fit more tiles per page than 480p";
}
