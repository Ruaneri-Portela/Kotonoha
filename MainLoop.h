namespace kotonoha
{
    class loop
    {
    public:
        int game(SDL_Window *windowEntry, SDL_Renderer *rendererEntry, std::string path, kotonoha::logger *log0)
        {
            // Create data structure
            kotonohaData::rootData *rootData = new kotonohaData::rootData;
            rootData->window = windowEntry;
            rootData->renderer = rendererEntry;
            rootData->log0 = log0;
            kotonohaData::controlData *controlData = new kotonohaData::controlData;
            kotonohaData::acessMapper *global = new kotonohaData::acessMapper;
            global->control = controlData;
            global->root = rootData;
            rootData->image0 = new kotonohaData::imageData;
            rootData->audio0 = new kotonohaData::audioData;
            rootData->video0 = new kotonohaData::videoData;
            static_cast<kotonoha::audioObject *>(rootData->audio0)->exportTo = global;
            static_cast<kotonoha::videoObject *>(rootData->video0)->exportTo = global;
            static_cast<kotonoha::imageObject *>(rootData->image0)->exportTo = global;
            // Start game loop and threads
            controlData->display[0] = true;
            controlData->display[1] = false;
            controlData->display[2] = false;
            controlData->display[3] = false;
            int returnCode = 0;
            std::thread thread1(ui, global);
            SDL_ShowWindow(windowEntry);
            ors(global, path);
            std::thread thread2(kotonoha::playImage, global);
            std::thread thread3(kotonoha::playVideo, global);
            std::thread thread4(kotonoha::playAudio, global);
            log0->appendLog("(ML) - Entry point to while");
            while (!controlData->exit)
            {
                while (SDL_PollEvent(&rootData->event))
                {
                    ImGui_ImplSDL2_ProcessEvent(&rootData->event);
                    if (rootData->event.type == SDL_QUIT)
                    {
                        returnCode = 1;
                        controlData->exit = true;
                    }
                    else if (rootData->event.type == SDL_KEYDOWN)
                    {
                        if (rootData->event.key.keysym.sym == SDLK_ESCAPE)
                        {
                            returnCode = 2;
                            controlData->exit = true;
                        }
                        if (rootData->event.key.keysym.sym == SDLK_r)
                        {
                            returnCode = 2;
                            controlData->exit = true;
                        }
                        if (rootData->event.key.keysym.sym == SDLK_F11)
                        {
                            Uint32 FullscreenFlag = SDL_WINDOW_FULLSCREEN;
                            bool IsFullscreen = SDL_GetWindowFlags(windowEntry) & FullscreenFlag;
                            SDL_SetWindowFullscreen(windowEntry, IsFullscreen ? 0 : FullscreenFlag);
                            SDL_ShowCursor(IsFullscreen);
                        }
                    }
                }
                if (controlData->reset)
                {
                    log0->appendLog("(ML) - Reset");
                    returnCode = 2;
                    controlData->exit = true;
                }
                else if (controlData->menu)
                {
                    log0->appendLog("(ML) - Return to menu");
                    returnCode = 3;
                    controlData->exit = true;
                }
                else if (controlData->display[3])
                {
                    SDL_RenderPresent(rendererEntry);
                    SDL_RenderClear(rendererEntry);
                    controlData->display[3] = false;
                    controlData->display[0] = true;
                }
            }
            // Wait threads
            thread1.join();
            thread2.join();
            thread3.join();
            thread4.join();
            log0->appendLog("(ML) - Exit");
            return returnCode;
        };
    };
}