/**
 * @file Menu.h
 * @brief Contains the definition of the menu function and the menuReturn struct.
 *
 * The menu function creates a graphical user interface using the ImGui library and allows the user to select a script file path.
 * The menuReturn struct contains the return code, the selected file path and a boolean flag for enabling the debug prompt.
 */
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
        FILE *dataR = fopen("kotonoha.dat", "rb");
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
        if (object.configs.configured)
        {
            strcpy(audioExtension, object.configs.audioExtension);
            strcpy(videoExtension, object.configs.videoExtension);
            strcpy(imageExtension, object.configs.imageExtension);
            strcpy(mediaPath, object.configs.mediaPath);
        }
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
            /// Start Windows
            // Windows main
            {
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
                    if (ImGui::Button("Configs"))
                    {
                        config = true;
                    }
                }
                ImGui::SameLine();
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
            //Windows set config
            {
                if (config or !object.configs.configured)
                {
                    ImGui::Begin("Config");
                    ImGui::InputText("Media path", mediaPath, 256);
                    ImGui::InputText("Audio extension", audioExtension, 32);
                    ImGui::InputText("Video extension", videoExtension, 32);
                    ImGui::InputText("Image extension", imageExtension, 32);
                    if (ImGui::Button("Save"))
                    {
                        object.configs.configured = true;
                        strcpy(object.configs.audioExtension, audioExtension);
                        strcpy(object.configs.videoExtension, videoExtension);
                        strcpy(object.configs.imageExtension, imageExtension);
                        strcpy(object.configs.mediaPath, mediaPath);
                        object.configs.configured = true;
                        fileConfig(1, object.configs);
                        configSaved = true;
                    }
                    ImGui::SameLine();
                    if (object.configs.configured)
                    {
                        if (ImGui::Button("Close"))
                        {
                            config = false;
                        }
                    }
                    ImGui::End();
                }
            }
            //Windows Alert Need Config
            {
                if (!object.configs.configured)
                {
                    ImGui::Begin("Alert");
                    ImGui::Text("You need set configs before start");
                    ImGui::End();
                }
            }
            //Windows Config saved
            {
                if(configSaved){
                    ImGui::Begin("Alert");
                    ImGui::Text("Config saved");
                    if(ImGui::Button("Close")){
                        configSaved = false;
                    }
                    ImGui::End();
                }
            }
            //Windows Log
            if (log0->enable)
            {
                log0->drawLogger();
            }
            // Windows End Scene
            if (gameReturn >= 3 && returnPrompt)
            {
                ImGui::Begin("Alert");
                ImGui::Text("The scene end, return code %d", gameReturn);
                if (ImGui::Button("Close"))
                {
                    returnPrompt = false;
                }
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