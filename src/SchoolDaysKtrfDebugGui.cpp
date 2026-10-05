#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfDebugGui.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfStateInspector.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

namespace {

bool showKtrfScenes = false;
bool showKtrfState = false;
char sceneFilter[128] = {0};
char debugStatus[512] = {0};

void SetDebugStatus(const char* text) {
    SDL_snprintf(debugStatus, sizeof(debugStatus), "%s", text != nullptr ? text : "");
}

bool JumpToOrdinal(Kotonoha::SchoolDaysKtrfAppController* controller,
                   std::size_t ordinal, Kotonoha::Gameplay*& play) {
    Kotonoha_KtrfError error{};
    if (!controller->DebugJumpToScene(ordinal, &error)) {
        SetDebugStatus(error.message[0] != '\0' ? error.message : "KTRF debug jump failed");
        return false;
    }
    play = controller->CurrentGameplay();
    const auto& scene = controller->CurrentScene();
    SDL_snprintf(debugStatus, sizeof(debugStatus), "Jumped to %s (NODE %u)",
                 scene.sceneKey.c_str(), static_cast<unsigned>(scene.nodeIndex));
    return true;
}

bool JumpRelative(Kotonoha::SchoolDaysKtrfAppController* controller,
                  int delta, Kotonoha::Gameplay*& play) {
    Kotonoha_KtrfError error{};
    if (!controller->DebugJumpRelative(delta, &error)) {
        SetDebugStatus(error.message[0] != '\0' ? error.message : "KTRF relative jump failed");
        return false;
    }
    play = controller->CurrentGameplay();
    const auto& scene = controller->CurrentScene();
    SDL_snprintf(debugStatus, sizeof(debugStatus), "Jumped to %s (NODE %u)",
                 scene.sceneKey.c_str(), static_cast<unsigned>(scene.nodeIndex));
    return true;
}

} // namespace

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Gameplay* play,
    Kotonoha_Game& context) {
    if (!Kotonoha_BasicGuiShow) return true;
    if (controller == nullptr || !controller->IsOpen()) return true;

    play = controller->CurrentGameplay();
    if (play == nullptr) return true;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    bool exitRequested = false;
    const int currentOrdinal = controller->DebugCurrentSceneOrdinal();
    const std::size_t sceneCount = controller->DebugSceneCount();
    const auto& currentScene = controller->CurrentScene();

    ImGui::Begin("Kotonoha - School Days KTRF", &Kotonoha_BasicGuiShow);

    if (currentOrdinal >= 0) {
        ImGui::Text("Current Scene: %d / %d", currentOrdinal + 1,
                    static_cast<int>(sceneCount));
    }
    else {
        ImGui::Text("Current Scene: <not in physical catalog> / %d",
                    static_cast<int>(sceneCount));
    }
    ImGui::Text("KTRF NODE: %u", static_cast<unsigned>(currentScene.nodeIndex));
    ImGui::TextWrapped("Scene Key: %s", currentScene.sceneKey.c_str());
    ImGui::TextWrapped("Current File: %s",
                       play->scriptPath.empty() ? "<no file path>" : play->scriptPath.c_str());

    if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
        float newTime = play->GetTime();
        const float lastTime = play->GetLastTime();
        ImGui::Text("Time: %.2f / %.2f", newTime, lastTime);

        if (ImGui::SliderFloat("Duration", &newTime, 0.0f, lastTime)) {
            play->SetTime(newTime);
        }

        if (ImGui::Button("Back 5s")) play->SeekBackward(5000);
        ImGui::SameLine();
        if (ImGui::Button("Forward 5s")) play->SeekForward(5000);
        ImGui::SameLine();
        ImGui::Checkbox("Paused", &context.paused);
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &play->loop);

        if (ImGui::Button("Reset Scene")) {
            play->Reset(true);
            SetDebugStatus("Current ORS timeline reset.");
        }
        ImGui::SameLine();

        const bool previousDisabled = currentOrdinal <= 0;
        ImGui::BeginDisabled(previousDisabled);
        if (ImGui::Button("Previous Scene")) {
            JumpRelative(controller, -1, play);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool nextDisabled = currentOrdinal < 0 ||
            static_cast<std::size_t>(currentOrdinal + 1) >= sceneCount;
        ImGui::BeginDisabled(nextDisabled);
        if (ImGui::Button("Next Scene")) {
            JumpRelative(controller, +1, play);
        }
        ImGui::EndDisabled();

        if (controller->HasPendingHandoff()) {
            ImGui::SameLine();
            if (controller->PendingHandoffIsTerminal()) {
                ImGui::TextUnformatted("Terminal handoff pending");
            }
            else if (ImGui::Button("Continue Handoff")) {
                Kotonoha_KtrfError error{};
                if (!controller->ContinueHandoff(&error)) {
                    SetDebugStatus(error.message[0] != '\0' ? error.message
                                                            : "KTRF handoff continue failed");
                }
                else {
                    play = controller->CurrentGameplay();
                    SetDebugStatus("Episode handoff continued.");
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Exit")) exitRequested = true;
    }

    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* sound = static_cast<Kotonoha::Sound*>(context.sound);
        if (sound != nullptr) {
            ImGui::SliderFloat("Master", &sound->volume, 0.0f, 1.0f);
            std::size_t count = 0;
            for (Kotonoha::Sound::Channel* channel = sound->GetChannelByIndex(count);
                 channel != nullptr;
                 channel = sound->GetChannelByIndex(count)) {
                ++count;
                ImGui::SliderFloat(channel->name.c_str(), &channel->volume, 0.0f, 1.0f);
            }
        }
        else {
            ImGui::TextUnformatted("Sound system unavailable.");
        }
    }

    if (ImGui::CollapsingHeader("Script / Routing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Scenes", &showKtrfScenes);
        ImGui::SameLine();
        ImGui::Checkbox("Show State Inspector", &showKtrfState);
        ImGui::TextDisabled("KTRF debug jumps preserve current runtime variables and activate the selected NODE.");
        ImGui::TextDisabled("Text Scene Editor is disabled here because .ENG.ORS is a binary asset.");
    }

    if (debugStatus[0] != '\0') {
        ImGui::Separator();
        ImGui::TextWrapped("%s", debugStatus);
    }

    ImGui::End();

    if (showKtrfScenes) {
        ImGui::Begin("Scenes - KTRF", &showKtrfScenes);
        ImGui::Text("Physical scenes: %d", static_cast<int>(sceneCount));
        ImGui::TextWrapped("Current: %s", controller->CurrentScene().sceneKey.c_str());
        ImGui::InputTextWithHint("##ktrf_scene_filter", "filter scene key (e.g. 05/05-9O)",
                                 sceneFilter, IM_ARRAYSIZE(sceneFilter));

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 160.0f) avail.y = 160.0f;

        if (ImGui::BeginListBox("##ktrf_scene_playlist", avail)) {
            for (std::size_t i = 0; i < sceneCount; ++i) {
                const auto* entry = controller->DebugSceneAt(i);
                if (entry == nullptr) continue;

                if (sceneFilter[0] != '\0' &&
                    entry->sceneKey.find(sceneFilter) == std::string::npos) {
                    continue;
                }

                char label[512];
                SDL_snprintf(label, sizeof(label), "%04d  %s  [NODE %u]",
                             static_cast<int>(i + 1), entry->sceneKey.c_str(),
                             static_cast<unsigned>(entry->nodeIndex));
                const bool selected = currentOrdinal >= 0 &&
                                      static_cast<std::size_t>(currentOrdinal) == i;
                if (ImGui::Selectable(label, selected)) {
                    JumpToOrdinal(controller, i, play);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
        ImGui::End();
    }

    if (showKtrfState) {
        Kotonoha_SchoolDaysKtrfStateInspectorRun(controller, context, &showKtrfState);
    }

    ImGui::Render();
    SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
    return !exitRequested;
}
