/**
 * @brief This function initializes and runs the user interface for the Kotonoha Project Visual Novel Engine.
 * 
 * @param import A void pointer to the access mapper for the engine.
 * @return int Returns 0 upon successful completion of the function.
 */
int ui(void *import)
{
    if (import != NULL)
    {
        kotonohaData::acessMapper *mapper = static_cast<kotonohaData::acessMapper *>(import);
        kotonohaData::rootData *data = mapper->root;
        kotonohaData::controlData *control = mapper->control;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplSDL2_InitForSDLRenderer(data->window, data->renderer);
        ImGui_ImplSDLRenderer2_Init(data->renderer);
        ImGui::StyleColorsDark();
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        kotonohaTime::delay(2000);
        data->log0->appendLog("(Ui) - Start");
        control->timer0.initTimeCapture();
        data->log0->appendLog("(Clock) - Start");
        while (!control->exit)
        {
            if (control->display[2])
            {
                ImGui_ImplSDLRenderer2_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();
                {
                    ImGui::Begin("Kotonoha Project Visual Novel Engine");
                    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                    ImGui::Text("Time %.3f s", control->timer0.pushTime());
                    if (!control->nonVideo)
                    {
                        ImGui::Text("Video end %d ", control->videoEnd);
                    }
                    else
                    {
                        ImGui::Text("Video not using in this scene");
                    }
                    if (!control->nonAudio)
                    {
                        ImGui::Text("Audio end %d ", control->audioEnd);
                    }
                    else
                    {
                        ImGui::Text("Audio not using in this scene");
                    }
                    if (!control->nonImage)
                    {
                        ImGui::Text("Image end %d ", control->imageEnd);
                    }
                    else
                    {
                        ImGui::Text("Image not using in this scene");
                    }
                    if (ImGui::Button("End"))
                    {
                        control->exit = true;
                    }
                    if (ImGui::Button("Reset"))
                    {
                        control->reset = true;
                    }
                    if (ImGui::Button("Menu"))
                    {
                        control->menu = true;
                    }
                    if (data->log0->enable)
                    {
                        data->log0->drawLogger();
                    }
                    ImGui::End();
                }
                ImGui::Render();
                SDL_RenderSetScale(data->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                SDL_SetRenderDrawColor(data->renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
                control->display[2] = false;
                control->display[3] = true;
            }
        }
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        data->log0->appendLog("(Ui) - End");
    }
    return 0;
};