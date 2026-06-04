#pragma once
#include <SDL2/SDL.h>
#include "osd.h"

class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();

    // Upload a YUV420 frame and composite the OSD on top.
    // y/u/v are raw plane pointers; strides are in bytes.
    void present_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int y_stride, int uv_stride,
                       Osd* osd, const OsdState& state);

    SDL_Renderer* sdl_renderer() const { return renderer_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    int          width_, height_;
    SDL_Window*  window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture*  frame_tex_{nullptr};
};
