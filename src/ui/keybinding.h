#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <utility>

// Parse a key binding string (e.g. "shift+up", "t", "escape") into an SDL
// keycode and a shift-required flag.  Returns {SDLK_UNKNOWN, false} on error.
std::pair<SDL_Keycode, bool> parse_key_binding(const std::string& s);
