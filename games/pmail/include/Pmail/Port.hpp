#pragma once

#include <Pmail/Vm.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailPack;

// Loads script objects from PMAIL's script.dat (pack container) and runs the
// PMAIL bytecode VM. Native (sys) calls are forwarded to the host callback so
// the runtime can implement engine behavior incrementally.
class PmailGame final : public ::Pmail::Host {
public:
  explicit PmailGame(std::vector<uint8_t> scriptPack,
                     std::string packPath = {});
  ~PmailGame() override;

  bool init();
  void pump(size_t maxSteps = 100000);
  bool click();

  bool waitingForClick() const { return waiting_; }
  bool atTitle() const { return title_; }
  bool ended() const { return vm_.ended(); }
  ::Pmail::Vm &vm() { return vm_; }

  const std::string &currentScript() const { return scriptName_; }
  const std::string &message() const { return message_; }
  const std::string &background() const { return background_; }

  // Optional hooks for runtime integration.
  std::function<void(const std::string &)> trace;
  std::function<void(int id, const std::vector<::Pmail::Arg> &)> onNative;
  std::function<bool()> sePlaying;

  // Host
  void native(::Pmail::Vm &vm, int id,
              const std::vector<::Pmail::Arg> &args) override;
  ::Pmail::ObjectPtr loadByName(const std::string &name) override;
  ::Pmail::ObjectPtr loadById(uint32_t id12) override;
  void log(const std::string &s) override;

private:
  struct Entry {
    std::string name;
    size_t offset = 0;
    size_t size = 0;
  };

  void indexPack();
  bool loadScript(const std::string &name, bool asMain);
  void flushLines();

  std::vector<uint8_t> pack_;
  std::string packPath_;
  std::unique_ptr<PmailPack> packReader_;
  std::vector<Entry> entries_;
  std::map<std::string, ::Pmail::ObjectPtr> byName_;
  std::map<uint32_t, ::Pmail::ObjectPtr> byId_;
  ::Pmail::Vm vm_;
  ::Pmail::ObjectPtr sub_, lib_, main_;
  std::string scriptName_;
  std::string message_;
  std::deque<std::string> messageQueue_;
  std::string background_;
  bool waiting_ = false;
  bool title_ = false;
  bool waitingForSe_ = false;
  bool waitingForTime_ = false;
  uint64_t waitUntil_ = 0;
  uint32_t titleChoiceVar_ = 0;
};

} // namespace Kotonoha::PmailPort
