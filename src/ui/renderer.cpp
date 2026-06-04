#include "renderer.h"
#include <stdexcept>

Renderer::Renderer(int width, int height)
    : width_(width), height_(height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    window_ = SDL_CreateWindow(
        "microscopy",
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

    // YV12 = planar YUV 4:2:0, SDL_UpdateYUVTexture accepts Y+U+V planes.
    frame_tex_ = SDL_CreateTexture(renderer_,
                                   SDL_PIXELFORMAT_YV12,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   width, height);
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
