/**
 * @file MainLoop.h
 * @brief Contains the definition of the kotonoha::loop class and its game function.
 * 
 * The kotonoha::loop class contains the game loop and threads for the Kotonoha game engine.
 * The game function initializes the necessary data structures, starts the game loop and threads,
 * and waits for them to finish before returning a return code.
 */
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
            int returnCode = 1;
            std::thread thread1(ui, global);
            SDL_ShowWindow(rootData->window);
            rootData->log0->appendLog("(ORS - Pre) - Reading "+path);
            ors(global,path);
            std::thread thread2(kotonoha::playImage, global);
            std::thread thread3(kotonoha::playVideo, global);
            std::thread thread4(kotonoha::playAudio, global);
            rootData->log0->appendLog("(ML) - Entry point to while");
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
                            bool IsFullscreen = SDL_GetWindowFlags(rootData->window) & FullscreenFlag;
                            SDL_SetWindowFullscreen(rootData->window, IsFullscreen ? 0 : FullscreenFlag);
                            SDL_ShowCursor(IsFullscreen);
                        }
                    }
                }
                if (controlData->reset)
                {
                    rootData->log0->appendLog("(ML) - Reset");
                    returnCode = 2;
                    controlData->exit = true;
                }
                else if (controlData->menu)
                {
                    rootData->log0->appendLog("(ML) - Return to menu");
                    returnCode = 3;
                    controlData->exit = true;
                }
                if (controlData->endTime < controlData->timer0.pushTime())
                {
                    rootData->log0->appendLog("(ML) - End script");
                    returnCode = 4;
                    controlData->exit = true;
                }
            }
            // Wait threads
            thread1.join();
            thread2.join();
            thread3.join();
            thread4.join();
            rootData->log0->appendLog("(ML) - Exit");

            return returnCode;
        };
    };
}