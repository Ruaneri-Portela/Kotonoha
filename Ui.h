/**
 * @brief This function initializes and runs the user interface for the Kotonoha Project Visual Novel Engine.
 *
 * @param import A void pointer to the access mapper for the engine.
 * @return int Returns 0 upon successful completion of the function.
 */
int ui(void *import)
{
    kotonohaData::acessMapper *mapper = static_cast<kotonohaData::acessMapper *>(import);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForSDLRenderer(mapper->root->window, mapper->root->renderer);
    ImGui_ImplSDLRenderer2_Init(mapper->root->renderer);
    ImGui::StyleColorsDark();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    kotonohaTime::delay(2000);
    mapper->root->log0->appendLog("(Ui) - Start");
    mapper->root->log0->appendLog("(Clock) - Start");
    mapper->control->timer0.initTimeCapture();
    while (mapper->control->outCode == -1)
    {
        if (mapper->control->display[2])
        {
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            {
                ImGui::Begin("Kotonoha Project Visual Novel Engine");
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::Text("Time %.3f s", mapper->control->timer0.pushTime());
                if (!mapper->control->nonVideo)
                {
                    ImGui::Text("Video end %d ", mapper->control->videoEnd);
                }
                else
                {
                    ImGui::Text("Video not using in this scene");
                }
                if (!mapper->control->nonAudio)
                {
                    ImGui::Text("Audio end %d ", mapper->control->audioEnd);
                }
                else
                {
                    ImGui::Text("Audio not using in this scene");
                }
                if (!mapper->control->nonImage)
                {
                    ImGui::Text("Image end %d ", mapper->control->imageEnd);
                }
                else
                {
                    ImGui::Text("Image not using in this scene");
                }
                if (ImGui::Button("End"))
                {
                    mapper->control->outCode = 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset"))
                {
                    mapper->control->outCode = 2;
                }
                ImGui::SameLine();
                if (ImGui::Button("Menu"))
                {
                    mapper->control->outCode = 3;
                }
                ImGui::SameLine();
                if (mapper->root->log0->enable)
                {
                    mapper->root->log0->drawLogger();
                }
                ImGui::SameLine();
                if (ImGui::Button("Pause"))
                {
                }
                ImGui::SameLine();
                if (ImGui::Button("Mute"))
                {
                }
                if (mapper->control->endTime < mapper->control->timer0.pushTime())
                {
                    mapper->control->outCode = 4;
                }
                ImGui::Checkbox("Enable Log", &mapper->root->log0->enable);
                ImGui::End();
            }
            ImGui::Render();
            SDL_RenderSetScale(mapper->root->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(mapper->root->renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(mapper->root->renderer);
            SDL_RenderClear(mapper->root->renderer);
            mapper->control->display[2] = false;
            mapper->control->display[0] = true;
        }
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    mapper->root->log0->appendLog("(Ui) - End");
    return 0;
};