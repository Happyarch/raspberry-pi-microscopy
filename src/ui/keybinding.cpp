#include "keybinding.h"
#include <cctype>
#include <string>

static SDL_Keycode name_to_keycode(const std::string& s) {
    static const struct { const char* name; SDL_Keycode code; } table[] = {
        {"up",        SDLK_UP},
        {"down",      SDLK_DOWN},
        {"left",      SDLK_LEFT},
        {"right",     SDLK_RIGHT},
        {"space",     SDLK_SPACE},
        {"escape",    SDLK_ESCAPE},
        {"return",    SDLK_RETURN},
        {"enter",     SDLK_RETURN},
        {"tab",       SDLK_TAB},
        {"backspace", SDLK_BACKSPACE},
    };
    for (auto& e : table)
        if (s == e.name) return e.code;
    if (s.size() == 1) {
        char c = s[0];
        if (c >= 'a' && c <= 'z') return (SDL_Keycode)(SDLK_a + (c - 'a'));
        if (c >= '0' && c <= '9') return (SDL_Keycode)(SDLK_0 + (c - '0'));
    }
    return SDLK_UNKNOWN;
}

std::pair<SDL_Keycode, bool> parse_key_binding(const std::string& s) {
    std::string lower = s;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    bool shift = false;
    std::string key_part = lower;
    if (lower.size() > 6 && lower.substr(0, 6) == "shift+") {
        shift    = true;
        key_part = lower.substr(6);
    }
    return {name_to_keycode(key_part), shift};
}
