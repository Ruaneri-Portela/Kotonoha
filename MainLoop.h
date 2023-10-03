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
        int game(SDL_Window *windowEntry, SDL_Renderer *rendererEntry, std::string path,kotonohaData::configsData fileConfigs,kotonoha::logger *log0)
        {
            // Create data structure
            kotonohaData::rootData *rootData = new kotonohaData::rootData;
            rootData->window = windowEntry;
            rootData->renderer = rendererEntry;
            rootData->log0 = log0;
            rootData->fileConfigs = &fileConfigs;
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
            controlData->display[4] = false;
            SDL_ShowWindow(rootData->window);
            rootData->log0->appendLog("(ORS - Pre) - Reading " + path);
            ors(global, path);
            std::thread thread1(ui, global);
            std::thread thread2(kotonoha::playImage, global);
            std::thread thread3(kotonoha::playVideo, global);
            std::thread thread4(kotonoha::playAudio, global);
            std::thread thread5(kotonoha::playText, global);
            kotonohaTime::delay(2000);
            rootData->log0->appendLog("(ML) - Entry point to while");
            while (controlData->outCode == -1)
            {
                while (SDL_PollEvent(&rootData->event))
                {
                    ImGui_ImplSDL2_ProcessEvent(&rootData->event);
                    if (rootData->event.type == SDL_QUIT)
                    {
                        controlData->outCode = 1;
                    }
                    else if (rootData->event.type == SDL_KEYDOWN)
                    {
                        if (rootData->event.key.keysym.sym == SDLK_ESCAPE)
                        {
                            controlData->outCode = 3;
                        }
                        if (rootData->event.key.keysym.sym == SDLK_r)
                        {
                            controlData->outCode = 2;
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
                if(controlData->display[4]){
                    SDL_RenderPresent(rendererEntry);
                    SDL_RenderClear(rendererEntry);
                    controlData->display[4] = false;
                    controlData->display[0] = true;
                }
            }
            // Wait threads
            thread1.join();
            thread2.join();
            thread3.join();
            thread4.join();
            thread5.join();
            switch (controlData->outCode)
            {
            case 1:
                rootData->log0->appendLog("(ML) - Close");
                break;
            case 2:
                rootData->log0->appendLog("(ML) - Reset");
                break;
            case 3:
                rootData->log0->appendLog("(ML) - Return to menu");
                break;
            case 4:
                rootData->log0->appendLog("(ML) - Scene ended");
                break;
            default:
                rootData->log0->appendLog("(ML) - Uknonw return code");
                break;
            }
            int returnCode = controlData->outCode;
            rootData->log0->appendLog("(ML) - End");
            delete static_cast<kotonoha::audioObject *>(rootData->audio0);
            delete static_cast<kotonoha::videoObject *>(rootData->video0);
            delete static_cast<kotonoha::imageObject *>(rootData->image0);
            delete global;
            delete controlData;
            delete rootData;
            return returnCode;
        };
    };
}