#include "input.h"
#include "keybinding.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction — builds the table-driven binding list
// ---------------------------------------------------------------------------

void InputHandler::build_bindings(const KeyMap& keys) {
    auto add = [&](const std::string& binding, bool no_repeat,
                   std::function<void()> action) {
        auto [sym, shift] = parse_key_binding(binding);
        if (sym != SDLK_UNKNOWN)
            bindings_.push_back({sym, shift, no_repeat, std::move(action)});
    };

    add(keys.mode_cycle_fwd,  true,  [&]{ if (cbs_.on_mode_cycle_fwd)  cbs_.on_mode_cycle_fwd(); });
    add(keys.mode_cycle_back, true,  [&]{ if (cbs_.on_mode_cycle_back) cbs_.on_mode_cycle_back(); });
    add(keys.mode_p,          true,  [&]{ if (cbs_.on_mode_set) cbs_.on_mode_set(0); });
    add(keys.mode_a,          true,  [&]{ if (cbs_.on_mode_set) cbs_.on_mode_set(1); });
    add(keys.mode_s,          true,  [&]{ if (cbs_.on_mode_set) cbs_.on_mode_set(2); });
    add(keys.mode_m,          true,  [&]{ if (cbs_.on_mode_set) cbs_.on_mode_set(3); });
    add(keys.iso_up,          false, [&]{ if (cbs_.on_iso_up)   cbs_.on_iso_up(); });
    add(keys.iso_down,        false, [&]{ if (cbs_.on_iso_down) cbs_.on_iso_down(); });
    add(keys.shutter_up,      false, [&]{ if (cbs_.on_shutter_up)   cbs_.on_shutter_up(); });
    add(keys.shutter_down,    false, [&]{ if (cbs_.on_shutter_down) cbs_.on_shutter_down(); });
    add(keys.focus_up,        false, [&]{ if (cbs_.on_focus_up)   cbs_.on_focus_up(); });
    add(keys.focus_down,      false, [&]{ if (cbs_.on_focus_down) cbs_.on_focus_down(); });
    add(keys.aperture_up,     false, [&]{ if (cbs_.on_aperture_up)   cbs_.on_aperture_up(); });
    add(keys.aperture_down,   false, [&]{ if (cbs_.on_aperture_down) cbs_.on_aperture_down(); });
    add(keys.toggle_af,       true,  [&]{ if (cbs_.on_toggle_af) cbs_.on_toggle_af(); });
    add(keys.still,           true,  [&]{ if (cbs_.on_still)     cbs_.on_still(); });
    add(keys.crosshair,       true,  [&]{ if (cbs_.on_toggle_crosshair) cbs_.on_toggle_crosshair(); });

    // Record and quit are handled separately (hold timers); stash their parsed keys.
    auto [rsym, rshift] = parse_key_binding(keys.record);
    record_sym_         = (rsym != SDLK_UNKNOWN) ? rsym : SDLK_r;
    record_needs_shift_ = rshift;

    auto [qsym, qshift] = parse_key_binding(keys.quit);
    quit_sym_           = (qsym != SDLK_UNKNOWN) ? qsym : SDLK_ESCAPE;
    (void)qshift; // quit binding never requires a modifier

    auto [hsym, hshift] = parse_key_binding(keys.help);
    help_sym_           = (hsym != SDLK_UNKNOWN) ? hsym : SDLK_h;
    (void)hshift;

    auto [vsym, vshift] = parse_key_binding(keys.cam_mode);
    cam_mode_sym_       = (vsym != SDLK_UNKNOWN) ? vsym : SDLK_v;
    (void)vshift;

    auto [tlsym, tlshift] = parse_key_binding(keys.timelapse);
    tl_sym_          = (tlsym != SDLK_UNKNOWN) ? tlsym : SDLK_l;
    tl_needs_shift_  = tlshift;

    add(keys.cam_mode, true, [&]{ if (cbs_.on_cam_mode_toggle) cbs_.on_cam_mode_toggle(); });
}

InputHandler::InputHandler(InputCallbacks cbs, const KeyMap& keys)
    : cbs_(std::move(cbs))
{
    build_bindings(keys);
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

bool InputHandler::process_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            return false;

        if (ev.type == SDL_MOUSEWHEEL) {
            if (ev.wheel.y != 0) {
                int dir = ev.wheel.y > 0 ? 1 : -1;
                if (mode_list_open_) {
                    // Scroll navigates the camera mode list.
                    if (dir > 0) { if (cbs_.on_cam_mode_up)   cbs_.on_cam_mode_up(); }
                    else         { if (cbs_.on_cam_mode_down) cbs_.on_cam_mode_down(); }
                } else {
                    if (cbs_.on_focus_scroll) cbs_.on_focus_scroll(dir);
                }
            }
            continue;
        }

        if (ev.type == SDL_KEYDOWN) {
            bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            SDL_Keycode sym = ev.key.keysym.sym;

            // ---- Camera mode list modal navigation ----
            // When the list is open, arrow keys and confirm/cancel are consumed
            // here; all other normal bindings are suppressed.
            if (mode_list_open_ && !ev.key.repeat) {
                if (sym == SDLK_UP && !shift) {
                    if (cbs_.on_cam_mode_up) cbs_.on_cam_mode_up();
                } else if (sym == SDLK_DOWN && !shift) {
                    if (cbs_.on_cam_mode_down) cbs_.on_cam_mode_down();
                } else if (sym == SDLK_SPACE || sym == SDLK_RETURN) {
                    if (cbs_.on_cam_mode_confirm) cbs_.on_cam_mode_confirm();
                } else if (sym == SDLK_ESCAPE || sym == cam_mode_sym_) {
                    if (cbs_.on_cam_mode_cancel) cbs_.on_cam_mode_cancel();
                }
                continue;
            }

            // ---- Help hold start ----
            if (sym == help_sym_ && !shift && !ev.key.repeat)
                help_held_ = true, help_press_tick_ = SDL_GetTicks64();

            // ---- Quit hold start ----
            bool is_quit_key = (sym == quit_sym_) || (sym == SDLK_q && !shift);
            if (is_quit_key && !ev.key.repeat && !quit_held_) {
                quit_held_     = true;
                quit_held_sym_ = sym;
                quit_press_tick_ = SDL_GetTicks64();
            }

            // ---- Record hold start ----
            if (sym == record_sym_ && shift == record_needs_shift_ && !ev.key.repeat) {
                record_held_       = true;
                record_press_tick_ = SDL_GetTicks64();
            }

            // ---- Timelapse hold start ----
            if (sym == tl_sym_ && shift == tl_needs_shift_ && !ev.key.repeat) {
                tl_held_       = true;
                tl_press_tick_ = SDL_GetTicks64();
            }

            // ---- Table-driven bindings ----
            // Skip if this key is the quit, record, or timelapse key (handled separately).
            bool is_record_key = (sym == record_sym_ && shift == record_needs_shift_);
            bool is_tl_key     = (sym == tl_sym_     && shift == tl_needs_shift_);
            if (!is_quit_key && !is_record_key && !is_tl_key) {
                for (auto& b : bindings_) {
                    if (b.sym == sym && b.shift == shift) {
                        if (!b.no_repeat || !ev.key.repeat)
                            b.action();
                        break;
                    }
                }
            }
        }

        if (ev.type == SDL_KEYUP) {
            bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            SDL_Keycode sym = ev.key.keysym.sym;

            // ---- Help hold release ----
            if (sym == help_sym_) help_held_ = false;

            // ---- Quit hold release ----
            if (quit_held_ && sym == quit_held_sym_) {
                uint64_t held = SDL_GetTicks64() - quit_press_tick_;
                if (held >= kQuitHoldMs) {
                    if (cbs_.on_quit) cbs_.on_quit();
                }
                quit_held_ = false;
            }

            // ---- Record hold release ----
            if (record_held_ && sym == record_sym_) {
                uint64_t held = SDL_GetTicks64() - record_press_tick_;
                if (held >= kRecordHoldMs && shift == record_needs_shift_) {
                    if (cbs_.on_record_toggle) cbs_.on_record_toggle();
                }
                record_held_ = false;
            }
            // Shift release cancels the record hold.
            if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT)
                record_held_ = false;

            // ---- Timelapse hold release ----
            if (tl_held_ && sym == tl_sym_) {
                uint64_t held = SDL_GetTicks64() - tl_press_tick_;
                if (held >= kRecordHoldMs && shift == tl_needs_shift_) {
                    if (cbs_.on_timelapse_toggle) cbs_.on_timelapse_toggle();
                }
                tl_held_ = false;
            }
            // Shift release cancels the timelapse hold (if bound to a shift key).
            if (tl_needs_shift_ && (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT))
                tl_held_ = false;
        }
    }

    // Fire quit if still held and threshold crossed mid-frame.
    if (quit_held_) {
        uint64_t held = SDL_GetTicks64() - quit_press_tick_;
        if (held >= kQuitHoldMs) {
            quit_held_ = false;
            if (cbs_.on_quit) cbs_.on_quit();
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Progress accessors
// ---------------------------------------------------------------------------

float InputHandler::record_hold_progress() const {
    if (!record_held_) return 0.0f;
    uint64_t held = SDL_GetTicks64() - record_press_tick_;
    return std::min(1.0f, (float)held / kRecordHoldMs);
}

float InputHandler::quit_hold_progress() const {
    if (!quit_held_) return 0.0f;
    uint64_t held = SDL_GetTicks64() - quit_press_tick_;
    return std::min(1.0f, (float)held / kQuitHoldMs);
}

bool InputHandler::help_visible() const {
    if (!help_held_) return false;
    return (SDL_GetTicks64() - help_press_tick_) >= kHelpHoldMs;
}

float InputHandler::timelapse_hold_progress() const {
    if (!tl_held_) return 0.0f;
    uint64_t held = SDL_GetTicks64() - tl_press_tick_;
    return std::min(1.0f, (float)held / kRecordHoldMs);
}
