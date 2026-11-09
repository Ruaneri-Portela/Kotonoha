#include <Kotonoha/debug/VoiceDubbingApplet.hpp>

#include <Kotonoha/core/AssetOverlay.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>

#include "ImGuiTheme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace Kotonoha {
namespace {
struct ImGuiComboScope {
  bool open = false;
  void close() {
    if (open) {
      ImGui::EndCombo();
      open = false;
    }
  }
  ~ImGuiComboScope() { close(); }
};

struct ImGuiTableScope {
  bool open = false;
  void close() {
    if (open) {
      ImGui::EndTable();
      open = false;
    }
  }
  ~ImGuiTableScope() { close(); }
};

struct ImGuiIdScope {
  explicit ImGuiIdScope(const char *id) { ImGui::PushID(id); }
  ~ImGuiIdScope() { ImGui::PopID(); }
};

bool audioName(std::string id) {
  std::transform(id.begin(), id.end(), id.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  static const char *extensions[] = {".wav", ".ogg", ".opus", ".mp3", ".flac",
                                     ".m4a", ".aac", ".aiff", ".nwa", ".adx",
                                     ".hca", ".at3", ".vag",  ".wma"};
  return std::any_of(
      std::begin(extensions), std::end(extensions), [&](const char *extension) {
        return id.size() >= std::strlen(extension) &&
               id.compare(id.size() - std::strlen(extension),
                          std::strlen(extension), extension) == 0;
      });
}

double duration(const AudioClip &clip) {
  const int frame = SDL_AUDIO_BYTESIZE(clip.spec.format) * clip.spec.channels;
  return frame > 0 && clip.spec.freq > 0
             ? double(clip.data.size() / std::size_t(frame)) / clip.spec.freq
             : 0;
}

AudioClip asFloat(AudioClip clip) {
  if (clip.spec.format == SDL_AUDIO_F32)
    return clip;
  SDL_AudioSpec target{SDL_AUDIO_F32, clip.spec.channels, clip.spec.freq};
  AudioStreamPtr converter(SDL_CreateAudioStream(&clip.spec, &target));
  if (!converter ||
      !SDL_PutAudioStreamData(converter.get(), clip.data.data(),
                              static_cast<int>(clip.data.size())) ||
      !SDL_FlushAudioStream(converter.get()))
    throw std::runtime_error(SDL_GetError());
  AudioClip result{target, {}};
  const int available = SDL_GetAudioStreamAvailable(converter.get());
  if (available <= 0)
    throw std::runtime_error("Audio conversion produced no samples");
  result.data.resize(available);
  const int got =
      SDL_GetAudioStreamData(converter.get(), result.data.data(), available);
  if (got <= 0)
    throw std::runtime_error(SDL_GetError());
  result.data.resize(got);
  return result;
}

std::vector<float> peaks(const AudioClip &clip, std::size_t buckets = 720) {
  if (clip.spec.format != SDL_AUDIO_F32 || clip.spec.channels <= 0)
    return {};
  const auto *samples = reinterpret_cast<const float *>(clip.data.data());
  const std::size_t frames =
      clip.data.size() / (sizeof(float) * clip.spec.channels);
  if (!frames)
    return {};
  buckets = std::min(buckets, frames);
  std::vector<float> result(buckets);
  for (std::size_t b = 0; b < buckets; ++b) {
    const std::size_t first = b * frames / buckets;
    const std::size_t last = std::max(first + 1, (b + 1) * frames / buckets);
    float peak = 0;
    for (std::size_t f = first; f < last; ++f)
      for (int c = 0; c < clip.spec.channels; ++c)
        peak = std::max(peak, std::abs(samples[f * clip.spec.channels + c]));
    result[b] = std::min(1.0f, peak);
  }
  return result;
}

void little(std::vector<Uint8> &out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i)
    out.push_back(Uint8(value >> (i * 8)));
}

std::vector<Uint8> wavSelection(const AudioClip &clip, float begin, float end) {
  if (clip.spec.format != SDL_AUDIO_F32 || clip.spec.channels <= 0 ||
      clip.spec.freq <= 0)
    throw std::runtime_error("The take has no editable PCM audio");
  const std::size_t total =
      clip.data.size() / (sizeof(float) * clip.spec.channels);
  const std::size_t first =
      std::min(total, std::size_t(std::max(0.0f, begin) * clip.spec.freq));
  const std::size_t last =
      std::min(total, std::size_t(std::max(begin, end) * clip.spec.freq));
  if (last <= first)
    throw std::runtime_error("Select a non-empty region of the take");
  const std::size_t samples = (last - first) * clip.spec.channels;
  if (samples > std::numeric_limits<std::uint32_t>::max() / 2)
    throw std::runtime_error("Selected audio is too large");
  const std::uint32_t dataSize = std::uint32_t(samples * 2);
  std::vector<Uint8> out;
  out.reserve(44 + dataSize);
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  little(out, 36 + dataSize, 4);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  little(out, 16, 4);
  little(out, 1, 2);
  little(out, clip.spec.channels, 2);
  little(out, clip.spec.freq, 4);
  little(out, clip.spec.freq * clip.spec.channels * 2, 4);
  little(out, clip.spec.channels * 2, 2);
  little(out, 16, 2);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  little(out, dataSize, 4);
  const auto *input = reinterpret_cast<const float *>(clip.data.data()) +
                      first * clip.spec.channels;
  for (std::size_t i = 0; i < samples; ++i) {
    const auto value =
        std::int16_t(std::lrint(std::clamp(input[i], -1.0f, 1.0f) * 32767.0f));
    little(out, std::uint16_t(value), 2);
  }
  return out;
}
} // namespace

struct VoiceDubbingApplet::FileDialog {
  std::mutex mutex;
  bool done = false;
  std::string path, error;
  static void SDLCALL finish(void *userdata, const char *const *files, int) {
    std::unique_ptr<std::shared_ptr<FileDialog>> owner(
        static_cast<std::shared_ptr<FileDialog> *>(userdata));
    auto state = *owner;
    std::lock_guard lock(state->mutex);
    if (!files)
      state->error = SDL_GetError();
    else if (files[0])
      state->path = files[0];
    state->done = true;
  }
};

VoiceDubbingApplet::VoiceDubbingApplet(Engine &engine) : engine_(engine) {
  refreshDevices();
}
VoiceDubbingApplet::~VoiceDubbingApplet() {
  stopRecording();
  stopPreview();
}
void VoiceDubbingApplet::suspend() {
  stopRecording();
  stopPreview();
}

void VoiceDubbingApplet::refreshSceneAudio() {
  auto active = engine_.GetRuntime().debugNode();
  if (active.empty())
    active = engine_.activeNode();
  if (active == sceneNode_)
    return;
  stopRecording();
  stopPreview();
  sceneNode_ = active;
  selectedId_.clear();
  manualId_[0] = '\0';
  characterFilter_.clear();
  original_ = {};
  overlay_ = {};
  take_ = {};
  originalPeaks_.clear();
  overlayPeaks_.clear();
  takePeaks_.clear();
  selectionStart_ = selectionEnd_ = 0;
  sceneAudio_ = engine_.GetRuntime().debugSceneAudio();
  if (active.empty())
    return;
  for (const auto &node : engine_.debugNodes()) {
    const bool same =
        node.id == active || (active.size() > node.id.size() &&
                              active.compare(0, node.id.size(), node.id) == 0 &&
                              active[node.id.size()] == ':');
    if (!same)
      continue;
    for (const auto &asset : node.assets)
      if (audioName(asset) &&
          std::none_of(sceneAudio_.begin(), sceneAudio_.end(),
                       [&](const auto &item) { return item.id == asset; }))
        sceneAudio_.push_back({asset, {}, {}});
  }
}

void VoiceDubbingApplet::select(const std::string &id) {
  selectedId_ = id;
  SDL_strlcpy(manualId_, id.c_str(), sizeof(manualId_));
  if (auto runtimeOriginal = engine_.GetRuntime().debugOriginalAudio(id))
    original_ = asFloat(std::move(*runtimeOriginal));
  else
    original_ = asFloat(engine_.GetAssets().loadAudio(id, true));
  originalPeaks_ = peaks(original_);
  overlay_ = {};
  overlayPeaks_.clear();
  const bool hasOverlay = engine_.GetAssets().hasOverlay(id);
  const auto overlayPath = engine_.GetAssets().overlayPath(id);
  if (hasOverlay) {
    overlay_ = asFloat(engine_.GetAssets().loadAudio(id));
    overlayPeaks_ = peaks(overlay_);
    take_ = overlay_;
    takePeaks_ = overlayPeaks_;
  } else {
    take_ = {};
    takePeaks_.clear();
  }
  selectionStart_ = 0;
  if (!take_.data.empty())
    selectionEnd_ = lockOriginalDuration_ ? std::min(float(duration(take_)),
                                                     float(duration(original_)))
                                          : float(duration(take_));
  status_ =
      !hasOverlay ? "Ready to dub: " + id : "Loaded original + overlay: " + id;
  statusError_ = false;
}

void VoiceDubbingApplet::importFile(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  if (size > 1024ull * 1024 * 1024)
    throw std::runtime_error("Imported audio exceeds 1 GiB");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open imported audio");
  std::vector<Uint8> bytes(size);
  if (size && !input.read(reinterpret_cast<char *>(bytes.data()), size))
    throw std::runtime_error("Cannot read imported audio");
  take_ = asFloat(MediaDecoder::decodeAudio(bytes));
  takePeaks_ = peaks(take_);
  selectionStart_ = 0;
  selectionEnd_ =
      lockOriginalDuration_ && !original_.data.empty()
          ? std::min(float(duration(take_)), float(duration(original_)))
          : float(duration(take_));
  status_ = "Imported take: " + path.filename().string();
  statusError_ = false;
}

void VoiceDubbingApplet::pollDialog() {
  if (!dialog_)
    return;
  bool done;
  std::string path, error;
  {
    std::lock_guard lock(dialog_->mutex);
    done = dialog_->done;
    path = dialog_->path;
    error = dialog_->error;
  }
  if (!done)
    return;
  dialog_.reset();
  if (!error.empty())
    throw std::runtime_error(error);
  if (!path.empty())
    importFile(path);
}

void VoiceDubbingApplet::refreshDevices() {
  int count = 0;
  SDLMemoryPtr<SDL_AudioDeviceID> found(SDL_GetAudioRecordingDevices(&count));
  devices_.assign(found ? found.get() : nullptr,
                  found ? found.get() + count : nullptr);
  selectedDevice_ =
      std::clamp(selectedDevice_, 0, std::max(0, int(devices_.size()) - 1));
}

void VoiceDubbingApplet::startRecording() {
  stopPreview();
  stopRecording();
  take_ = {{SDL_AUDIO_F32, 1, 48000}, {}};
  takePeaks_.clear();
  SDL_AudioDeviceID device = devices_.empty()
                                 ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING
                                 : devices_[selectedDevice_];
  capture_.reset(
      SDL_OpenAudioDeviceStream(device, &take_.spec, nullptr, nullptr));
  if (!capture_ || !SDL_ResumeAudioStreamDevice(capture_.get())) {
    capture_.reset();
    throw std::runtime_error(SDL_GetError());
  }
  selectionStart_ = selectionEnd_ = 0;
  status_ = "Recording microphone...";
  statusError_ = false;
}

void VoiceDubbingApplet::pollRecording() {
  if (!capture_)
    return;
  int available = SDL_GetAudioStreamAvailable(capture_.get());
  while (available > 0) {
    const std::size_t old = take_.data.size();
    take_.data.resize(old + available);
    const int got = SDL_GetAudioStreamData(capture_.get(),
                                           take_.data.data() + old, available);
    if (got <= 0) {
      take_.data.resize(old);
      break;
    }
    take_.data.resize(old + got);
    available = SDL_GetAudioStreamAvailable(capture_.get());
  }
  takePeaks_ = peaks(take_);
  selectionEnd_ = float(duration(take_));
}

void VoiceDubbingApplet::stopRecording() {
  if (!capture_)
    return;
  SDL_PauseAudioStreamDevice(capture_.get());
  pollRecording();
  capture_.reset();
  if (lockOriginalDuration_ && !original_.data.empty())
    selectionEnd_ =
        std::min(float(duration(take_)), float(duration(original_)));
  status_ = "Recording stopped";
}

void VoiceDubbingApplet::stopPreview() {
  if (previewChannel_ && preview_)
    previewChannel_->detach(preview_.get());
  preview_.reset();
  previewChannel_.reset();
}

void VoiceDubbingApplet::play(const AudioClip &source, bool selection) {
  stopPreview();
  AudioClip clip = source;
  if (selection) {
    const auto frame = sizeof(float) * clip.spec.channels;
    const auto total = clip.data.size() / frame;
    const auto first =
        std::min(total, std::size_t(selectionStart_ * clip.spec.freq));
    const auto last =
        std::min(total, std::size_t(selectionEnd_ * clip.spec.freq));
    if (last <= first)
      throw std::runtime_error("Select a non-empty region first");
    clip.data = {source.data.begin() + first * frame,
                 source.data.begin() + last * frame};
  }
  preview_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &clip.spec, nullptr, nullptr));
  if (!preview_)
    throw std::runtime_error(SDL_GetError());
  previewChannel_ = engine_.GetAudio().channel("dubbing-preview");
  previewChannel_->attach(preview_.get());
  if (!SDL_PutAudioStreamData(preview_.get(), clip.data.data(),
                              static_cast<int>(clip.data.size())) ||
      !SDL_FlushAudioStream(preview_.get()) ||
      !SDL_ResumeAudioStreamDevice(preview_.get()))
    throw std::runtime_error(SDL_GetError());
}

void VoiceDubbingApplet::save() {
  const auto id = engine_.GetAssets().overlayWriteId(
      selectedId_.empty() ? manualId_ : selectedId_);
  if (id.empty())
    throw std::runtime_error("Choose a valid logical audio asset ID");
  if (take_.data.empty())
    throw std::runtime_error("Import or record a take first");
  const auto root = engine_.ensureAssetOverlay();
  const auto output = root / std::filesystem::path(id);
  std::filesystem::create_directories(output.parent_path());
  const auto bytes = wavSelection(take_, selectionStart_, selectionEnd_);
  auto temporary = output;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()))
      throw std::runtime_error("Cannot write overlay audio");
  }
  std::error_code error;
  std::filesystem::rename(temporary, output, error);
  if (error) {
    std::filesystem::remove(output, error);
    error.clear();
    std::filesystem::rename(temporary, output, error);
  }
  if (error)
    throw std::runtime_error("Cannot replace overlay audio: " +
                             error.message());
  // Re-index the same provider-owned overlay so regular game playback sees
  // the new file. The dubbing applet never creates a private overlay root.
  engine_.GetAssets().setOverlay(root);
  const std::string logical = selectedId_.empty() ? manualId_ : selectedId_;
  engine_.GetRuntime().debugOverlayAssetChanged(logical);
  overlay_ = asFloat(engine_.GetAssets().loadAudio(logical));
  overlayPeaks_ = peaks(overlay_);
  status_ = "Saved to the active engine overlay and ready in-game: " +
            output.string();
  statusError_ = false;
}

void VoiceDubbingApplet::discardOverlay() {
  const std::string logical = selectedId_.empty() ? manualId_ : selectedId_;
  if (logical.empty())
    throw std::runtime_error("Choose an overlay audio first");
  if (!engine_.GetAssets().discardOverlay(logical))
    throw std::runtime_error("This audio has no overlay to discard");
  engine_.GetRuntime().debugOverlayAssetChanged(logical);
  stopPreview();
  overlay_ = {};
  overlayPeaks_.clear();
  status_ = "Discarded overlay audio; game playback restored the original: " +
            logical;
  statusError_ = false;
}

void VoiceDubbingApplet::drawWaveform(const char *id,
                                      const std::vector<float> &values,
                                      float seconds, bool selection) {
  const ImVec2 size(ImGui::GetContentRegionAvail().x, 100);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, size);
  auto *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(at, {at.x + size.x, at.y + size.y},
                      IM_COL32(18, 20, 28, 255), 4);
  const float middle = at.y + size.y * 0.5f;
  if (!values.empty())
    for (std::size_t i = 0; i < values.size(); ++i) {
      const float x =
          at.x + size.x * float(i) /
                     float(std::max<std::size_t>(1, values.size() - 1));
      const float h = values[i] * (size.y * 0.46f);
      draw->AddLine({x, middle - h}, {x, middle + h},
                    IM_COL32(95, 205, 255, 255));
    }
  if (selection && seconds > 0) {
    const float x0 = at.x + size.x * selectionStart_ / seconds;
    const float x1 = at.x + size.x * selectionEnd_ / seconds;
    draw->AddRectFilled({at.x, at.y}, {x0, at.y + size.y},
                        IM_COL32(0, 0, 0, 145));
    draw->AddRectFilled({x1, at.y}, {at.x + size.x, at.y + size.y},
                        IM_COL32(0, 0, 0, 145));
    draw->AddLine({x0, at.y}, {x0, at.y + size.y}, IM_COL32(255, 190, 70, 255),
                  3);
    draw->AddLine({x1, at.y}, {x1, at.y + size.y}, IM_COL32(255, 190, 70, 255),
                  3);
  }
  ImGui::Text("%.3f s", seconds);
}

void VoiceDubbingApplet::draw() {
  try {
    pollDialog();
    pollRecording();
    refreshSceneAudio();
    ImGuiTheme::sectionHeader("Scene audio pairs");
    ImGui::TextWrapped("The left side always reads the untouched game/archive "
                       "audio, ignoring overlays. The right side is the live "
                       "overlay pair, or Empty when it still needs dubbing.");
    const auto recent = engine_.GetAssets().recentAudio();
    if (sceneAudio_.empty() && recent.empty())
      ImGui::TextDisabled("No audio has been requested in this session yet.");
    std::vector<DebugAudioAsset> rows = sceneAudio_;
    // Runtimes which enumerate a scene are authoritative: never leak audio
    // requested by the preceding scene into the booth.
    if (rows.empty())
      for (const auto &id : recent)
        if (std::none_of(rows.begin(), rows.end(),
                         [&](const auto &item) { return item.id == id; }))
          rows.push_back({id, {}, {}});
    std::sort(rows.begin(), rows.end(),
              [](const auto &left, const auto &right) {
                if (left.character != right.character)
                  return left.character < right.character;
                return left.id < right.id;
              });
    std::vector<std::string> characters;
    for (const auto &row : rows)
      if (!row.character.empty() &&
          std::find(characters.begin(), characters.end(), row.character) ==
              characters.end())
        characters.push_back(row.character);
    const char *filterLabel =
        characterFilter_.empty() ? "All characters" : characterFilter_.c_str();
    ImGui::SetNextItemWidth(240);
    ImGuiComboScope characterCombo{ImGui::BeginCombo("Character", filterLabel)};
    if (characterCombo.open) {
      if (ImGui::Selectable("All characters", characterFilter_.empty()))
        characterFilter_.clear();
      for (const auto &character : characters)
        if (ImGui::Selectable(character.c_str(), characterFilter_ == character))
          characterFilter_ = character;
    }
    characterCombo.close();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu audio pairs", rows.size());
    enum class RowAction {
      None,
      Open,
      PlayOriginal,
      PlayOverlay,
      DiscardOverlay
    };
    RowAction rowAction = RowAction::None;
    std::string rowActionId;
    ImGuiTableScope audioTable{ImGui::BeginTable(
        "##dubbing-pairs", 5,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)};
    if (audioTable.open) {
      ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Line / scene audio",
                              ImGuiTableColumnFlags_WidthStretch, 2.2f);
      ImGui::TableSetupColumn("Original", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Overlay", ImGuiTableColumnFlags_WidthStretch,
                              1.2f);
      ImGui::TableSetupColumn("Create / edit", ImGuiTableColumnFlags_WidthFixed,
                              110);
      ImGui::TableHeadersRow();
      for (const auto &row : rows) {
        if (!characterFilter_.empty() && row.character != characterFilter_)
          continue;
        const auto &id = row.id;
        ImGuiIdScope rowId(id.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (row.character.empty())
          ImGui::TextDisabled("Unknown");
        else
          ImGui::TextUnformatted(row.character.c_str());
        ImGui::TableSetColumnIndex(1);
        if (!row.text.empty()) {
          ImGui::TextWrapped("%s", row.text.c_str());
          ImGui::TextDisabled("%s", id.c_str());
        } else
          ImGui::TextWrapped("%s", id.c_str());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton("Play original")) {
          rowAction = RowAction::PlayOriginal;
          rowActionId = id;
        }
        ImGui::TableSetColumnIndex(3);
        const bool hasOverlay = engine_.GetAssets().hasOverlay(id);
        const auto overlay = engine_.GetAssets().overlayPath(id);
        if (!hasOverlay)
          ImGui::TextDisabled("Empty");
        else {
          if (ImGui::SmallButton("Play overlay")) {
            rowAction = RowAction::PlayOverlay;
            rowActionId = id;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", overlay.c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("Discard")) {
            rowAction = RowAction::DiscardOverlay;
            rowActionId = id;
          }
        }
        ImGui::TableSetColumnIndex(4);
        if (ImGui::Button(id == selectedId_ ? "Selected" : "Open")) {
          rowAction = RowAction::Open;
          rowActionId = id;
        }
      }
    }
    audioTable.close();
    // File/archive decoding may throw. Execute it only after EndTable(), so a
    // bad or unsupported voice can never corrupt ImGui's table stack.
    if (rowAction != RowAction::None) {
      select(rowActionId);
      if (rowAction == RowAction::PlayOriginal)
        play(original_, false);
      else if (rowAction == RowAction::PlayOverlay)
        play(overlay_, false);
      else if (rowAction == RowAction::DiscardOverlay)
        discardOverlay();
    }
    ImGui::InputText("Logical asset ID", manualId_, sizeof(manualId_));
    ImGui::SameLine();
    if (ImGui::Button("Load original"))
      select(manualId_);

    if (!original_.data.empty()) {
      ImGui::SeparatorText("Original timing");
      drawWaveform("##original-wave", originalPeaks_,
                   float(duration(original_)), false);
      if (ImGui::Button("Play original"))
        play(original_, false);
      if (!overlay_.data.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Play overlay"))
          play(overlay_, false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Stop preview"))
        stopPreview();
    }

    ImGui::SeparatorText("Import or record take");
    if (ImGui::Button("Import audio file...")) {
      if (!dialog_) {
        dialog_ = std::make_shared<FileDialog>();
        static const SDL_DialogFileFilter filters[] = {
            {"Audio", "wav;ogg;opus;mp3;flac;m4a;aac;aiff;wma"},
            {"All files", "*"}};
        SDL_ShowOpenFileDialog(
            FileDialog::finish, new std::shared_ptr<FileDialog>(dialog_),
            &engine_.GetWindow(), filters, 2, nullptr, false);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh microphones"))
      refreshDevices();
    const char *deviceName =
        devices_.empty() ? "Default recording device"
                         : SDL_GetAudioDeviceName(devices_[selectedDevice_]);
    ImGuiComboScope microphoneCombo{
        ImGui::BeginCombo("Microphone", deviceName ? deviceName : "Unknown")};
    if (microphoneCombo.open) {
      for (int i = 0; i < int(devices_.size()); ++i) {
        const char *name = SDL_GetAudioDeviceName(devices_[i]);
        if (ImGui::Selectable(name ? name : "Unknown", i == selectedDevice_))
          selectedDevice_ = i;
      }
    }
    microphoneCombo.close();
    if (!capture_) {
      if (ImGuiTheme::primaryButton("Record microphone"))
        startRecording();
    } else {
      ImGuiTheme::badge("RECORDING", ImGuiTheme::error());
      ImGui::SameLine();
      if (ImGui::Button("Stop recording"))
        stopRecording();
    }

    if (!take_.data.empty()) {
      const float takeDuration = float(duration(take_));
      ImGui::SeparatorText("Take timeline and in/out window");
      drawWaveform("##take-wave", takePeaks_, takeDuration, true);
      if (ImGui::Checkbox("Lock window to original duration",
                          &lockOriginalDuration_) &&
          lockOriginalDuration_ && !original_.data.empty())
        selectionEnd_ = std::min(takeDuration,
                                 selectionStart_ + float(duration(original_)));
      const float oldStart = selectionStart_;
      ImGui::SliderFloat("In", &selectionStart_, 0, takeDuration, "%.3f s");
      if (lockOriginalDuration_ && !original_.data.empty() &&
          selectionStart_ != oldStart) {
        const float window = float(duration(original_));
        selectionStart_ =
            std::min(selectionStart_, std::max(0.0f, takeDuration - window));
        selectionEnd_ = std::min(takeDuration, selectionStart_ + window);
      }
      if (!lockOriginalDuration_ || original_.data.empty())
        ImGui::SliderFloat("Out", &selectionEnd_, selectionStart_, takeDuration,
                           "%.3f s");
      ImGui::Text("Window: %.3f s%s", selectionEnd_ - selectionStart_,
                  !original_.data.empty() &&
                          std::abs((selectionEnd_ - selectionStart_) -
                                   duration(original_)) < 0.002
                      ? " (matches original)"
                      : "");
      if (ImGui::Button("Play selected window"))
        play(take_, true);
      ImGui::SameLine();
      if (ImGui::Button("Save to overlay"))
        save();
      if (engine_.GetAssets().hasOverlay(selectedId_.empty() ? manualId_
                                                             : selectedId_)) {
        ImGui::SameLine();
        if (ImGui::Button("Discard current overlay"))
          discardOverlay();
      }
    }
  } catch (const std::exception &error) {
    status_ = error.what();
    statusError_ = true;
  }
  if (!status_.empty())
    ImGuiTheme::statusText(status_.c_str(), statusError_);
}
} // namespace Kotonoha
