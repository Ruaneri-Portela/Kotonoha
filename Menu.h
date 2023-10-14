namespace kotonoha
{
    typedef struct menuReturn
    {
        int returnCode = 0;
        std::string filenameString = "";
        bool debugPromptEnabled = false;
        kotonohaData::configsData configs;
    } menuReturn;
    kotonohaData::configsData fileConfig(int parm, kotonohaData::configsData configs = {false, "", "", "", ""})
    {
        FILE *dataR = fopen("kotonoha.ckot", "rb");
        if (dataR == NULL or parm == 1)
        {
            FILE *dataW = fopen("kotonoha.dat", "wb");
            fwrite(&configs, sizeof(kotonohaData::configsData), 1, dataW);
            fclose(dataW);
        }
        else
        {
            fread(&configs, sizeof(kotonohaData::configsData), 1, dataR);
        }
        fclose(dataR);
        return configs;
    }
    menuReturn menu(SDL_Window *window, SDL_Renderer *renderer, int gameReturn, kotonoha::logger *log0)
    {
        log0->appendLog("(Menu) - Entry point to menu");
        menuReturn object = {0, "", true};
        object.configs = fileConfig(0);
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
        bool config = false;
        bool configSaved = false;
        bool returnPrompt = true;
        char scriptPath[256] = "";
        char mediaPath[256] = "";
        char audioExtension[32] = "";
        char videoExtension[32] = "";
        char imageExtension[32] = "";
        char soundFe0[256] = "";
        char stylesPath[256] = "";
        if (object.configs.configured)
        {
            strcpy(audioExtension, object.configs.audioExtension);
            strcpy(videoExtension, object.configs.videoExtension);
            strcpy(imageExtension, object.configs.imageExtension);
            strcpy(mediaPath, object.configs.mediaPath);
            strcpy(soundFe0, object.configs.soundFe0);
            strcpy(stylesPath, object.configs.stylesPath);
        }
        SDL_Event event;
        while (true)
        {
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                event.type == SDL_QUIT ? SDL_Quit() : (void)0;
                if (event.type == SDL_KEYDOWN)
                {
                    event.key.keysym.sym == SDLK_ESCAPE ? object.returnCode = 1 : 0;
                    event.key.keysym.sym == SDLK_RETURN ? object.returnCode = 1 : 0;
                }
            }
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            /// Start Windows
            // Windows main
            ImGui::Begin("Kotonoha Project, A visual novel engine");
            ImGui::Text("Copy the script path from text box e press start!");
            ImGui::InputText("Script file path", scriptPath, 256);
            ImGui::Checkbox("About", &about);
            ImGui::Checkbox("Enable logger", &log0->enable);
            if (object.configs.configured)
            {
                if (ImGui::Button("Start"))
                {
                    object.filenameString = scriptPath;
                    object.returnCode = 2;
                    break;
                }
                ImGui::SameLine();
                ImGui::Button("Configs") ? config = true : 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                object.returnCode = 1;
                break;
            }
            ImGui::End();
            // Windows about
            if (about)
            {
                ImGui::Begin("About");
                ImGui::Text("Kotonoha Project, A visual novel engine");
                ImGui::Text(kotonoha::version.c_str());
                ImGui::Text("Developed by: Ruaneri F.Portela");
                ImGui::Text("Using SDL, FFMPEG, ASSLIB e IMGui");
                ImGui::Button("Close") ? about = false : 0;
                ImGui::End();
            }
            // Windows set config
            if (config or !object.configs.configured)
            {
                ImGui::Begin("Config");
                ImGui::InputText("Media path", mediaPath, 256);
                ImGui::InputText("Audio extension", audioExtension, 32);
                ImGui::InputText("Video extension", videoExtension, 32);
                ImGui::InputText("Image extension", imageExtension, 32);
                ImGui::InputText("Sound Effect 1", soundFe0, 256);
                ImGui::InputText("File with text Styles", stylesPath, 256);
                if (ImGui::Button("Save"))
                {
                    object.configs.configured = true;
                    strcpy(object.configs.audioExtension, audioExtension);
                    strcpy(object.configs.videoExtension, videoExtension);
                    strcpy(object.configs.imageExtension, imageExtension);
                    strcpy(object.configs.mediaPath, mediaPath);
                    strcpy(object.configs.soundFe0, soundFe0);
                    strcpy(object.configs.stylesPath, stylesPath);
                    object.configs.configured = true;
                    fileConfig(1, object.configs);
                    configSaved = true;
                }
                ImGui::SameLine();
                if (object.configs.configured)
                {
                    ImGui::Button("Close") ? config = false : 0;
                }
                ImGui::End();
            }
            // Windows Alert Need Config
            if (!object.configs.configured)
            {
                ImGui::Begin("Alert");
                ImGui::Text("You need set configs before start");
                ImGui::End();
            }
            // Windows Config saved
            if (configSaved)
            {
                ImGui::Begin("Alert");
                ImGui::Text("Config saved");
                ImGui::Button("Close") ? configSaved = false : 0;
                ImGui::End();
            }
            // Windows Log
            if (log0->enable)
            {
                log0->drawLogger();
            }
            // Windows End Scene
            if (gameReturn >= 3 && returnPrompt)
            {
                ImGui::Begin("Alert");
                ImGui::Text("The scene end, return code %d", gameReturn);
                ImGui::Button("Close") ? returnPrompt = false : 0;
                ImGui::End();
            }
            /// End Windows
            ImGui::Render();
            SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(renderer);
        }
        log0->appendLog("(Menu) - Menu out");
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return object;
    }
}