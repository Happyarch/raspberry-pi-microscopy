#include "input.h"
#include <algorithm>

InputHandler::InputHandler(InputCallbacks cbs) : cbs_(std::move(cbs)) {}

bool InputHandler::process_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            if (cbs_.on_quit) cbs_.on_quit();
            return false;
        }

        if (ev.type == SDL_KEYDOWN) {
            bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                if (cbs_.on_quit) cbs_.on_quit();
                return false;

            case SDLK_UP:
                if (shift) { if (cbs_.on_focus_up)    cbs_.on_focus_up(); }
                else        { if (cbs_.on_aperture_up) cbs_.on_aperture_up(); }
                break;

            case SDLK_DOWN:
                if (shift) { if (cbs_.on_focus_down)    cbs_.on_focus_down(); }
                else        { if (cbs_.on_aperture_down) cbs_.on_aperture_down(); }
                break;

            case SDLK_a:
                if (shift) { if (cbs_.on_toggle_af) cbs_.on_toggle_af(); }
                else        { if (cbs_.on_toggle_ae) cbs_.on_toggle_ae(); }
                break;

            case SDLK_SPACE:
                if (cbs_.on_still) cbs_.on_still();
                break;

            case SDLK_r:
                if (shift && !ev.key.repeat) {
                    record_held_      = true;
                    record_press_tick_ = SDL_GetTicks64();
                }
                break;

            default: break;
            }
        }

        if (ev.type == SDL_KEYUP) {
            bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            if (ev.key.keysym.sym == SDLK_r && record_held_) {
                uint64_t held = SDL_GetTicks64() - record_press_tick_;
                if (held >= kRecordHoldMs && shift) {
                    if (cbs_.on_record_toggle) cbs_.on_record_toggle();
                }
                record_held_ = false;
            }
            // Also cancel if shift released while r is still logically held.
            if (ev.key.keysym.sym == SDLK_LSHIFT || ev.key.keysym.sym == SDLK_RSHIFT) {
                record_held_ = false;
            }
        }
    }

    // Check if hold threshold crossed mid-frame (key still down).
    // The actual trigger fires on key-up; we only track progress here.
    return true;
}

float InputHandler::record_hold_progress() const {
    if (!record_held_) return 0.0f;
    uint64_t held = SDL_GetTicks64() - record_press_tick_;
    return std::min(1.0f, (float)held / kRecordHoldMs);
}
