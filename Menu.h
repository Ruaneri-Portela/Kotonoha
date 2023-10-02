namespace kotonoha
{
    typedef struct menuReturn
    {
        int returnCode = 0;
        std::string filenameString = "";
        bool debugPromptEnabled = false;
    } menuReturn;
    menuReturn menu(SDL_Window *window, SDL_Renderer *renderer, int gameReturn, kotonoha::logger *log0)
    {
        menuReturn object = {0, ""};
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        ImGui::StyleColorsDark();
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        bool about = false;
        bool returnPrompt = true;
        char filepath[256] = "";
        while (true)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                {
                    object.returnCode = 1;
                    break;
                }
                if (event.type == SDL_KEYDOWN)
                {
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                    {
                        object.returnCode = 1;
                        break;
                    }
                    if (event.key.keysym.sym == SDLK_RETURN)
                    {
                        object.returnCode = 1;
                        break;
                    }
                }
            }
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            ///
            // Windows main
            {
                ImGui::Begin("Kotonoha Project, A visual novel engine");
                ImGui::Text("Copy the script path from text box e press start!");
                // ImGui::Checkbox("Enable Debug Prompt", &object.debugPromptEnabled);
                ImGui::InputText("Scirpt file path", filepath, 256);
                ImGui::Checkbox("About", &about);
                ImGui::Checkbox("Enable logger", &log0->enable);
                if (ImGui::Button("Start"))
                {
                    object.filenameString = filepath;
                    object.returnCode = 2;
                    break;
                }
                if (ImGui::Button("Close"))
                {
                    object.returnCode = 1;
                    break;
                }
                ImGui::End();
            }
            // Windows about
            if (about)
            {
                ImGui::Begin("About");
                ImGui::Text("Kotonoha Project, A visual novel engine");
                ImGui::Text("Version 0.1 alpha");
                ImGui::Text("Developed by: Ruaneri F.Portela");
                ImGui::Text("Using SDL, FFMPEG, ASSLIB e IMGui");
                if (ImGui::Button("Close"))
                {
                    about = false;
                }
                ImGui::End();
            }
            ///
            if (log0->enable)
            {
                log0->drawLogger();
            }
            /// Windows End Scene
            if (gameReturn == 3 && returnPrompt)
            {
                ImGui::Begin("Scene End");
                ImGui::Text("The scene as end, return code %d", gameReturn);
                if (ImGui::Button("Close"))
                {
                    returnPrompt = false;
                }
                ImGui::End();
            }
            //
            ImGui::Render();
            SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(renderer);
        }
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return object;
    }
}