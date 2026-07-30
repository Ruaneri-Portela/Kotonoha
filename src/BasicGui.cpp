#include "Kotonoha/components/Sound.hpp"
#include <Kotonoha/Kotonoha.hpp>
#include <cstddef>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <sstream>
#include <string>

bool Kotonoha_BasicGuiShow = false;
bool showError = false;
static char scriptBuf[8192];

void Kotonoha_BasicGuiInit(Kotonoha_Game &gameContext) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
  ImGui_ImplSDLRenderer3_Init(gameContext.render);
}

void Kotonoha_BasicGuiRun(Kotonoha::Kotonoha *game, Kotonoha::Gameplay *play,
                          Kotonoha_Game &context) {
  if (!Kotonoha_BasicGuiShow)
    return;

  // Start the Dear ImGui frame
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  if (play != nullptr) {

    ImGui::NewFrame();
    ImGui::Begin("Kotonoha");

    snprintf(scriptBuf, sizeof(scriptBuf), "%s", play->script.c_str());

    ImGui::InputTextMultiline("##script", scriptBuf, IM_ARRAYSIZE(scriptBuf),
                              ImVec2(-1.0f, 20.0f),
                              ImGuiInputTextFlags_ReadOnly);

    Kotonoha::Sound *sound = static_cast<Kotonoha::Sound *>(context.sound);
    ImGui::SliderFloat("Master", &sound->volume, 0.0f, 1.0f);
    size_t count = 0;
    for (Kotonoha::Sound::Channel *channel = sound->GetChannelByIndex(count);
         channel != nullptr; channel = sound->GetChannelByIndex(count)) {
      count++;
      ImGui::SliderFloat(channel->name.c_str(), &channel->volume, 0.0f, 1.0f);
    }

    float newTime = play->GetTime();
    if (ImGui::SliderFloat("Duration", &newTime, 0.0f, play->GetLastTime())) {
        play->Reset();
        play->SetTime(newTime);
    }

    if (ImGui::Button("Back")) {
      play->SeekBackward(5000);
    }

    ImGui::SameLine();
    if (ImGui::Button("Forward")) {
      play->SeekForward(5000);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Paused", &context.paused);

    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      play->Reset();
    }

    ImGui::SameLine();
    if (ImGui::Button("Next")) {
      context.next = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Return")) {
      context.back = true;
    }

    ImGui::End();
  } else {
    ImGui::NewFrame();
    ImGui::Begin("Kotonoha");

    ImGui::InputText("Script Path", scriptBuf, IM_ARRAYSIZE(scriptBuf));
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
      std::stringstream ss(scriptBuf);
      std::string part;
      while (std::getline(ss, part, ';')) {
        if (!part.empty()) {
          game->ClearGameplays();
          showError = !game->loadScriptFile(part.c_str());
        }
      }
    }
    if (showError) {
      ImGui::Text("Error for open last file!");
    }
    if (!game->gameplays.empty()) {
      if (ImGui::Button("Restart")) {
        context.scene = 0;
      }
    }

    ImGui::End();
  }
  ImGui::Render();
  SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
}

void Kotonoha_BasicGuiEvent(SDL_Event *event) {
  ImGui_ImplSDL3_ProcessEvent(event);
}