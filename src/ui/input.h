#pragma once
#include <SDL2/SDL.h>
#include <functional>

struct InputCallbacks {
    std::function<void()> on_aperture_up;
    std::function<void()> on_aperture_down;
    std::function<void()> on_focus_up;
    std::function<void()> on_focus_down;
    std::function<void()> on_toggle_ae;
    std::function<void()> on_toggle_af;
    std::function<void()> on_still;
    std::function<void()> on_record_toggle;   // fires after 500ms hold
    std::function<void()> on_toggle_crosshair;
    std::function<void()> on_quit;
};

class InputHandler {
public:
    explicit InputHandler(InputCallbacks cbs);

    // Process all pending SDL events. Returns false if app should quit.
    bool process_events();

    // Returns 0.0–1.0 progress of current Shift+R hold (0 if not held).
    float record_hold_progress() const;

private:
    InputCallbacks cbs_;

    // Shift+R hold tracking
    bool     record_held_{false};
    uint64_t record_press_tick_{0};
    static constexpr uint64_t kRecordHoldMs = 500;
};
