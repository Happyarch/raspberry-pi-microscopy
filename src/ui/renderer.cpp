#include "renderer.h"
#include <stdexcept>

Renderer::Renderer(int width, int height)
    : width_(width), height_(height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    window_ = SDL_CreateWindow(
        "microscopi",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window_)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED |
                                   SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
        throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    // SDL_WINDOW_FULLSCREEN_DESKTOP ignores the requested dimensions and uses
    // the display's current mode.  Query the actual output size so the OSD
    // and camera resolution match what's really on screen.
    SDL_GetRendererOutputSize(renderer_, &width_, &height_);

    // IYUV = I420 = planar YUV 4:2:0 with U (Cb) before V (Cr), matching
    // libcamera's YUV420 output order.
    frame_tex_ = SDL_CreateTexture(renderer_,
                                   SDL_PIXELFORMAT_IYUV,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   width_, height_);
    if (!frame_tex_)
        throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
}

Renderer::~Renderer() {
    if (frame_tex_)  SDL_DestroyTexture(frame_tex_);
    if (renderer_)   SDL_DestroyRenderer(renderer_);
    if (window_)     SDL_DestroyWindow(window_);
    SDL_Quit();
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
    SDL_RenderCopy(renderer_, frame_tex_, nullptr, nullptr);

    if (osd) osd->draw(state);

    SDL_RenderPresent(renderer_);
}
