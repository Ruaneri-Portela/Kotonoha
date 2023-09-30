#include "Ors.h"
#include "Ui.h"
class gameLoop
{
public:
    int game(SDL_Window *windowEntry, SDL_Renderer *rendererEntry)
    {
        gameData data;
        drawControl drawing;
        data.window = windowEntry;
        data.renderer = rendererEntry;
        data.dataDraw = &drawing;
        SDL_ShowWindow(data.window);
        ors(&data);
        SDL_Thread *thread1 = SDL_CreateThread(audio::play, "t1", data.audio0.data);
        SDL_Thread *thread2 = SDL_CreateThread(video::play, "t2", data.video0.data);
        std::thread thread3(ui, &data);
        int windowWidth =1280, windowHeight=720;
        while (data.run)
        {
            data.square = {0, 0, windowWidth, windowWidth};
            SDL_GetWindowSize(windowEntry, &windowWidth, &windowHeight);
            while (SDL_PollEvent(&data.event))
            {
                ImGui_ImplSDL2_ProcessEvent(&data.event);
                if (data.event.type == SDL_QUIT)
                {
                    data.run = false;
                }
                else if (data.event.type == SDL_KEYDOWN)
                {
                    if (data.event.key.keysym.sym == SDLK_ESCAPE)
                    {
                        data.run = false;
                    }
                }
            }
            if (data.run == false)
            {
                runAudio = false;
                runVideo = false;
            }
            if (drawing.sendFrame == true)
            {
                SDL_RenderPresent(rendererEntry);
                SDL_RenderClear(data.renderer);
                drawing.sendFrame = false;
                drawing.videoD = true;
            }
        }
        thread3.join();
        return 0;
    };
};