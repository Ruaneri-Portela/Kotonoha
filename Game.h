class gameWindows
{
public:
    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    int laucherGame()
    {
        SDL_Init(SDL_INIT_EVERYTHING);
        window = SDL_CreateWindow("Kotonoha Project", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 460, SDL_WINDOW_HIDDEN);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        Mix_Init(MIX_INIT_OGG);
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
        TTF_Init();
        return 0;
    }
    int closeGame()
    {
        SDL_DestroyWindow(window);
        SDL_DestroyRenderer(renderer);
        Mix_CloseAudio();
        Mix_Quit();
        TTF_Quit();
        SDL_Quit();
        return 0;
    }
};
