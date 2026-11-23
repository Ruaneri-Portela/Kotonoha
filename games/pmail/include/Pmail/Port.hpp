#pragma once

#include <Pmail/Animation.hpp>
#include <Pmail/Vm.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailPack;

class PmailGame final : public ::Pmail::Host {
public:
  struct Sprite {
    std::string name;
    int x = 0, y = 0;
    int id = 0;
    int tint[3] = {0x100, 0x100, 0x100};
  };

  // Computer-screen text and chat history exposed by the native PMAIL
  // commands. Coordinates are in the original 800x600 framebuffer.
  struct ComputerText {
    std::string text;
    int x = 0, y = 0, width = 0, height = 0, size = 0, color = 0;
  };
  struct ChatLine {
    int mode = 0;
    std::string text;
  };
  // Two native computer screen families: SYS 176..179 appends to the
  // chat-family records at 0xa6d6a0 (0x417d80), while SYS 192..195 uses the
  // mail-family records at 0x93d6e0 (0x411320). Each native record is
  // {uint16 objId, uint16 stringIndex, uint8 mode}; mode selects the text
  // style. SYS 179 copies seven integers into 0xf8a77a..0xf8a792 and calls
  // 0x417a50: (x, y, width, height, rightMargin, indent, drawHeader).
  struct ComputerPanel {
    bool active = false;
    std::vector<ChatLine> entries;
    std::array<int32_t, 7> params{};
    int32_t selection = 0;
  };
  struct ComputerIcon {
    bool active = false;
    int32_t x = 0;
    int32_t y = 0;
  };
  static constexpr size_t ComputerPanelLimit = 60;
  static constexpr size_t ComputerIconLimit = 25;
  static constexpr size_t ComputerTextLimit = 10;
  static constexpr size_t ChatHistoryLimit = 15;

  std::vector<ComputerText> computerText() const {
    std::lock_guard lock(mutex_);
    return computerText_;
  }
  std::vector<ChatLine> chatHistory() const {
    std::lock_guard lock(mutex_);
    return chatHistory_;
  }
  std::string computerClock() const {
    std::lock_guard lock(mutex_);
    return computerClock_;
  }
  ComputerPanel chatPanel() const {
    std::lock_guard lock(mutex_);
    return chatPanel_;
  }
  ComputerPanel mailPanel() const {
    std::lock_guard lock(mutex_);
    return mailPanel_;
  }
  std::array<ComputerIcon, ComputerIconLimit> computerIcons() const {
    std::lock_guard lock(mutex_);
    return computerIcons_;
  }

  // The original name-entry dialog uses tokens $NO and $HO in its scripts.
  // Names are chosen before New Game and are included in port save states.
  void setPlayerNames(std::string no, std::string ho);
  std::string playerNameNO() const {
    std::lock_guard lock(mutex_);
    return playerNameNO_;
  }
  std::string playerNameHO() const {
    std::lock_guard lock(mutex_);
    return playerNameHO_;
  }
  std::string expandPlayerNames(const std::string &text) const;

  bool hasComputerOverlay() const {
    std::lock_guard lock(mutex_);
    bool hasIcons = false;
    for (const auto &icon : computerIcons_)
      hasIcons = hasIcons || icon.active;
    return computerOverlayVisible_ &&
           (!computerText_.empty() || !chatHistory_.empty() ||
            !computerClock_.empty() || chatPanel_.active ||
            mailPanel_.active || hasIcons);
  }

  explicit PmailGame(std::vector<uint8_t> scriptPack,
                     std::string packPath = {});
  ~PmailGame() override;

  bool init();
  // Read-only PAK index for the shared Kotonoha Debug UI. Returns logical
  // script names without decoding every VM object.
  std::map<int, std::vector<std::string>> translationInventory();
  std::vector<std::string> debugScripts() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto &entry : entries_)
      if (!entry.name.empty())
        names.push_back(entry.name);
    return names;
  }
  std::string scriptPackId() const { return packPath_; }
  // Debug-only jump to a native Pmail script entry (not to library routines).
  bool debugJumpScript(const std::string &name);
  std::string serialize() const;
  void deserialize(const std::string &data);
  void returnToTitle();
  void pump(size_t maxSteps = 100000);
  void advanceAnimation(uint32_t elapsedMs);
  std::vector<AnimationState> animations() const {
    std::lock_guard lock(mutex_);
    return animations_;
  }
  bool animating() const {
    std::lock_guard lock(mutex_);
    return !animations_.empty() && animations_.back().playing;
  }
  bool click();
  bool choose(size_t i);

  bool waitingForClick() const {
    std::lock_guard lock(mutex_);
    return waiting_;
  }
  bool choosing() const {
    std::lock_guard lock(mutex_);
    return choosing_;
  }
  bool atTitle() const {
    std::lock_guard lock(mutex_);
    return title_;
  }

  // Hardcoded title menu (pmail.exe:0x42c950, entered from sys 213). The exe
  // draws TITL00<variant>.CRGB plus three 423x55 buttons at x=189 and
  // y=326/388/450, highlights the hovered one with TITL0n<variant>.IMG, and
  // writes the chosen button index (1..3) into the variable passed to sys 213.
  int titleVariant() const {
    std::lock_guard lock(mutex_);
    return titleVariant_;
  }
  int titleCount() const {
    std::lock_guard lock(mutex_);
    return titleCount_;
  }
  int titleSelection() const {
    std::lock_guard lock(mutex_);
    return titleIndex_;
  }
  void titleHover(int index);
  void titleMove(int delta);
  bool titleChoose(int index);
  // Native extras menu (pmail.exe:0x41e9f0, entered from sys 212). The
  // original keeps 0 as "no item hovered" and returns 1..4 to the script.
  bool extrasMenuActive() const {
    std::lock_guard lock(mutex_);
    return extrasMenu_;
  }
  // Native omake viewers: sys 209 (CG MODE, 0x405390), sys 210 (KAIROKU
  // MODE, 0x42a4c0) and sys 208 (MUSIC MODE, 0x41d810). Each keeps its own
  // modal state and blocks the VM until the player leaves it.
  enum class ExtrasViewerMode { None = 0, Music = 208, CgGrid = 209, Recollection = 210 };
  bool extrasViewerActive() const {
    std::lock_guard lock(mutex_);
    return extrasViewerMode_ != ExtrasViewerMode::None;
  }
  ExtrasViewerMode extrasViewerMode() const {
    std::lock_guard lock(mutex_);
    return extrasViewerMode_;
  }
  int extrasViewerCategory() const {
    std::lock_guard lock(mutex_);
    return extrasViewerCategory_;
  }
  int extrasViewerCategoryCount() const { return 8; }
  int extrasViewerPage() const {
    std::lock_guard lock(mutex_);
    return extrasViewerPage_;
  }
  int extrasViewerPageCount() const;
  int extrasViewerSlot() const {
    std::lock_guard lock(mutex_);
    return extrasViewerSlot_;
  }
  // 0 means the cell has no selectable content (locked or empty).
  bool extrasViewerSlotLocked(int slot) const;
  bool extrasViewerFullView() const {
    std::lock_guard lock(mutex_);
    return extrasViewerFullView_;
  }
  // Grid cells for the current page: one entry per row in reading order.
  // CG viewers return the CG resource stem, the music viewer the track id.
  std::vector<std::string> extrasViewerCells() const;
  int extrasViewerTrack() const {
    std::lock_guard lock(mutex_);
    return extrasViewerTrack_;
  }
  int extrasViewerTrackCount() const;
  int extrasViewerTrackFirst() const {
    std::lock_guard lock(mutex_);
    return extrasViewerTrackFirst_;
  }
  void extrasViewerHover(int slot);
  void extrasViewerMove(int dx, int dy);
  void extrasViewerPageBy(int delta);
  void extrasViewerCategoryBy(int delta);
  void extrasViewerSetCategory(int category);
  bool extrasViewerActivate();
  void extrasViewerBack();
  void extrasViewerSetTrack(int track);
  void extrasViewerTrackBy(int delta);
  void extrasViewerStopMusic();
  void extrasViewerToggleRepeat();
  bool extrasViewerRepeat() const {
    std::lock_guard lock(mutex_);
    return extrasViewerRepeat_;
  }
  int extrasSelection() const {
    std::lock_guard lock(mutex_);
    return extrasIndex_;
  }
  void extrasHover(int index);
  void extrasMove(int delta);
  bool extrasChoose(int index);
  bool ended() const {
    std::lock_guard lock(mutex_);
    return vm_.ended();
  }

  std::vector<int> unhandledNativeIds() const {
    std::lock_guard lock(mutex_);
    return {unhandledNativeIds_.begin(), unhandledNativeIds_.end()};
  }

  // Evidence for each distinct unknown native call site. String arguments
  // are represented by VM reference IDs, not by retail text or player input.
  struct NativeGap {
    int id = 0;
    std::string script;
    uint32_t pc = 0;
    std::string signature;
    std::size_t hits = 0;
  };
  static constexpr std::size_t NativeGapLimit = 512;
  std::vector<NativeGap> unhandledNativeSites() const {
    std::lock_guard lock(mutex_);
    std::vector<NativeGap> out;
    out.reserve(unhandledNativeSites_.size());
    for (const auto &entry : unhandledNativeSites_)
      out.push_back(entry.second);
    return out;
  }
  ::Pmail::Vm &vm() { return vm_; }

  std::string currentScript() const {
    std::lock_guard lock(mutex_);
    return scriptName_;
  }
  std::string message() const {
    std::lock_guard lock(mutex_);
    return message_;
  }
  std::string speaker() const {
    std::lock_guard lock(mutex_);
    return speakerName_;
  }
  std::string background() const {
    std::lock_guard lock(mutex_);
    return background_;
  }
  std::vector<Sprite> sprites() const {
    std::lock_guard lock(mutex_);
    return sprites_;
  }
  uint32_t sceneVersion() const {
    std::lock_guard lock(mutex_);
    return sceneVersion_;
  }
  bool filled() const {
    std::lock_guard lock(mutex_);
    return filled_;
  }
  uint32_t fillColor() const {
    std::lock_guard lock(mutex_);
    return fillColor_;
  }
  std::vector<std::string> choices() const {
    std::lock_guard lock(mutex_);
    return choiceText_;
  }
  // Native message-window state. pmail.exe keeps the current and next text
  // as {objectId, stringIndex} pairs at 0xf87d6e/0x76; SYS 18 selects the
  // current pair, SYS 25 the next one. SYS 19/20 create and release the text
  // surfaces, and SYS 216 stores the text mode at 0xa8add8.
  int textMode() const {
    std::lock_guard lock(mutex_);
    return textMode_;
  }
  bool textSurfacesReady() const {
    std::lock_guard lock(mutex_);
    return textSurfacesReady_;
  }
  std::pair<int, int> currentTextRef() const {
    std::lock_guard lock(mutex_);
    return {currentTextObject_, currentTextIndex_};
  }
  std::pair<int, int> nextTextRef() const {
    std::lock_guard lock(mutex_);
    return {nextTextObject_, nextTextIndex_};
  }
  bool nativeTimeWaitActive() const {
    std::lock_guard lock(mutex_);
    return audioWait_ == AudioWait::Time;
  }
  // SYS 53 selects a retail movie stem (pm01, ...) and blocks the VM until
  // playback ends, matching pmail.exe:0x4011b0.
  std::string lastRequestedMovie() const {
    std::lock_guard lock(mutex_);
    return lastRequestedMovie_;
  }
  bool movieWaitActive() const {
    std::lock_guard lock(mutex_);
    return movieWait_;
  }
  // Called by the runtime when the selected movie ends or is skipped.
  void resumeFromMovie();
  // SYS 225/226 forward the original WM_COMMAND 0xbba (load) / 0xbbb (save)
  // to the host save UI. The VM stays blocked while that dialog is open and
  // resumes when the host reports it closed.
  bool saveMenuWaitActive() const {
    std::lock_guard lock(mutex_);
    return saveMenuWait_;
  }
  bool saveMenuWaitLoad() const {
    std::lock_guard lock(mutex_);
    return saveMenuLoad_;
  }
  void resumeFromSaveMenu();
  // SYS 215 marks one of the 32 retail extra/gallery entries as unlocked.
  bool extraUnlocked(const std::string &name) const {
    std::lock_guard lock(mutex_);
    return unlockedExtras_.count(name) != 0;
  }
  std::vector<std::string> unlockedExtras() const;
  // Original global flag block 0x7933c0 is persisted to SAVE\SAVEFL99.SAV
  // (pmail.exe:0x40eea0 load / 0x40eef0 save). The version lets the host
  // mirror that file whenever a new entry is unlocked.
  uint32_t extrasVersion() const {
    std::lock_guard lock(mutex_);
    return extrasVersion_;
  }
  void loadUnlockedExtras(const std::vector<std::string> &names);

  struct TransitionInfo {
    int type = 0;
    int param = 0;
    int speed = 0;
    uint32_t version = 0;
  };
  struct StaffRollState {
    bool active = false;
    int category = 3;
    int bgmTrack = 23;
    uint32_t elapsedMs = 0;
    uint32_t durationMs = 48000;
    std::vector<std::string> vignettes;
  };
  TransitionInfo lastTransition() const {
    std::lock_guard lock(mutex_);
    return lastTransition_;
  }
  StaffRollState staffRoll() const {
    std::lock_guard lock(mutex_);
    return staffRoll_;
  }
  bool staffRollActive() const {
    std::lock_guard lock(mutex_);
    return staffRoll_.active;
  }
  void setInteractiveStaffRoll(bool enabled) {
    std::lock_guard lock(mutex_);
    interactiveStaffRoll_ = enabled;
  }
  bool skipStaffRoll();
  std::vector<std::string> endingVignettes(int category) const {
    std::lock_guard lock(mutex_);
    const auto it = endingVignettes_.find(category);
    return it == endingVignettes_.end() ? std::vector<std::string>{}
                                        : it->second;
  }

  // Optional hooks for runtime integration.
  std::function<void(const std::string &)> trace;
  std::function<void(int id, const std::vector<::Pmail::Arg> &)> onNative;
  std::function<void(int type, int param, int speed)> onTransition;
  std::function<void(const std::string &)> onChoiceSelected;
  std::function<bool()> sePlaying;
  std::function<bool()> voicePlaying;
  std::function<int()> voiceMouthFrame;
  int voiceActor() const {
    std::lock_guard lock(mutex_);
    return voiceActor_;
  }
  std::function<size_t(const std::string &)> animationFrameCount;
  // Returns true when a movie actually started; false lets the VM continue.
  std::function<bool(const std::string &)> playMovie;
  // Returns true when the host opened its save (false) or load (true) dialog.
  std::function<bool(bool)> openSaveMenu;
  // MUSIC MODE (sys 208) consults the host for the external track list and
  // routes playback through the same BGM slot as SYS 112.
  std::function<std::vector<std::string>()> musicTracks;
  std::function<void(const std::string &)> playMusic;
  std::function<void()> stopMusic;

  // Host
  void native(::Pmail::Vm &vm, int id,
              const std::vector<::Pmail::Arg> &args) override;
  ::Pmail::ObjectPtr loadByName(const std::string &name) override;
  ::Pmail::ObjectPtr loadById(uint32_t id12) override;
  void log(const std::string &s) override;

private:
  mutable std::recursive_mutex mutex_;
  struct Entry {
    std::string name;
    size_t offset = 0;
    size_t size = 0;
  };
  struct CallFrame {
    std::string name;
    uint32_t pc = 0;
  };
  enum class AudioWait { None, Voice, VoiceSkippable, SeSkippable, Time };

  void indexPack();
  bool loadScript(const std::string &name, bool asMain);
  void flushLines();
  bool popCall();
  void showNext();

  std::vector<uint8_t> pack_;
  std::string packPath_;
  // Read-only cache; script pack does not change during a game session.
  // Avoid unpacking all scripts again when F7 follows a new native scene.
  std::map<int, std::vector<std::string>> translationCatalog_;
  bool translationCatalogReady_ = false;
  std::unique_ptr<PmailPack> packReader_;
  std::vector<Entry> entries_;
  std::map<std::string, ::Pmail::ObjectPtr> byName_;
  std::map<uint32_t, ::Pmail::ObjectPtr> byId_;
  std::vector<CallFrame> callStack_;
  ::Pmail::Vm vm_;
  ::Pmail::ObjectPtr sub_, lib_, main_;
  std::string scriptName_;
  std::string message_;
  std::string speakerName_;
  std::deque<std::string> messageQueue_;
  std::string background_;
  std::vector<Sprite> sprites_;
  std::vector<ComputerText> computerText_;
  std::vector<ChatLine> chatHistory_;
  std::string computerClock_;
  ComputerPanel chatPanel_, mailPanel_;
  std::array<ComputerIcon, ComputerIconLimit> computerIcons_{};
  bool computerOverlayVisible_ = false;
  std::string playerNameNO_ = "NO", playerNameHO_ = "HO";
  void clearComputerOverlay();
  std::vector<AnimationState> animations_;
  int voiceActor_ = -1;
  uint32_t sceneVersion_ = 0;
  bool filled_ = false;
  uint32_t fillColor_ = 0;
  bool returningToTitle_ = false;
  bool waiting_ = false;
  bool title_ = false;
  bool choosing_ = false;
  int textMode_ = 0;
  bool textSurfacesReady_ = false;
  int currentTextObject_ = -1;
  int currentTextIndex_ = -1;
  int nextTextObject_ = -1;
  int nextTextIndex_ = -1;
  std::string lastRequestedMovie_;
  bool movieWait_ = false;
  bool saveMenuWait_ = false;
  bool saveMenuLoad_ = false;
  std::set<std::string> unlockedExtras_;
  uint32_t extrasVersion_ = 0;
  AudioWait audioWait_ = AudioWait::None;
  uint64_t waitUntil_ = 0;
  uint32_t titleChoiceVar_ = 0;
  int titleVariant_ = 0;
  int titleCount_ = 3;
  int titleIndex_ = 1;
  int titleHover_ = 0;
  bool extrasMenu_ = false;
  int extrasIndex_ = 0;
  uint32_t extrasMenuVar_ = 0;
  ExtrasViewerMode extrasViewerMode_ = ExtrasViewerMode::None;
  int extrasViewerCategory_ = 0;
  int extrasViewerPage_ = 0;
  int extrasViewerSlot_ = 0;
  bool extrasViewerFullView_ = false;
  int extrasViewerExtraIndex_ = -1;
  uint32_t extrasViewerVar_ = 0;
  std::vector<std::string> musicTrackIds_;
  int extrasViewerTrack_ = 0;
  int extrasViewerTrackFirst_ = 0;
  bool extrasViewerRepeat_ = false;
  void closeExtrasViewer(int result);
  std::vector<int> extrasViewerCatalog() const;
  void clampExtrasViewerSelection();
  uint32_t choiceVar_ = 0;
  std::vector<std::string> choiceText_;
  std::vector<int> choiceValue_;
  std::map<int, std::string> spriteSlots_;
  std::map<int, std::vector<std::string>> endingVignettes_;
  TransitionInfo lastTransition_{};
  uint32_t transitionVersion_ = 0;
  StaffRollState staffRoll_{};
  bool interactiveStaffRoll_ = false;
  // Diagnostic only: unknown original native commands seen this run.
  std::set<int> unhandledNativeIds_;
  std::map<std::tuple<int, std::string, uint32_t>, NativeGap>
      unhandledNativeSites_;
};

} // namespace Kotonoha::PmailPort
