namespace kotonoha
{
    typedef struct image_s
    {
        int width, height, stride;
        unsigned char *buffer; // RGB24
    } image_t;
    int playText(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        importedTo->root->log0->appendLog("(Text) - Start");
        kotonohaTime::delay(1000);

        char file[64] = "test.ass";
        char *subfile = file;
        ASS_Library *ass_library;
        ASS_Renderer *ass_renderer;
        int frame_w = 1280;
        int frame_h = 720;
        ass_library = ass_library_init();
        if (!ass_library)
        {
            printf("ass_library_init failed!\n");
            exit(1);
        }
        ass_set_extract_fonts(ass_library, 1);
        ass_renderer = ass_renderer_init(ass_library);
        if (!ass_renderer)
        {
            printf("ass_renderer_init failed!\n");
            exit(1);
        }
        ass_set_storage_size(ass_renderer, frame_w, frame_h);
        ass_set_frame_size(ass_renderer, frame_w, frame_h);
        ass_set_fonts(ass_renderer, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
        ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
        if (!track)
        {
            printf("track init failed!\n");
            return 1;
        }
        importedTo->root->log0->appendLog("(Text) - End");
        while (importedTo->control->outCode == -1)
        {
            if (importedTo->control->display[2])
            {
                ASS_Image *img = ass_render_frame(ass_renderer, track, (int)(5 * 1000), NULL);
                SDL_Texture *texture = nullptr;
                //texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
                //SDL_UpdateTexture(texture, nullptr, img->bitmap, img->stride);
                //SDL_Rect rect = {0, 0, frame_h, frame_w};
                //SDL_RenderCopy(importedTo->root->renderer, texture, &rect, NULL);
                //SDL_DestroyTexture(texture);
                importedTo->control->display[2] = false;
                importedTo->control->display[3] = true;
            }
        }
        ass_free_track(track);
        ass_renderer_done(ass_renderer);
        ass_library_done(ass_library);
        importedTo->root->log0->appendLog("(Text) - End");
        return 0;
    };
}