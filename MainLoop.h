#include "Ors.h"
#include "Ui.h"
class gameLoop
{
public:
    int windowWidth = 1280, windowHeight = 720;
    bool game(SDL_Window *windowEntry, SDL_Renderer *rendererEntry, std::string filename)
    {
        bool run = false;
        gameData data;
        drawControl drawing;
        data.window = windowEntry;
        data.renderer = rendererEntry;
        data.dataDraw = &drawing;
        data.audio0 = new audio::audioObject;
        data.video0 = new video::videoObject;
        SDL_ShowWindow(data.window);
        std::cout << "Game init" << std::endl;
        std::thread thread1(ui, &data);
        ors(&data, filename);
        std::thread thread2(video::play, data.video0->data);
        std::thread thread3(audio::play, data.audio0->data);
        std::cout << "Main loop init" << std::endl;
        while (!drawing.exit)
        {
            while (SDL_PollEvent(&data.event))
            {
                ImGui_ImplSDL2_ProcessEvent(&data.event);
                if (data.event.type == SDL_QUIT)
                {
                    drawing.exit = true;
                }
                else if (data.event.type == SDL_KEYDOWN)
                {
                    if (data.event.key.keysym.sym == SDLK_ESCAPE)
                    {
                        drawing.exit = true;
                    }
                    if (data.event.key.keysym.sym == SDLK_r)
                    {
                        run = 1;
                        drawing.exit = true;
                    }
                }
            }
            if (drawing.reset)
            {
                run = 1;
                drawing.exit = true;
            }
            if (drawing.sendFrame && !drawing.exit)
            {
                SDL_RenderPresent(rendererEntry);
                SDL_RenderClear(data.renderer);
                drawing.sendFrame = false;
                drawing.videoD = true;
            }
            if (drawing.videoE && !drawing.sendFrame)
            {
                drawing.uiD = true;
            }
        }
        thread1.join();
        thread2.join();
        thread3.join();
        return run;
    };
};