#define _CRT_SECURE_NO_WARNINGS
namespace kotonoha
{
    class textObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL;
        void push(std::string text, std::string timeStart, std::string timeEnd, std::string type, std::string sceneName)
        {
            // Start ASS Header
            if (!exportTo->text.init)
            {
                std::stringstream ss;
                std::ifstream stylesFile(exportTo->root->fileConfigs->stylesPath);
                if (stylesFile.is_open())
                {
                    std::string line;
                    while (std::getline(stylesFile, line))
                    {
                        if (!line.empty())
                            ss << line << std::endl;
                    }
                    exportTo->text.stream << "[Script Info]\nTitle:" << sceneName << "\nScriptType: v4.00+\nWrapStyle: 0\nScaledBorderAndShadow: yes\nYCbCr Matrix: None\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                                          << ss.str() << "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
                }
                stylesFile.close();
                exportTo->text.init = true;
            }
            // Convert ([comand]=MM:SS:DD) and (MM:SS:DD;) to ASS format
            std::vector<std::string> timeEndMMSSDD;
            std::vector<std::string> timeStartMMSSDD;
            std::string token;
            timeEnd.size() > 9 ? timeEnd.erase(timeEnd.size() - 3) : timeEnd.erase(timeEnd.size() - 1);
            std::istringstream stringStream(timeEnd);
            while (std::getline(stringStream, token, ':'))
            {
                timeEndMMSSDD.push_back(token);
            }
            stringStream.str("");
            stringStream = std::istringstream(timeStart);
            while (std::getline(stringStream, token, ':'))
            {
                timeStartMMSSDD.push_back(token);
            }
            // Push to ASS File, this line append one track sub to ASS
            exportTo->text.stream << "Dialogue: 0,0:" << timeStartMMSSDD[0] << ":" << timeStartMMSSDD[1] << "." << timeStartMMSSDD[2] << ",0:" << timeEndMMSSDD[0] << ":" << timeEndMMSSDD[1] << "." << timeEndMMSSDD[2] << "," << type << ",,0,0,0,," << text << std::endl;
        };
    };
    int playText(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        int h, w;
        // Init ASS
        ASS_Library *ass_library;
        ASS_Renderer *ass_renderer;
        ass_library = ass_library_init();
        ass_renderer = ass_renderer_init(ass_library);
        ass_set_extract_fonts(ass_library, 1);
        ass_set_fonts(ass_renderer, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
        ass_set_shaper(ass_renderer, ASS_SHAPING_COMPLEX);
        ass_set_hinting(ass_renderer, ASS_HINTING_NORMAL);
        // Create new ASS Track
        ASS_Track *track = ass_new_track(ass_library);
        std::string subSs = importedTo->text.stream.str();
        char *str_c = new char[subSs.length() + 1];
        strcpy(str_c, subSs.c_str());
        ass_process_data(track, str_c, (int)subSs.length() + 1);
        // Start
        importedTo->root->log0->appendLog("(Text) - Start");
        SDL_Texture *texture = NULL;
        ASS_Image *img = NULL;
        std::uint32_t* pixels = NULL;
        while (importedTo->control->outCode == 0)
        {
            if (importedTo->control->display[2])
            {
                // Get window size to update
                SDL_GetWindowSize(importedTo->root->window, &w, &h);
                ass_set_storage_size(ass_renderer, w, h);
                ass_set_frame_size(ass_renderer, w, h);
                img = ass_render_frame(ass_renderer, track, kotonohaTime::convertToMs(importedTo->control->timer0.pushTime()), NULL);
                // This for loop send all tracks to display
                for (; img != nullptr; img = img->next)
                {
                    if (importedTo->control->hiddenSub)
                        goto END;
                    SDL_Rect dst = {img->dst_x, img->dst_y, img->w, img->h};
                    texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
                    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                    int pitch = 0;
                    SDL_LockTexture(texture, NULL, reinterpret_cast<void **>(&pixels), &pitch);
                    // Push color, in aplha is set to 255 (opacque)
                    uint32_t cor = (img->color & 0xffffff00) | 0xff;
                    for (int y = 0; y < img->h; y++)
                    {
                        for (int x = 0; x < img->w; x++)
                        {
                            // Put black color with alpha set to 0 (transparent)
                            img->bitmap[x] != 0 ? pixels[x] = cor : pixels[x] = 0x00000000;
                        }
                        pixels = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uintptr_t>(pixels) + pitch);
                        img->bitmap += img->stride;
                    }
                    SDL_UnlockTexture(texture);
                    SDL_RenderCopy(importedTo->root->renderer, texture, NULL, &dst);
                    SDL_DestroyTexture(texture);
                }
                // End frame sub draw
                pixels = NULL;
                delete img;
            END:
                importedTo->control->display[3] = true;
                importedTo->control->display[2] = false;
            }
        };
        // End text engine
        ass_free_track(track);
        ass_renderer_done(ass_renderer);
        ass_library_done(ass_library);
        importedTo->root->log0->appendLog("(Text) - End");
        return 0;
    };
}