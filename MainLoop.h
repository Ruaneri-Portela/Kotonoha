#include "Ors.h"
class gameLoop
{
public:
    int game(SDL_Window *windowEntry, SDL_Renderer *rendererEntry)
    {
        gameData data;
        data.window = windowEntry;
        data.renderer = rendererEntry;
        SDL_ShowWindow(data.window);
        SDL_RenderClear(data.renderer);
        ors(&data);
        SDL_Thread* thread1 = SDL_CreateThread(audio::play, "t1", data.audio0.data);
        SDL_Thread* thread2 = SDL_CreateThread(video::play, "t2", data.video0.data);
        while (data.run)
        {
            while (SDL_PollEvent(&data.event))
            {
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
            try
            {
                SDL_RenderPresent(rendererEntry);
            }
            catch (const std::exception &e)
            {
            }
            if(data.run == false){
                runAudio = false;
                runVideo = false;
            }
        }
        return 0;
    };
};