int ui(void *import)
{
    gameData *data = static_cast<gameData *>(import);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(data->window, data->renderer);
    ImGui_ImplSDLRenderer2_Init(data->renderer);
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    timeUtils::delay(2000);
    data->dataDraw->timer0.initTimeCapture();
    std::cout << "Ui init" << std::endl;
    while (!data->dataDraw->exit)
    {
        if (data->dataDraw->uiD)
        {
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            {
                ImGui::Begin("Kotonoha Project Visual Novel Engine");
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::Text("Time %.3f ms", data->dataDraw->timer0.pushTime());
                ImGui::Text("Video end %d ", data->dataDraw->videoE);
                ImGui::Text("Audio end %d ", data->dataDraw->audioE);
                ImGui::Text("Image end %d ", data->dataDraw->imageE);
                if (ImGui::Button("End"))
                {
                    data->dataDraw->exit = true;
                }
                if (ImGui::Button("Reset"))
                {
                    data->dataDraw->reset = true;
                }
                ImGui::End();
            }

            ImGui::Render();
            SDL_RenderSetScale(data->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(data->renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            data->dataDraw->uiD = false;
            data->dataDraw->sendFrame = true;
        }
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    std::cout << "Ui end" << std::endl;
    return 0;
};