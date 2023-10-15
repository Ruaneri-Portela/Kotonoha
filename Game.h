// This file used to Open and Close game contexts
namespace kotonoha
{
    class set
    {
    public:
        SDL_Renderer *renderer = NULL;
        SDL_Window *window = NULL;
        ImGuiIO *io = NULL;
        int laucher()
        {
            SDL_Init(SDL_INIT_EVERYTHING);
            SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
            window = SDL_CreateWindow("Kotonoha Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            Mix_Init(MIX_INIT_OGG);
            Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
            TTF_Init();
            SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
            ImGui::SetCurrentContext(NULL);
            ImGui::CreateContext();
            ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
            ImGui_ImplSDLRenderer2_Init(renderer);
            ImGui::StyleColorsDark();
            io = &ImGui::GetIO();
            (void)io;
            io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            return 0;
        }
        int close()
        {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
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