#pragma once
#include <memory>
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/SaveStore.hpp>
#include <Kotonoha/components/TextBox.hpp>

namespace Kotonoha {

// A pending list of choices rendered and answered through Kotonoha::Prompt.
// Owns the prompt clock and result slot, so a game only opens it with the
// entries and polls Result() (-2 pending, otherwise the picked index).
class ChoiceMenu {
public:
  ChoiceMenu();
  ~ChoiceMenu();
  ChoiceMenu(const ChoiceMenu &) = delete;
  ChoiceMenu &operator=(const ChoiceMenu &) = delete;

  void Open(std::vector<std::string> items, TTF_Font *font);
  void Close();
  bool Active() const { return prompt_ != nullptr; }
  int Result() const { return result_; }
  const std::vector<std::string> &Items() const { return items_; }
  bool Select(int index);
  void HandleEvent(SDL_Event *event, Kotonoha_Game *game);
  // Draws over the whole window, outside the logical presentation.
  void Draw(SDL_Window *window, SDL_Renderer *renderer);

private:
  Kotonoha_Time *time_ = nullptr;
  std::unique_ptr<Prompt> prompt_;
  std::vector<std::string> items_;
  int result_ = -2;
};

// Title / pause / save / load screens built from ChoiceMenu pages.
class GameMenu {
public:
  enum class Page { None, Title, Pause, SaveSlots, LoadSlots };
  enum class Action { None, NewGame, Resume, Save, Load, Title, Quit };
  struct Event {
    Action action = Action::None;
    int slot = -1;
  };

  explicit GameMenu(std::string gameId, int slots = SaveStore::kSlots) : store_(std::move(gameId), slots) {}

  void SetGameTitle(std::string title) { gameTitle_ = std::move(title); }
  SaveStore &Store() { return store_; }
  bool Active() const { return page_ != Page::None; }
  Page CurrentPage() const { return page_; }
  void Show(Page page, TTF_Font *font);
  void Hide();
  void HandleEvent(SDL_Event *event, Kotonoha_Game *game);
  // Resolves a pick into an action (or moves to a sub-page).
  Event Poll();
  void Draw(SDL_Window *window, SDL_Renderer *renderer, float logicalW,
            float logicalH, const std::string &title);
  ChoiceMenu &Choices() { return menu_; }

private:
  void OpenPage(Page page);

  SaveStore store_;
  ChoiceMenu menu_;
  Page page_ = Page::None;
  Page parent_ = Page::None;
  std::string gameTitle_;
  TextBox title_;
  TTF_Font *font_ = nullptr;
  std::vector<Action> actions_; // entry index -> action for Title/Pause
};

} // namespace Kotonoha
