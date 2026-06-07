#include "ui/keybinding.h"
#include <gtest/gtest.h>
#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Single letters
// ---------------------------------------------------------------------------

TEST(ParseKeyBinding, LowercaseLetter) {
    auto [sym, shift] = parse_key_binding("t");
    EXPECT_EQ(sym, SDLK_t);
    EXPECT_FALSE(shift);
}

TEST(ParseKeyBinding, UppercaseInputNormalized) {
    // Input strings from config files may be any case.
    auto [sym, shift] = parse_key_binding("T");
    EXPECT_EQ(sym, SDLK_t);
    EXPECT_FALSE(shift);
}

TEST(ParseKeyBinding, Digit) {
    auto [sym, shift] = parse_key_binding("1");
    EXPECT_EQ(sym, SDLK_1);
    EXPECT_FALSE(shift);
}

// ---------------------------------------------------------------------------
// Shift modifier
// ---------------------------------------------------------------------------

TEST(ParseKeyBinding, ShiftLetter) {
    auto [sym, shift] = parse_key_binding("shift+r");
    EXPECT_EQ(sym, SDLK_r);
    EXPECT_TRUE(shift);
}

TEST(ParseKeyBinding, ShiftLetterUpperInput) {
    auto [sym, shift] = parse_key_binding("Shift+R");
    EXPECT_EQ(sym, SDLK_r);
    EXPECT_TRUE(shift);
}

TEST(ParseKeyBinding, ShiftArrow) {
    auto [sym, shift] = parse_key_binding("shift+up");
    EXPECT_EQ(sym, SDLK_UP);
    EXPECT_TRUE(shift);
}

// ---------------------------------------------------------------------------
// Named special keys
// ---------------------------------------------------------------------------

TEST(ParseKeyBinding, ArrowKeys) {
    EXPECT_EQ(parse_key_binding("up").first,    SDLK_UP);
    EXPECT_EQ(parse_key_binding("down").first,  SDLK_DOWN);
    EXPECT_EQ(parse_key_binding("left").first,  SDLK_LEFT);
    EXPECT_EQ(parse_key_binding("right").first, SDLK_RIGHT);
}

TEST(ParseKeyBinding, SpaceAndEscape) {
    EXPECT_EQ(parse_key_binding("space").first,  SDLK_SPACE);
    EXPECT_EQ(parse_key_binding("escape").first, SDLK_ESCAPE);
}

TEST(ParseKeyBinding, ReturnAliases) {
    EXPECT_EQ(parse_key_binding("return").first, SDLK_RETURN);
    EXPECT_EQ(parse_key_binding("enter").first,  SDLK_RETURN);
}

TEST(ParseKeyBinding, Tab) {
    EXPECT_EQ(parse_key_binding("tab").first, SDLK_TAB);
}

// ---------------------------------------------------------------------------
// Default key map entries (the ones InputHandler will use at startup)
// ---------------------------------------------------------------------------

TEST(ParseKeyBinding, DefaultBindings) {
    EXPECT_EQ(parse_key_binding("t").first,          SDLK_t);
    EXPECT_EQ(parse_key_binding("shift+t").first,    SDLK_t);
    EXPECT_EQ(parse_key_binding("shift+t").second,   true);
    EXPECT_EQ(parse_key_binding("i").first,          SDLK_i);
    EXPECT_EQ(parse_key_binding("shift+i").first,    SDLK_i);
    EXPECT_EQ(parse_key_binding("shift+r").first,    SDLK_r);
    EXPECT_EQ(parse_key_binding("shift+a").first,    SDLK_a);
    EXPECT_EQ(parse_key_binding("c").first,          SDLK_c);
    EXPECT_EQ(parse_key_binding("h").first,          SDLK_h);
    EXPECT_EQ(parse_key_binding("space").first,      SDLK_SPACE);
    EXPECT_EQ(parse_key_binding("escape").first,     SDLK_ESCAPE);
    EXPECT_EQ(parse_key_binding("shift+up").first,   SDLK_UP);
    EXPECT_EQ(parse_key_binding("shift+down").first, SDLK_DOWN);
}

// ---------------------------------------------------------------------------
// Unknown / invalid inputs
// ---------------------------------------------------------------------------

TEST(ParseKeyBinding, UnknownNameReturnsUnknown) {
    EXPECT_EQ(parse_key_binding("foobar").first,   SDLK_UNKNOWN);
    EXPECT_EQ(parse_key_binding("").first,         SDLK_UNKNOWN);
    EXPECT_EQ(parse_key_binding("shift+").first,   SDLK_UNKNOWN);
}
