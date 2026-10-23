#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/utils/AtomicUi.hpp>

namespace Kotonoha::Debug {

static void DrawPlayback(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
                         Kotonoha_Game &context) {
  if (play == nullptr ||
      !ImGui::CollapsingHeader("Playback Controls",
                               ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  float newTime = play->GetTime();
  const float lastTime = play->GetLastTime();

  ImGui::TextColored(
      ImVec4(0.85f, 0.75f, 0.95f, 1.0f), "Time: %s / %s",
      FormatTime(static_cast<Uint64>(newTime * 1000.0f)).c_str(),
      FormatTime(static_cast<Uint64>(lastTime * 1000.0f)).c_str());

  if (ImGui::SliderFloat("Timeline", &newTime, 0.0f, lastTime)) {
    play->SetTime(newTime);
  }

  const float buttonWidth =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) /
      4.0f;

  if (ImGui::Button("<< 5s", ImVec2(buttonWidth, 0)))
    play->SeekBackward(5000);
  ImGui::SameLine();
  if (ImGui::Button(">> 5s", ImVec2(buttonWidth, 0)))
    play->SeekForward(5000);
  ImGui::SameLine();
  EditAtomic(context.config.paused,
             [](bool *v) { return ImGui::Checkbox("Pause", v); });
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &play->loop);

  if (ImGui::Button("Reset Scene", ImVec2(buttonWidth, 0)))
    play->Reset(true);
  ImGui::SameLine();
  if (ImGui::Button("Next", ImVec2(buttonWidth, 0)))
    context.config.next = true;
  ImGui::SameLine();
  if (ImGui::Button("Previous", ImVec2(buttonWidth, 0)))
    context.config.back = true;
  ImGui::SameLine();
  if (ImGui::Button("Exit", ImVec2(buttonWidth, 0))) {
    GoToScene(game, context, static_cast<int>(game->gameplays.size()) - 1);
    context.config.next = true;
  }
}

void DrawAudio(Kotonoha_Game &context) {
  if (!ImGui::CollapsingHeader("Audio Mixer", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  auto *sound = static_cast<::Kotonoha::Sound *>(context.sound);
  if (sound == nullptr) {
    ImGui::TextUnformatted("Sound system unavailable.");
    return;
  }

  float masterVolume = sound->volume.load(std::memory_order_relaxed);
  if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
    sound->volume.store(masterVolume, std::memory_order_relaxed);
  }

  for (size_t i = 0;; ++i) {
    ::Kotonoha::Sound::Channel *channel = sound->GetChannelByIndex(i);
    if (channel == nullptr)
      break;

    float channelVolume = channel->volume.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(channel->name.c_str(), &channelVolume, 0.0f, 1.0f)) {
      channel->volume.store(channelVolume, std::memory_order_relaxed);
    }
  }
}

void DrawPlaybackWindow(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
                      Kotonoha_Game &context) {
  if (!playbackVisible)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 200, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 440),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  const bool panelVisible =
      ImGui::Begin("Playback & Mixer", &playbackVisible);
  if (panelVisible) {
    if (play == nullptr) {
      ImGui::TextDisabled("No active engine scene.");
    } else {
      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                         "Current Scene: %d / %d",
                         static_cast<int>(context.config.scene) + 1,
                         static_cast<int>(game->gameplays.size()));

      ImGui::TextWrapped("File: %s", play->scriptPath.empty()
                                         ? "<no file path>"
                                         : play->scriptPath.c_str());
      ImGui::Separator();

      DrawPlayback(game, play, context);
    }
    DrawAudio(context);
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
