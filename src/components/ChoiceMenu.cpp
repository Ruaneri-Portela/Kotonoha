#include <Kotonoha/components/ChoiceMenu.hpp>

#include <Kotonoha/utils/Time.h>

#include <limits>

namespace Kotonoha {

ChoiceMenu::ChoiceMenu() = default;

ChoiceMenu::~ChoiceMenu() { Close(); }

void ChoiceMenu::Open(std::vector<std::string> items, TTF_Font *font) {
  Close();
  items_ = std::move(items);
  result_ = -2;
  time_ = KotonohaTimeNew(false);
  prompt_ = std::make_unique<Prompt>(items_, &result_, 0,
                                     std::numeric_limits<Uint64>::max(), time_,
                                     font, "");
  prompt_->SetVerticalList(true);
}

void ChoiceMenu::Close() {
  prompt_.reset();
  if (time_ != nullptr) {
    KotonohaTimeDestroy(time_);
    time_ = nullptr;
  }
  items_.clear();
  result_ = -2;
}

bool ChoiceMenu::Select(int index) {
  return prompt_ != nullptr && prompt_->SelectChoice(index, nullptr);
}

void ChoiceMenu::HandleEvent(SDL_Event *event, Kotonoha_Game *game) {
  if (prompt_ != nullptr) prompt_->HandleEvent(event, game, nullptr);
}

void ChoiceMenu::Draw(SDL_Window *window, SDL_Renderer *renderer) {
  if (prompt_ == nullptr) return;
  int logW = 0, logH = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(renderer, &logW, &logH, &mode);
  SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  Prompt::Render(window, renderer, prompt_.get(), nullptr);
  if (mode != SDL_LOGICAL_PRESENTATION_DISABLED)
    SDL_SetRenderLogicalPresentation(renderer, logW, logH, mode);
}

void GameMenu::Show(Page page, TTF_Font *font) {
  font_ = font;
  parent_ = Page::None;
  OpenPage(page);
}

void GameMenu::Hide() {
  menu_.Close();
  page_ = Page::None;
}

void GameMenu::OpenPage(Page page) {
  page_ = page;
  actions_.clear();
  std::vector<std::string> items;
  std::string title;
  switch (page) {
  case Page::Title:
    title = gameTitle_.empty() ? "Kotonoha" : gameTitle_;
    items = {"New Game"};
    actions_ = {Action::NewGame};
    if (store_.AnySave()) {
      items.push_back("Continue");
      actions_.push_back(Action::Load);
    }
    items.push_back("Quit");
    actions_.push_back(Action::Quit);
    break;
  case Page::Pause:
    title = "Menu";
    items = {"Resume", "Save", "Load", "Title"};
    break;
  case Page::SaveSlots:
  case Page::LoadSlots:
    title = page == Page::SaveSlots ? "Save" : "Load";
    for (int i = 0; i < SaveStore::kSlots; ++i) {
      const std::string label = store_.Label(i);
      items.push_back("Slot " + std::to_string(i + 1) + "  " +
                      (label.empty() ? std::string("-- empty --") : label));
    }
    items.push_back("Back");
    break;
  case Page::None:
    Hide();
    return;
  }
  title_.SetFont(font_);
  TextBox::Style style = title_.GetStyle();
  style.fontSize = 28.0f;
  title_.SetStyle(style);
  title_.SetCaption(title);
  menu_.Open(std::move(items), font_);
}

void GameMenu::HandleEvent(SDL_Event *event, Kotonoha_Game *game) {
  if (!Active()) return;
  // Escape / right click on a slot page is the same as its "Back" item.
  const bool back = (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) ||
                    (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_RIGHT);
  if (back && (page_ == Page::SaveSlots || page_ == Page::LoadSlots)) {
    OpenPage(parent_ == Page::None ? Page::Title : parent_);
    return;
  }
  menu_.HandleEvent(event, game);
}

GameMenu::Event GameMenu::Poll() {
  Event out;
  if (!Active()) return out;
  const int pick = menu_.Result();
  if (pick < 0) return out;

  switch (page_) {
  case Page::Title:
    if (static_cast<size_t>(pick) < actions_.size()) {
      const Action a = actions_[static_cast<size_t>(pick)];
      if (a == Action::Load) {
        parent_ = Page::Title;
        OpenPage(Page::LoadSlots);
      } else {
        Hide();
        out.action = a;
      }
    }
    break;
  case Page::Pause:
    if (pick == 0) {
      Hide();
      out.action = Action::Resume;
    } else if (pick == 1 || pick == 2) {
      parent_ = Page::Pause;
      OpenPage(pick == 1 ? Page::SaveSlots : Page::LoadSlots);
    } else {
      Hide();
      out.action = Action::Title;
    }
    break;
  case Page::SaveSlots:
  case Page::LoadSlots: {
    const bool save = page_ == Page::SaveSlots;
    if (pick >= SaveStore::kSlots) {
      OpenPage(parent_ == Page::None ? Page::Title : parent_);
    } else if (save || store_.Exists(pick)) {
      Hide();
      out.action = save ? Action::Save : Action::Load;
      out.slot = pick;
    } else {
      OpenPage(page_); // empty slot: stay on the page
    }
    break;
  }
  case Page::None: break;
  }
  return out;
}

void GameMenu::Draw(SDL_Window *window, SDL_Renderer *renderer, float logicalW,
                    float logicalH, const std::string &) {
  if (!Active()) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
  SDL_FRect all{0, 0, logicalW, logicalH};
  SDL_RenderFillRect(renderer, &all);
  title_.Draw(renderer);
  menu_.Draw(window, renderer);
}

} // namespace Kotonoha
