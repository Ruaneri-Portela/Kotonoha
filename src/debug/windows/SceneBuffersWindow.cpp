#include <Kotonoha/debug/DebugInternal.hpp>

#include <algorithm>

namespace Kotonoha::Debug {
namespace {

char sceneBufferFilter[256] = {};
Uint64 selectedTextureId = 0;
bool texturePreviewVisible = false;

bool MatchesFilter(const ::Kotonoha::MediaBufferInfo &media) {
  return sceneBufferFilter[0] == '\0' ||
         SDL_strcasestr(media.type.c_str(), sceneBufferFilter) != nullptr ||
         SDL_strcasestr(media.channel.c_str(), sceneBufferFilter) != nullptr ||
         SDL_strcasestr(media.path.c_str(), sceneBufferFilter) != nullptr;
}

void DrawTexturePreview(const char *label, SDL_Texture *texture) {
  ImGui::TextUnformatted(label);
  if (texture == nullptr) {
    ImGui::TextDisabled("Texture not available yet.");
    return;
  }

  float textureWidth = 0.0f;
  float textureHeight = 0.0f;
  if (!SDL_GetTextureSize(texture, &textureWidth, &textureHeight)) {
    ImGui::Text("Could not read texture size: %s", SDL_GetError());
    return;
  }
  if (textureWidth <= 0.0f || textureHeight <= 0.0f) {
    ImGui::TextDisabled("Invalid texture size.");
    return;
  }

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float scale =
      std::min(std::max(available.x, 1.0f) / textureWidth,
               std::max(available.y - 28.0f, 1.0f) / textureHeight);
  ImGui::Image(texture, ImVec2(textureWidth * scale, textureHeight * scale));
  ImGui::Text("%.0f x %.0f", textureWidth, textureHeight);
}

void DrawTextureWindow(const std::vector<::Kotonoha::MediaBufferInfo> &media) {
  if (!texturePreviewVisible) {
    return;
  }

  const auto selected = std::find_if(
      media.begin(), media.end(), [](const ::Kotonoha::MediaBufferInfo &item) {
        return item.id == selectedTextureId;
      });

  if (ImGui::Begin("Media Texture", &texturePreviewVisible)) {
    if (selected == media.end()) {
      ImGui::TextDisabled("The selected media is no longer in this scene.");
    } else {
      ImGui::TextWrapped("%s: %s", selected->type.c_str(),
                         selected->path.empty() ? "<no path>"
                                                : selected->path.c_str());
      DrawTexturePreview(selected->type.c_str(), selected->texture);
      for (const ::Kotonoha::MediaTextureInfo &overlay : selected->overlays) {
        DrawTexturePreview(overlay.label.c_str(), overlay.texture);
      }
    }
  }
  ImGui::End();
}

} // namespace

void DrawSceneBuffersWindow(::Kotonoha::Gameplay *play) {
  const bool hasScene = play != nullptr && play->eventManager != nullptr;
  if (!sceneBuffersVisible && !texturePreviewVisible) {
    return;
  }

  std::vector<::Kotonoha::MediaBufferInfo> media;
  if (hasScene) {
    media = play->eventManager->GetMediaBufferInfo(play->audio, play->video,
                                                   play->image);
  }

  if (sceneBuffersVisible) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x * 0.72f, viewport->WorkSize.y * 0.80f),
        ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Buffers", &sceneBuffersVisible)) {
      if (!hasScene) {
        ImGui::TextDisabled("No active scene.");
      } else {
        ImGui::TextWrapped("Scene: %s", play->scriptPath.empty()
                                            ? "<no file path>"
                                            : play->scriptPath.c_str());
        ImGui::Text("Scene time: %.3f s", static_cast<double>(play->GetTime()));

        const std::string nowPlaying =
            play->eventManager->GetMediaNowPath(play->audio);
        if (nowPlaying.empty()) {
          ImGui::TextDisabled("Now playing: -");
        } else {
          ImGui::AlignTextToFramePadding();
          ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f),
                             "Now playing: %s", nowPlaying.c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("Stop")) {
            play->eventManager->StopMediaNow(play->audio);
          }
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##scene_buffer_filter", "Filter media...",
                                 sceneBufferFilter,
                                 IM_ARRAYSIZE(sceneBufferFilter));

        const float tableHeight =
            std::max(ImGui::GetContentRegionAvail().y, 1.0f);
        if (ImGui::BeginTable("##scene_media_buffers", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0.0f, tableHeight))) {
          ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                                  75.0f);
          ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed,
                                  65.0f);
          ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch,
                                  1.0f);
          ImGui::TableSetupColumn("Total (s)", ImGuiTableColumnFlags_WidthFixed,
                                  75.0f);
          ImGui::TableSetupColumn("Played (s)",
                                  ImGuiTableColumnFlags_WidthFixed, 75.0f);
          ImGui::TableSetupColumn("Cache (s)", ImGuiTableColumnFlags_WidthFixed,
                                  75.0f);
          ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed,
                                  55.0f);
          ImGui::TableHeadersRow();

          for (size_t i = 0; i < media.size(); ++i) {
            const ::Kotonoha::MediaBufferInfo &item = media[i];
            if (!MatchesFilter(item)) {
              continue;
            }

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool textureSelected =
                texturePreviewVisible && selectedTextureId == item.id;
            if (ImGui::Selectable(item.type.c_str(), textureSelected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              if (item.type == "Audio" || item.type == "Audio Loop") {
                if (!play->eventManager->PlayMediaNow(play->audio, item.path,
                                                      item.channel)) {
                  SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                               "Could not play selected audio: %s",
                               item.path.c_str());
                }
              } else if (item.type == "Image" || item.type == "Video") {
                selectedTextureId = item.id;
                texturePreviewVisible = true;
              }
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.channel.empty() ? "-"
                                                        : item.channel.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s",
                               item.path.empty() ? "-" : item.path.c_str());
            ImGui::TableNextColumn();
            if (item.totalMs > 0) {
              ImGui::Text("%.3f", static_cast<double>(item.totalMs) / 1000.0);
            } else {
              ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (item.type == "Audio" || item.type == "Audio Loop" ||
                item.type == "Video") {
              ImGui::Text("%.3f", static_cast<double>(item.playedMs) / 1000.0);
            } else {
              ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (item.type == "Audio" || item.type == "Audio Loop" ||
                item.type == "Video") {
              ImGui::Text("%.3f", static_cast<double>(item.cachedMs) / 1000.0);
            } else {
              ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (item.type == "Audio" || item.type == "Audio Loop" ||
                item.type == "Video") {
              ImGui::TextColored(item.eof ? ImVec4(1.0f, 0.55f, 0.45f, 1.0f)
                                          : ImVec4(0.55f, 0.85f, 0.65f, 1.0f),
                                 "%s", item.eof ? "EOF" : "Active");
            } else {
              ImGui::TextDisabled("-");
            }
            ImGui::PopID();
          }

          ImGui::EndTable();
        }
      }
    }
    ImGui::End();
  }

  DrawTextureWindow(media);
}

} // namespace Kotonoha::Debug
