#include "renderer.h"
#include <algorithm>
#include <stdexcept>

Renderer::Renderer(int width, int height)
    : width_(width), height_(height)
{
    // Grab keyboard so unbound keys don't leak to the underlying TTY.
    // On kmsdrm/evdev this causes EVIOCGRAB on each input device, which
    // exclusively locks them away from the kernel's VT/TTY input path.
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    window_ = SDL_CreateWindow(
        "microscopi",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window_)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    SDL_SetWindowGrab(window_, SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);

    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED |
                                   SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
        throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    // SDL_WINDOW_FULLSCREEN_DESKTOP ignores the requested dimensions and uses
    // the display's current mode.  Query the actual output size so the OSD
    // and camera resolution match what's really on screen.
    SDL_GetRendererOutputSize(renderer_, &width_, &height_);

    cam_w_ = width_;
    cam_h_ = height_;

    // IYUV = I420 = planar YUV 4:2:0 with U (Cb) before V (Cr), matching
    // libcamera's YUV420 output order.
    frame_tex_ = SDL_CreateTexture(renderer_,
                                   SDL_PIXELFORMAT_IYUV,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   cam_w_, cam_h_);
    if (!frame_tex_)
        throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
}

Renderer::~Renderer() {
    if (frame_tex_)  SDL_DestroyTexture(frame_tex_);
    if (renderer_)   SDL_DestroyRenderer(renderer_);
    if (window_)     SDL_DestroyWindow(window_);
    SDL_Quit();
}

void Renderer::update_texture_size(int cam_width, int cam_height) {
    cam_w_ = cam_width;
    cam_h_ = cam_height;
    if (frame_tex_) SDL_DestroyTexture(frame_tex_);
    frame_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_IYUV,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   cam_w_, cam_h_);
}

void Renderer::set_crop(int top, int bottom, int left, int right) {
    crop_top_    = top;
    crop_bottom_ = bottom;
    crop_left_   = left;
    crop_right_  = right;
}

void Renderer::present_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int y_stride, int uv_stride,
                              Osd* osd, const OsdState& state)
{
    SDL_UpdateYUVTexture(frame_tex_, nullptr,
                         y, y_stride,
                         u, uv_stride,
                         v, uv_stride);

    SDL_RenderClear(renderer_);
    if (crop_top_ || crop_bottom_ || crop_left_ || crop_right_) {
        SDL_Rect src;
        src.x = crop_left_;
        src.y = crop_top_;
        src.w = std::max(1, cam_w_ - crop_left_ - crop_right_);
        src.h = std::max(1, cam_h_ - crop_top_  - crop_bottom_);
        SDL_RenderCopy(renderer_, frame_tex_, &src, nullptr);
    } else {
        SDL_RenderCopy(renderer_, frame_tex_, nullptr, nullptr);
    }

    if (osd) osd->draw(state);

    SDL_RenderPresent(renderer_);
}
