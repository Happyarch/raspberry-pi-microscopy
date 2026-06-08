#pragma once
#include "../config/config.h"
#include <SDL2/SDL.h>
#include <functional>
#include <vector>

struct InputCallbacks {
    std::function<void()>    on_mode_cycle_fwd;
    std::function<void()>    on_mode_cycle_back;
    std::function<void(int)> on_mode_set;        // 0=P, 1=A, 2=S, 3=M
    std::function<void()>    on_iso_up;
    std::function<void()>    on_iso_down;
    std::function<void()>    on_shutter_up;
    std::function<void()>    on_shutter_down;
    std::function<void()>    on_aperture_up;
    std::function<void()>    on_aperture_down;
    std::function<void()>    on_focus_up;
    std::function<void()>    on_focus_down;
    std::function<void()>    on_toggle_af;
    std::function<void()>    on_still;
    std::function<void()>    on_record_toggle;   // fires after hold threshold
    std::function<void()>    on_timelapse_toggle; // fires after hold threshold
    std::function<void()>    on_toggle_crosshair;
    std::function<void()>    on_quit;

    // Mouse scroll wheel: +1 = toward macro, -1 = toward infinity (per notch).
    // Only fires when a mouse is connected; no-op if not set.
    std::function<void(int)> on_focus_scroll;

    // Camera mode list navigation — only fired while mode list is open.
    std::function<void()>    on_cam_mode_up;
    std::function<void()>    on_cam_mode_down;
    std::function<void()>    on_cam_mode_confirm;
    std::function<void()>    on_cam_mode_cancel;
    // Toggle the mode list open/closed (normal dispatch, list not yet open).
    std::function<void()>    on_cam_mode_toggle;
};

class InputHandler {
public:
    InputHandler(InputCallbacks cbs, const KeyMap& keys);

    // Process all pending SDL events. Returns false only on SDL_QUIT.
    bool process_events();

    // 0.0–1.0 progress of current Shift+R hold (0 if not held).
    float record_hold_progress() const;

    // 0.0–1.0 progress of current timelapse key hold (0 if not held).
    float timelapse_hold_progress() const;

    // 0.0–1.0 progress of current quit key hold (0 if not held).
    // Warning shows at 0.5 (2.5 s); quit fires at 1.0 (5 s).
    float quit_hold_progress() const;

    // True when the help key has been held for ≥ 3 s (overlay stays while held).
    bool help_visible() const;

    // Switch the input handler between normal dispatch and mode-list navigation.
    void set_mode_list_open(bool open) { mode_list_open_ = open; }

private:
    struct BoundKey {
        SDL_Keycode           sym;
        bool                  shift;
        bool                  no_repeat;
        std::function<void()> action;
    };

    void build_bindings(const KeyMap& keys);

    InputCallbacks       cbs_;
    std::vector<BoundKey> bindings_;

    // Shift+R hold
    bool     record_held_{false};
    uint64_t record_press_tick_{0};
    SDL_Keycode record_sym_{SDLK_r};
    bool        record_needs_shift_{true};

    // Timelapse hold
    bool        tl_held_{false};
    uint64_t    tl_press_tick_{0};
    SDL_Keycode tl_sym_{SDLK_l};
    bool        tl_needs_shift_{false};

    // Quit hold — tracks whichever quit key was pressed first
    bool        quit_held_{false};
    SDL_Keycode quit_held_sym_{SDLK_UNKNOWN};
    uint64_t    quit_press_tick_{0};
    SDL_Keycode quit_sym_{SDLK_ESCAPE};

    // Help hold
    bool        help_held_{false};
    uint64_t    help_press_tick_{0};
    SDL_Keycode help_sym_{SDLK_h};

    // Camera mode list modal state
    bool        mode_list_open_{false};
    SDL_Keycode cam_mode_sym_{SDLK_v};

    static constexpr uint64_t kRecordHoldMs = 500;
    static constexpr uint64_t kQuitHoldMs   = 5000;
    static constexpr uint64_t kHelpHoldMs   = 3000;
};
