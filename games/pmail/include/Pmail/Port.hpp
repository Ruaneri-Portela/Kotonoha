#pragma once

#include <Pmail/Vm.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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

  explicit PmailGame(std::vector<uint8_t> scriptPack,
                     std::string packPath = {});
  ~PmailGame() override;

  bool init();
  std::string serialize() const;
  void deserialize(const std::string &data);
  void returnToTitle();
  void pump(size_t maxSteps = 100000);
  bool click();
  bool choose(size_t i);

  bool waitingForClick() const { std::lock_guard lock(mutex_); return waiting_; }
  bool choosing() const { std::lock_guard lock(mutex_); return choosing_; }
  bool atTitle() const { std::lock_guard lock(mutex_); return title_; }

  // Hardcoded title menu (pmail.exe:0x42c950, entered from sys 213). The exe
  // draws TITL00<variant>.CRGB plus three 423x55 buttons at x=189 and
  // y=326/388/450, highlights the hovered one with TITL0n<variant>.IMG, and
  // writes the chosen button index (1..3) into the variable passed to sys 213.
  int titleVariant() const { std::lock_guard lock(mutex_); return titleVariant_; }
  int titleCount() const { std::lock_guard lock(mutex_); return titleCount_; }
  int titleSelection() const { std::lock_guard lock(mutex_); return titleIndex_; }
  void titleHover(int index);
  void titleMove(int delta);
  bool titleChoose(int index);
  bool ended() const { std::lock_guard lock(mutex_); return vm_.ended(); }
  ::Pmail::Vm &vm() { return vm_; }

  std::string currentScript() const { std::lock_guard lock(mutex_); return scriptName_; }
  std::string message() const { std::lock_guard lock(mutex_); return message_; }
  std::string speaker() const { std::lock_guard lock(mutex_); return speakerName_; }
  std::string background() const { std::lock_guard lock(mutex_); return background_; }
  std::vector<Sprite> sprites() const { std::lock_guard lock(mutex_); return sprites_; }
  uint32_t sceneVersion() const { std::lock_guard lock(mutex_); return sceneVersion_; }
  bool filled() const { std::lock_guard lock(mutex_); return filled_; }
  uint32_t fillColor() const { std::lock_guard lock(mutex_); return fillColor_; }
  std::vector<std::string> choices() const { std::lock_guard lock(mutex_); return choiceText_; }

  // Optional hooks for runtime integration.
  std::function<void(const std::string &)> trace;
  std::function<void(int id, const std::vector<::Pmail::Arg> &)> onNative;
  std::function<bool()> sePlaying;
  std::function<bool()> voicePlaying;

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
  uint32_t sceneVersion_ = 0;
  bool filled_ = false;
  uint32_t fillColor_ = 0;
  bool returningToTitle_ = false;
  bool waiting_ = false;
  bool title_ = false;
  bool choosing_ = false;
  AudioWait audioWait_ = AudioWait::None;
  uint64_t waitUntil_ = 0;
  uint32_t titleChoiceVar_ = 0;
  int titleVariant_ = 0;
  int titleCount_ = 3;
  int titleIndex_ = 1;
  int titleHover_ = 0;
  uint32_t choiceVar_ = 0;
  std::vector<std::string> choiceText_;
  std::vector<int> choiceValue_;
  std::map<int, std::string> spriteSlots_;
};

} // namespace Kotonoha::PmailPort
