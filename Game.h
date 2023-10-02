namespace kotonoha{
/**
 * @brief The set class contains functions to initialize and close SDL, Mixer and TTF libraries.
 */
class set
{
public:
    SDL_Renderer *renderer = NULL; /**< Pointer to SDL_Renderer object. */
    SDL_Window *window = NULL; /**< Pointer to SDL_Window object. */
    /**
     * @brief Initializes SDL, Mixer and TTF libraries and creates a window and renderer.
     * @return 0 if successful, -1 otherwise.
     */
    int laucher()
    {
        SDL_Init(SDL_INIT_EVERYTHING);
        SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        window = SDL_CreateWindow("Kotonoha Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        Mix_Init(MIX_INIT_OGG);
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
        TTF_Init();
        return 0;
    }
    /**
     * @brief Closes the window, renderer, Mixer and TTF libraries.
     * @return 0 if successful, -1 otherwise.
     */
    int close()
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
}