#include <SDL2/SDL.h>
#include <ass/ass.h>
#include <cassert>
#include <array>
#include <string_view>
#include <cstdint>
#include <thread>
#include <chrono>
#include <vector>

int WinMain(int, char **)
{
    constexpr auto w = 1280;
    constexpr auto h = 720;

    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow("Subtitles", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    assert(window);
    auto renderer = SDL_CreateRenderer(window, -1, 0);
    assert(renderer);

    int frame_w = 0;
    int frame_h = 0;
    assert(SDL_GetRendererOutputSize(renderer, &frame_w, &frame_h) == 0);

    auto ass = ass_library_init();
    assert(ass);
    auto ass_renderer = ass_renderer_init(ass);
    assert(ass_renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    ass_set_storage_size(ass_renderer, frame_w, frame_h);
    ass_set_frame_size(ass_renderer, frame_w, frame_h);
    constexpr std::string_view file_sv = "test.ass";
    std::array<char, file_sv.size() + 1> file;
    auto it = std::copy(file_sv.begin(), file_sv.end(), file.begin());
    *it = '\0';
    ass_set_fonts(ass_renderer, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    auto track = ass_read_file(ass, file.data(), nullptr);
    int detect = 0;
    ASS_Image *img = ass_render_frame(ass_renderer, track, 0, &detect);
    std::vector<std::uint32_t> colors;
    for (; img != nullptr; img = img->next)
    {
        SDL_Rect dst = {img->dst_x, img->dst_y, img->w, img->h};
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        std::uint32_t *pixels = NULL;
        int pitch = 0;
        SDL_LockTexture(texture, NULL, reinterpret_cast<void **>(&pixels), &pitch);
        // It's better to unroll this loop.
        uint32_t cor = (img->color & 0xffffff00) | 0xff;
        for (int y = 0; y < img->h; y++)
        {
            for (int x = 0; x < img->w; x++)
            {
                if (img->bitmap[x] != 0)
                {
                    
                    pixels[x] = cor;
                }
                else
                {
                    pixels[x] = 0x00000000;
                }
            }
            pixels = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uintptr_t>(pixels) + pitch);
            img->bitmap += img->stride;
        }
        SDL_UnlockTexture(texture);
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
        SDL_RenderPresent(renderer);
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
    ass_free_track(track);
    ass_renderer_done(ass_renderer);
    ass_library_done(ass);
    SDL_Quit();
    return 0;
}