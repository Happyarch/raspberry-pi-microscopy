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

    // Recreate the YUV texture at a new camera resolution (e.g. after a mode change).
    // Display window size is unchanged; SDL scales the texture to fill the window.
    void update_texture_size(int cam_width, int cam_height);

    // Set crop margins (camera-frame pixels). The cropped region is scaled to fill
    // the display. All zero by default (no crop).
    void set_crop(int top, int bottom, int left, int right);


    SDL_Renderer* sdl_renderer() const { return renderer_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    int          width_, height_;
    int          cam_w_{0}, cam_h_{0};  // camera frame dimensions (may differ from display)
    int          crop_top_{0}, crop_bottom_{0}, crop_left_{0}, crop_right_{0};
    SDL_Window*  window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture*  frame_tex_{nullptr};
};
