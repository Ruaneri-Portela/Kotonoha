#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfDebugGui.hpp>
#include <Kotonoha/routing/KtrfSceneTransaction.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfStateInspector.hpp>
#include <Kotonoha/debug/DebugUi.hpp>

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

void DrawKtrfDebugMenu(void*, ImGuiContext* context) {
    // The runtime owns the ImGui context. The executable links the same ImGui
    // implementation for game-only debug windows, so synchronize its module-
    // local current-context pointer before issuing any ImGui calls here.
    ImGui::SetCurrentContext(context);
    ImGui::Checkbox("KTRF Scene List", &showKtrfScenes);
    ImGui::Checkbox("KTRF State Inspector", &showKtrfState);
}

void DrawPlayDataDebugMenu(void*, ImGuiContext* context) {
    ImGui::SetCurrentContext(context);
    ImGui::Checkbox("KTRF State Inspector", &showKtrfState);
}

template <typename Action>
bool RunDebugSceneMutation(Kotonoha::SchoolDaysKtrfAppController* controller,
                           Kotonoha::Gameplay*& play,
                           Kotonoha::KtrfSceneTransaction* transaction,
                           Kotonoha_Game& context,
                           const char* reason,
                           Action action) {
    if (controller == nullptr) return false;
    Kotonoha::Gameplay* before = controller->CurrentGameplay();
    bool preparedHere = false;

    if (transaction != nullptr && before != nullptr) {
        const bool alreadyRetained = transaction->HasLease() ||
            transaction->HasPreparedFrameFor(before);
        if (!alreadyRetained) {
            if (context.render == nullptr) {
                SetDebugStatus("Cannot change scene without a renderer for transition capture.");
                return false;
            }
            SDL_SetRenderTarget(context.render, nullptr);
            if (!transaction->PrepareExplicit(context.render, before, reason)) {
                SetDebugStatus("Could not retain the outgoing frame for the debug scene change.");
                return false;
            }
            preparedHere = true;
        }
    }

    if (!action()) {
        if (preparedHere && transaction != nullptr) transaction->Reset();
        return false;
    }

    play = controller->CurrentGameplay();
    if (transaction != nullptr && context.render != nullptr && before != nullptr) {
        transaction->CommitIfSwapped(before, play, reason);
    }
    return true;
}

bool JumpToOrdinal(Kotonoha::SchoolDaysKtrfAppController* controller,
                   std::size_t ordinal, Kotonoha::Gameplay*& play,
                   Kotonoha::KtrfSceneTransaction* transaction,
                   Kotonoha_Game& context) {
    return RunDebugSceneMutation(
        controller, play, transaction, context, "debug-scene-jump", [&]() {
            Kotonoha_KtrfError error{};
            if (!controller->DebugJumpToScene(ordinal, &error)) {
                SetDebugStatus(error.message[0] != '\0' ? error.message
                                                        : "KTRF debug jump failed");
                return false;
            }
            const auto& scene = controller->CurrentScene();
            SDL_snprintf(debugStatus, sizeof(debugStatus),
                         "Jumped to %s (NODE %u)", scene.sceneKey.c_str(),
                         static_cast<unsigned>(scene.nodeIndex));
            return true;
        });
}

bool JumpRelative(Kotonoha::SchoolDaysKtrfAppController* controller,
                  int delta, Kotonoha::Gameplay*& play,
                  Kotonoha::KtrfSceneTransaction* transaction,
                  Kotonoha_Game& context) {
    return RunDebugSceneMutation(
        controller, play, transaction, context, "debug-relative-jump", [&]() {
            Kotonoha_KtrfError error{};
            if (!controller->DebugJumpRelative(delta, &error)) {
                SetDebugStatus(error.message[0] != '\0' ? error.message
                                                        : "KTRF relative jump failed");
                return false;
            }
            const auto& scene = controller->CurrentScene();
            SDL_snprintf(debugStatus, sizeof(debugStatus),
                         "Jumped to %s (NODE %u)", scene.sceneKey.c_str(),
                         static_cast<unsigned>(scene.nodeIndex));
            return true;
        });
}

} // namespace

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Kotonoha* game,
    Kotonoha::Gameplay* play,
    Kotonoha::SchoolDaysKtrfSession* temporarySession,
    Kotonoha::KtrfSceneTransaction* transaction,
    Kotonoha_Game& context) {
    if (Kotonoha::Debug::SyncActivation(context)) {
        showKtrfScenes = false;
        showKtrfState = false;
    }
    if (controller == nullptr || !controller->IsOpen()) return true;

    const auto flowState = controller->FlowState();
    const bool titleFrontend =
        flowState == Kotonoha::SchoolDaysFlowController::State::Title ||
        flowState == Kotonoha::SchoolDaysFlowController::State::TitleLoad ||
        flowState == Kotonoha::SchoolDaysFlowController::State::TitleRouteMap ||
        flowState == Kotonoha::SchoolDaysFlowController::State::TitleConfig ||
        flowState == Kotonoha::SchoolDaysFlowController::State::TitleExit;
    const bool temporaryPlayback = titleFrontend && play != nullptr;
    if (!context.config.enableDebugUi) {
        Kotonoha::Debug::Run(game, temporaryPlayback ? play : nullptr, context,
                             titleFrontend && !temporaryPlayback);
        return true;
    }

    if (temporaryPlayback) {
        // Replay/HScene and PlayData live outside the retained narrative
        // controller. Their Gameplay is nevertheless a normal engine Gameplay,
        // so expose it to the generic Playback/Scene Buffers/Canvas tools.
        // Ordinary Replay has no private KTRF runtime. PlayData does, so expose
        // only the read-only state inspector bound to that exact temporary
        // session; never display the retained controller state in its place.
        Kotonoha::Debug::BeginWorkspaceFrame(
            context, temporarySession != nullptr ? &DrawPlayDataDebugMenu : nullptr,
            nullptr);
        if (temporarySession != nullptr && showKtrfState) {
            Kotonoha_SchoolDaysKtrfStateInspectorRunSession(
                temporarySession, context, &showKtrfState);
        }
        bool shouldExit = false;
        const bool auxiliaryWindowsContinue =
            Kotonoha::Debug::DrawAuxiliaryWindows(
                game, play, context, false, shouldExit,
                context.config.enableDebugUi);
        const bool frameRendered = Kotonoha::Debug::FinishWorkspaceFrame(context);
        Kotonoha::Debug::UpdateFocusState();
        return frameRendered && auxiliaryWindowsContinue && !shouldExit;
    }

    play = titleFrontend ? nullptr : controller->CurrentGameplay();
    if (play == nullptr && !titleFrontend) return true;

    Kotonoha::Debug::BeginWorkspaceFrame(
        context, &DrawKtrfDebugMenu, nullptr);

    if (titleFrontend) {
        bool shouldExit = false;
        if (context.config.enableDebugUi) {
            if (showKtrfScenes) {
                const int currentOrdinal = controller->DebugCurrentSceneOrdinal();
                const std::size_t sceneCount = controller->DebugSceneCount();
                ImGui::Begin("KTRF Scene List", &showKtrfScenes);
                ImGui::Text("Physical scenes: %d",
                            static_cast<int>(sceneCount));
                ImGui::TextDisabled(
                    "Scene jumps are available while KTRF Gameplay is active.");
                ImGui::InputTextWithHint(
                    "##ktrf_scene_filter", "filter scene key", sceneFilter,
                    IM_ARRAYSIZE(sceneFilter));
                if (ImGui::BeginListBox("##ktrf_scene_playlist")) {
                    for (std::size_t i = 0; i < sceneCount; ++i) {
                        const auto* entry = controller->DebugSceneAt(i);
                        if (entry == nullptr ||
                            (sceneFilter[0] != '\0' &&
                             entry->sceneKey.find(sceneFilter) ==
                                 std::string::npos)) {
                            continue;
                        }
                        char label[512];
                        SDL_snprintf(label, sizeof(label), "%04d  %s  [NODE %u]",
                                     static_cast<int>(i + 1),
                                     entry->sceneKey.c_str(),
                                     static_cast<unsigned>(entry->nodeIndex));
                        ImGui::Selectable(
                            label,
                            currentOrdinal >= 0 &&
                                static_cast<std::size_t>(currentOrdinal) == i,
                            ImGuiSelectableFlags_Disabled);
                    }
                    ImGui::EndListBox();
                }
                ImGui::End();
            }
            if (showKtrfState) {
                Kotonoha_SchoolDaysKtrfStateInspectorRun(
                    controller, context, &showKtrfState);
            }
        }
        const bool auxiliaryWindowsContinue =
            Kotonoha::Debug::DrawAuxiliaryWindows(
                game, nullptr, context, true, shouldExit,
                context.config.enableDebugUi);
        const bool frameRendered =
            Kotonoha::Debug::FinishWorkspaceFrame(context);
        Kotonoha::Debug::UpdateFocusState();
        return frameRendered && auxiliaryWindowsContinue && !shouldExit;
    }

    bool exitRequested = false;
    const int currentOrdinal = controller->DebugCurrentSceneOrdinal();
    const std::size_t sceneCount = controller->DebugSceneCount();
    const auto& currentScene = controller->CurrentScene();

    bool& playbackVisible = Kotonoha::Debug::PlaybackVisibility();
    if (playbackVisible) {
    ImGui::Begin("KTRF Playback & Mixer", &playbackVisible);

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
        if (ImGui::Button("Forward 5s")) {
            play->SetTime(SDL_min(play->GetTime() + 5.0f, play->GetLastTime()));
        }
        ImGui::SameLine();
        bool paused = context.config.paused.load();
        if (ImGui::Checkbox("Paused", &paused)) {
            context.config.paused.store(paused);
        }
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
            JumpRelative(controller, -1, play, transaction, context);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool nextDisabled = currentOrdinal < 0 ||
            static_cast<std::size_t>(currentOrdinal + 1) >= sceneCount;
        ImGui::BeginDisabled(nextDisabled);
        if (ImGui::Button("Next Scene")) {
            JumpRelative(controller, +1, play, transaction, context);
        }
        ImGui::EndDisabled();

        if (controller->HasPendingHandoff()) {
            ImGui::SameLine();
            if (controller->PendingHandoffIsTerminal()) {
                ImGui::TextUnformatted("Terminal handoff pending");
            }
            else if (ImGui::Button("Continue Handoff")) {
                RunDebugSceneMutation(
                    controller, play, transaction, context,
                    "debug-episode-handoff", [&]() {
                        Kotonoha_KtrfError error{};
                        if (!controller->ContinueHandoff(&error)) {
                            SetDebugStatus(error.message[0] != '\0'
                                               ? error.message
                                               : "KTRF handoff continue failed");
                            return false;
                        }
                        SetDebugStatus("Episode handoff continued.");
                        return true;
                    });
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Exit")) exitRequested = true;
    }

    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto* sound = static_cast<Kotonoha::Sound*>(context.sound);
        if (sound != nullptr) {
            float masterVolume = sound->volume.load(std::memory_order_relaxed);
            if (ImGui::SliderFloat("Master", &masterVolume, 0.0f, 1.0f)) {
                sound->volume.store(masterVolume, std::memory_order_relaxed);
            }
            std::size_t count = 0;
            for (Kotonoha::Sound::Channel* channel = sound->GetChannelByIndex(count);
                 channel != nullptr;
                 channel = sound->GetChannelByIndex(count)) {
                ++count;
                float channelVolume = channel->volume.load(std::memory_order_relaxed);
                if (ImGui::SliderFloat(channel->name.c_str(), &channelVolume, 0.0f, 1.0f)) {
                    channel->volume.store(channelVolume, std::memory_order_relaxed);
                }
            }
        }
        else {
            ImGui::TextUnformatted("Sound system unavailable.");
        }
    }

    if (ImGui::CollapsingHeader("Script / Routing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("KTRF debug jumps preserve runtime variables, sync ROUTE/SCENE to the selected scene, and activate its NODE.");
        ImGui::TextDisabled("Text Scene Editor is disabled here because .ENG.ORS is a binary asset.");
    }

    if (debugStatus[0] != '\0') {
        ImGui::Separator();
        ImGui::TextWrapped("%s", debugStatus);
    }

    ImGui::End();
    }

    if (showKtrfScenes) {
        ImGui::Begin("KTRF Scene List", &showKtrfScenes);
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
                    JumpToOrdinal(controller, i, play, transaction, context);
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

    const bool auxiliaryWindowsContinue =
        Kotonoha::Debug::DrawAuxiliaryWindows(
            game, play, context, false, exitRequested);

    const bool frameRendered = Kotonoha::Debug::FinishWorkspaceFrame(context);
    Kotonoha::Debug::UpdateFocusState();
    return frameRendered && auxiliaryWindowsContinue && !exitRequested;
}
