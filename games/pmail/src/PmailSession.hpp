#pragma once

#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Pmail/Vm.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailGame;
class PmailComponents;
class PmailTitleMenu;

/**
 * PmailSession manages the high-level state of the PMAIL game instance.
 * It holds the PmailGame (the VM host) and manages the transition between
 * different game states (Title, Gameplay, etc.) using the Kotonoha Session
 * model.
 */
class PmailSession {
public:
  explicit PmailSession(
      std::shared_ptr<PmailGame> game, Kotonoha::Engine &engine,
      std::shared_ptr<Kotonoha::Providers::SnowAssets> assets);
  ~PmailSession();

  // Session overrides
  void update();
  void render(Kotonoha::RendererRef renderer);
  void onEvent(Kotonoha::EventRef event);

  // Session management
  void init();
  void shutdown();
  void setMenuActions(std::function<void()> save, std::function<void()> load,
                      std::function<void()> title);
  void closeMenus(bool clearBacklog = false);
  bool paused() const;
  void resetClock() { lastUpdateMs_ = SDL_GetTicks(); }
  std::vector<Kotonoha::DialogueEntry> backlogEntries() const;
  void restoreBacklog(const std::vector<Kotonoha::DialogueEntry> &entries);

  // Getters
  std::shared_ptr<PmailGame> game() const { return game_; }
  Kotonoha::Providers::SnowAssets *assets() const { return assets_.get(); }
  bool isAtTitle() const;

  // Native handler
  void handleNative(int id, const std::vector<::Pmail::Arg> &args);

private:
  void updateTitle();
  void updateGameplay();
  void renderTitle(Kotonoha::RendererRef renderer);
  void renderGameplay(Kotonoha::RendererRef renderer);

  bool HandleTitleEvent(Kotonoha::EventRef event, SDL_FPoint stagePoint,
                        bool haveStagePoint);

  std::shared_ptr<PmailGame> game_;
  Kotonoha::Engine &engine_;
  std::shared_ptr<Kotonoha::Providers::SnowAssets> assets_;
  std::unique_ptr<PmailComponents> components_;

  std::unique_ptr<PmailTitleMenu> titleMenu_;

  // Status
  uint64_t lastUpdateMs_ = 0;
  std::size_t choiceSelection_ = 0; // Keyboard/gamepad choice focus.
  bool initialized_ = false;
  std::function<void()> loadAction_;
  void chooseTitle(int index);
};

} // namespace Kotonoha::PmailPort
