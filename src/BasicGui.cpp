#include <Kotonoha/Kotonoha.hpp>
#include <sstream>
#include <string>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

bool Kotonoha_BasicGuiShow = false;
static char scriptBuf[8192];

void Kotonoha_BasicGuiInit(Kotonoha_Game* gameContext)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL3_InitForSDLRenderer(gameContext->window, gameContext->render);
	ImGui_ImplSDLRenderer3_Init(gameContext->render);
}

void Kotonoha_BasicGuiRun(Kotonoha::Kotonoha* game, Kotonoha::Gameplay *play, Kotonoha_Game &context)
{
	if (!Kotonoha_BasicGuiShow)
		return;

	// Start the Dear ImGui frame
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
    if (play != nullptr)
    {

        ImGui::NewFrame();
        ImGui::Begin("Kotonoha");

        snprintf(scriptBuf, sizeof(scriptBuf), "%s", play->script.c_str());

        ImGui::InputTextMultiline(
            "##script",
            scriptBuf,
            IM_ARRAYSIZE(scriptBuf),
            ImVec2(-1.0f, 20.0f),
            ImGuiInputTextFlags_ReadOnly
        );

        ImGui::SliderFloat("Master", &game->sound.volume, 0.0f, 1.0f);
        ImGui::SliderFloat("Voice", &game->Voice->volume, 0.0f, 1.0f);
        ImGui::SliderFloat("BGM", &game->BGM->volume, 0.0f, 1.0f);
        ImGui::SliderFloat("Sound Effect", &game->Se->volume, 0.0f, 1.0f);

        if (ImGui::Button("Reset")) {
            play->tm->seekTime -= Kotonoha_timeGet(play->tm);
            context.softReset = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Back")) {
            Uint64 time = Kotonoha_timeGet(play->tm);
            play->tm->seekTime -= time < 5000 ? time : 5000;
            context.softReset = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Forward")) {
            play->tm->seekTime += 500;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Paused", &context.paused);


        ImGui::SameLine();
        if (ImGui::Button("End")) {
            play->tm->seekTime += SDL_MAX_SINT64;
        }

        ImGui::End();
    }
    else
    {
        ImGui::NewFrame();
        ImGui::Begin("Kotonoha");

        ImGui::InputText("Script Path", scriptBuf, IM_ARRAYSIZE(scriptBuf));
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            std::stringstream ss(scriptBuf);
            std::string part;
            while (std::getline(ss, part, ';'))
            {
                if (!part.empty())
                {
                    game->loadScriptFile(part.c_str());
                }
            }
        }
        ImGui::End();
    }
	ImGui::Render();
	SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
}

void Kotonoha_BasicGuiEvent(SDL_Event* event) {
	ImGui_ImplSDL3_ProcessEvent(event);
}