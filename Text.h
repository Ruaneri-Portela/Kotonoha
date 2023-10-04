namespace kotonoha
{
    inline std::pair<size_t, std::uint8_t> x2pos_mask(int x)
    {
        //size_t pos = x / CHAR_BIT;
        size_t r = x % CHAR_BIT;

        std::uint8_t mask = 0b10000000;
        mask >>= r;

        return {x, mask};
    }
    int playText(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        importedTo->root->log0->appendLog("(Text) - Start");
        kotonohaTime::delay(1000);

        char file[64] = "test.ass";
        char *subfile = file;
        ASS_Library *ass_library;
        ASS_Renderer *ass_renderer;
        int h, w;
        ass_library = ass_library_init();
        ass_set_extract_fonts(ass_library, 1);
        ass_renderer = ass_renderer_init(ass_library);
        ass_set_fonts(ass_renderer, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
        ass_set_hinting(ass_renderer,ASS_HINTING_NATIVE);
        ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
        importedTo->root->log0->appendLog("(Text) - Start");
        while (importedTo->control->outCode == -1)
        {
            if (importedTo->control->display[2])
            {
                SDL_GetWindowSize(importedTo->root->window, &w, &h);
                ass_set_storage_size(ass_renderer, w, h);
                ass_set_frame_size(ass_renderer, w, h);
                ASS_Image *img = ass_render_frame(ass_renderer, track, 1000,NULL);
                for (; img != NULL; img = img->next)
                {
                    if (img->w == 0 || img->h == 0)
                    {
                        continue;
                    }
                    SDL_Texture *texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
                    uint32_t *pixels = NULL;
                    int pitch = 0;
                    SDL_LockTexture(texture, NULL, reinterpret_cast<void **>(&pixels), &pitch);
                    unsigned char *bitmap = img->bitmap;
                    // It's better to unroll this loop.
                    for (int y = 0; y < img->h; y++)
                    {
                        for (int x = 0; x < img->w; x++)
                        {
                            auto pos_mask = x2pos_mask(x);

                            auto filled = bitmap[pos_mask.first] & pos_mask.second;

                            pixels[x] = (filled) ? img->color : 0;
                        }

                        pixels = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uintptr_t>(pixels) + pitch);
                        bitmap += img->stride;
                    }

                    SDL_UnlockTexture(texture);

                    SDL_Rect dst = {img->dst_x, img->dst_y, img->w, img->h};
                    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_MUL);
                    SDL_RenderCopy(importedTo->root->renderer, texture, NULL, &dst);

                    SDL_DestroyTexture(texture);
                }
                importedTo->control->display[2] = false;
                importedTo->control->display[3] = true;
            }
        };
        ass_free_track(track);
        ass_renderer_done(ass_renderer);
        ass_library_done(ass_library);
        importedTo->root->log0->appendLog("(Text) - End");
        return 0;
    };
}