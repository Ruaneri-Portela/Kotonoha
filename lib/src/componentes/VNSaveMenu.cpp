#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>
#include <stdexcept>

namespace Kotonoha {
namespace {
constexpr std::size_t pageSize = 7;
}

VNSaveMenu::VNSaveMenu(Engine &engine) : Scene(engine) {}
VNSaveMenu::~VNSaveMenu() = default;

std::size_t VNSaveMenu::count() const {
  return slots.size() + (loading ? 0 : 1);
}

void VNSaveMenu::open(const std::string &path, bool load) {
  directory = path;
  loading = load;
  opened = true;
  confirmation = Confirmation::None;
  selected = first = 0;
  error.clear();
  slots.clear();
  try {
    refresh();
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot list saves: %s",
                 failure.what());
  }
}

void VNSaveMenu::close() {
  finishRename();
  opened = false;
  confirmation = Confirmation::None;
  engine.GetRuntime().RemoveScene(this);
}

bool VNSaveMenu::hasSelectedSave() const {
  return selected < count() && (loading || selected > 0);
}

const VNSaveSlot &VNSaveMenu::selectedSave() const {
  if (!hasSelectedSave())
    throw std::logic_error("Select an existing save first");
  return slots.at(selected - (loading ? 0 : 1));
}

void VNSaveMenu::refresh(const std::string &preferredPath) {
  slots = VNSaveSlots(directory).list();
  selected = count() ? std::min(selected, count() - 1) : 0;
  for (std::size_t i = 0; i < slots.size(); ++i)
    if (slots[i].path == preferredPath)
      selected = i + (loading ? 0 : 1);
  first = std::min(first, selected);
  revealSelection();
}

void VNSaveMenu::reportError(const std::string &message) {
  error = message;
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save management failed: %s",
               message.c_str());
}

void VNSaveMenu::finishRename() {
  if (!renameDialog)
    return;
  const auto &scenes = engine.GetRuntime().GetActiveScenes();
  if (std::find(scenes.begin(), scenes.end(), renameDialog.get()) !=
      scenes.end()) {
    engine.GetRuntime().RemoveScene(renameDialog.get());
    if (SDL_TextInputActive(&engine.GetWindow()) &&
        !SDL_StopTextInput(&engine.GetWindow()))
      throw std::runtime_error(SDL_GetError());
  }
}

void VNSaveMenu::beginRename() {
  if (!hasSelectedSave())
    return;
  error.clear();
  const auto slot = selectedSave();
  if (!renameDialog)
    renameDialog =
        std::make_unique<TextInputDialog>(engine, "", nullptr, nullptr);
  renameDialog->configure(
      "New save name (.vn extension added automatically)",
      [this, slot](const std::string &name) {
        try {
          const auto path = VNSaveSlots(directory).rename(slot.name, name);
          refresh(path);
          error.clear();
        } catch (const std::exception &failure) {
          reportError(failure.what());
        }
        finishRename();
      },
      [this] { finishRename(); });
  engine.GetRuntime().AddScene(renameDialog.get());
}

void VNSaveMenu::deleteSelected() {
  try {
    VNSaveSlots(directory).remove(selectedSave().name);
    refresh();
    error.clear();
  } catch (const std::exception &failure) {
    reportError(failure.what());
  }
  confirmation = Confirmation::None;
}

void VNSaveMenu::revealSelection() {
  if (selected < first)
    first = selected;
  if (selected >= first + pageSize)
    first = selected - pageSize + 1;
}

void VNSaveMenu::activate() {
  if (count() == 0)
    return;
  if (confirmation == Confirmation::Delete) {
    deleteSelected();
    return;
  }
  if (!loading && selected != 0 && confirmation == Confirmation::None) {
    confirmation = Confirmation::Overwrite;
    return;
  }
  try {
    if (loading)
      engine.GetRuntime().loadGame(slots.at(selected).path);
    else {
      const auto path = selected == 0 ? VNSaveSlots(directory).nextPath()
                                      : slots.at(selected - 1).path;
      engine.GetRuntime().saveGame(path);
    }
    close();
  } catch (const std::exception &failure) {
    confirmation = Confirmation::None;
    error = failure.what();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save/load failed: %s",
                 failure.what());
    // A failed load may reinitialize the runtime during rollback.
    const auto &scenes = engine.GetRuntime().GetActiveScenes();
    if (std::find(scenes.begin(), scenes.end(), this) == scenes.end())
      engine.GetRuntime().AddScene(this);
  }
}

void VNSaveMenu::onEvent(EventRef event) {
  if (!opened)
    return;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE) {
      if (confirmation != Confirmation::None)
        confirmation = Confirmation::None;
      else
        close();
      return;
    }
    if (event.key.key == SDLK_RETURN) {
      activate();
      return;
    }
    if (confirmation == Confirmation::None && hasSelectedSave()) {
      if (event.key.key == SDLK_F2) {
        beginRename();
        return;
      }
      if (event.key.key == SDLK_DELETE) {
        confirmation = Confirmation::Delete;
        return;
      }
    }
    if (confirmation == Confirmation::None && count()) {
      if (event.key.key == SDLK_DOWN)
        selected = (selected + 1) % count();
      else if (event.key.key == SDLK_UP)
        selected = (selected + count() - 1) % count();
      else if (event.key.key == SDLK_PAGEDOWN)
        selected = std::min(count() - 1, selected + pageSize);
      else if (event.key.key == SDLK_PAGEUP)
        selected = selected > pageSize ? selected - pageSize : 0;
      revealSelection();
    }
  }
  if (UI::clicked(event, {890, 80, 180, 44})) {
    close();
    return;
  }
  if (confirmation != Confirmation::None) {
    if (UI::clicked(event, {290, 570, 320, 44}))
      activate();
    else if (UI::clicked(event, {650, 570, 320, 44}))
      confirmation = Confirmation::None;
    return;
  }
  if (hasSelectedSave()) {
    if (UI::clicked(event, {290, 570, 320, 44})) {
      beginRename();
      return;
    }
    if (UI::clicked(event, {650, 570, 320, 44})) {
      confirmation = Confirmation::Delete;
      return;
    }
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL && count()) {
    if (event.wheel.y < 0)
      selected = std::min(count() - 1, selected + 1);
    else if (event.wheel.y > 0 && selected > 0)
      --selected;
    revealSelection();
  }
  for (std::size_t row = 0; row < pageSize && first + row < count(); ++row) {
    const auto index = first + row;
    const float y = 160.0f + row * 50;
    if (loading || index > 0) {
      if (UI::clicked(event, {780, y, 130, 44})) {
        selected = index;
        beginRename();
        return;
      }
      if (UI::clicked(event, {920, y, 130, 44})) {
        selected = index;
        confirmation = Confirmation::Delete;
        return;
      }
    }
    if (UI::clicked(event, {230, y, 540, 44})) {
      selected = first + row;
      activate();
      return;
    }
  }
}

void VNSaveMenu::render(RendererRef renderer, float) {
  if (!opened)
    return;
  auto &ui = engine.GetUI();
  ui.panel(renderer, {0, 0, UI::width, UI::height}, {0, 0, 0, 190});
  ui.panel(renderer, {210, 60, 880, 610});
  ui.text(renderer, loading ? "Load game" : "Save game", 230, 85, 640,
          {255, 215, 100, 255});
  ui.button(renderer, {890, 80, 180, 44}, "Close [ESC]");
  for (std::size_t row = 0; row < pageSize && first + row < count(); ++row) {
    const auto index = first + row;
    const auto label = !loading && index == 0
                           ? std::string("+ New save")
                           : slots.at(index - (loading ? 0 : 1)).name;
    const float y = 160.0f + row * 50;
    const SDL_Rect rowClip{230, static_cast<int>(y), 820, 44};
    if (!SDL_SetRenderClipRect(&renderer, &rowClip))
      throw std::runtime_error(SDL_GetError());
    ui.button(renderer, {230, y, 540, 44}, label, selected == index);
    if (loading || index > 0) {
      ui.button(renderer, {780, y, 130, 44}, "Rename");
      ui.button(renderer, {920, y, 130, 44}, "Delete");
    }
    if (!SDL_SetRenderClipRect(&renderer, nullptr))
      throw std::runtime_error(SDL_GetError());
  }
  if (count() == 0)
    ui.text(renderer, "No saves found.", 230, 160, 820);
  ui.text(renderer,
          "Saves: " + std::to_string(slots.size()) +
              " | Arrows / PgUp / PgDn / wheel | Enter: select",
          230, 520, 820);
  if (confirmation != Confirmation::None) {
    const bool deleting = confirmation == Confirmation::Delete;
    ui.text(renderer,
            (deleting ? "Permanently delete " : "Overwrite ") +
                selectedSave().name + "?",
            230, 125, 820);
    ui.button(renderer, {290, 570, 320, 44},
              deleting ? "Delete [Enter]" : "Overwrite [Enter]");
    ui.button(renderer, {650, 570, 320, 44}, "Cancel [ESC]");
  } else if (hasSelectedSave()) {
    ui.button(renderer, {290, 570, 320, 44}, "Rename [F2]");
    ui.button(renderer, {650, 570, 320, 44}, "Delete [Delete]");
  }
  if (!error.empty()) {
    const SDL_Rect clip{230, 620, 820, 45};
    if (!SDL_SetRenderClipRect(&renderer, &clip))
      throw std::runtime_error(SDL_GetError());
    ui.text(renderer, error, 230, 620, 820, {255, 140, 140, 255});
    if (!SDL_SetRenderClipRect(&renderer, nullptr))
      throw std::runtime_error(SDL_GetError());
  }
}

} // namespace Kotonoha
