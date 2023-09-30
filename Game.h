class gameWindows
{
public:
    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    int laucherGame()
    {
        SDL_Init(SDL_INIT_EVERYTHING);
        SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        window = SDL_CreateWindow("Kotonoha Project",  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
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
