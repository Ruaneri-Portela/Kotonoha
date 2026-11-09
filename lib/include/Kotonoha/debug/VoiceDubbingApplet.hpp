#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
class AudioChannel;

// Shared live dubbing workstation used by every runtime through logical asset
// IDs. It deliberately writes standard PCM WAV data even when the logical ID
// has another extension: all Kotonoha providers probe the file contents.
class VoiceDubbingApplet {
  struct FileDialog;
  Engine &engine_;
  std::shared_ptr<FileDialog> dialog_;
  std::string selectedId_;
  std::string sceneNode_;
  std::vector<DebugAudioAsset> sceneAudio_;
  std::string characterFilter_;
  char manualId_[1024]{};
  AudioClip original_, overlay_, take_;
  std::vector<float> originalPeaks_, overlayPeaks_, takePeaks_;
  float selectionStart_ = 0, selectionEnd_ = 0;
  bool lockOriginalDuration_ = true;
  std::string status_;
  bool statusError_ = false;
  std::vector<SDL_AudioDeviceID> devices_;
  int selectedDevice_ = 0;
  AudioStreamPtr capture_;
  AudioStreamPtr preview_;
  std::shared_ptr<AudioChannel> previewChannel_;

  void select(const std::string &id);
  void refreshSceneAudio();
  void importFile(const std::filesystem::path &path);
  void pollDialog();
  void refreshDevices();
  void startRecording();
  void pollRecording();
  void stopRecording();
  void play(const AudioClip &clip, bool selection);
  void stopPreview();
  void save();
  void discardOverlay();
  void drawWaveform(const char *id, const std::vector<float> &peaks,
                    float duration, bool selection);

public:
  explicit VoiceDubbingApplet(Engine &engine);
  ~VoiceDubbingApplet();
  void draw();
  void suspend();
};
} // namespace Kotonoha
