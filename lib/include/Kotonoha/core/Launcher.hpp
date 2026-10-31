#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Settings.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ImGuiContext;

namespace Kotonoha {
namespace ImGuiTheme { class ShaderFilePicker; }

// Pre-game configuration window (ImGui) for renderer, window, upscaling and
// logs. It edits the same EngineSettings file the Engine loads, so the game
// starts with what was chosen. Games derive from it to add their own options
// and drive it from the SDL callbacks before the Engine exists:
//   event() / iterate() until state() != Running, then close() and start the
//   Engine (the launcher window and ImGui context must be gone by then).
class Launcher {
public:
  enum class State { Running, Launch, Quit };
  using FileFilters = std::vector<std::pair<std::string, std::string>>;

  // settingsPath may be empty: nothing is persisted, use settings() instead.
  Launcher(std::string title, std::string settingsPath,
           EngineSettings defaults = {});
  virtual ~Launcher();
  Launcher(const Launcher &) = delete;
  Launcher &operator=(const Launcher &) = delete;

  void open();
  void close();
  bool isOpen() const;
  SDL_AppResult event(const SDL_Event &event);
  SDL_AppResult iterate();
  State state() const;
  EngineSettings &settings();
  const std::string &getSettingsPath() const;
  const std::string &getStatus() const;
  // Validates and saves, then finishes with State::Launch. On failure keeps
  // running and returns false with the reason in getStatus().
  bool launch();
  void quit();

protected:
  virtual void drawGameOptions();
  virtual bool validate(std::string &error);
  // Persists game options; called by launch() after the engine settings.
  virtual void save();
  void setStatus(std::string message, bool error);
  // Native file picker; `done` runs on the launcher thread on selection.
  void browseFile(const FileFilters &filters, const std::string &initial,
                  std::function<void(const std::string &)> done);
  // Native folder picker, same contract as browseFile().
  void browseFolder(const std::string &initial,
                    std::function<void(const std::string &)> done);

private:
  struct Dialog;
  static void SDLCALL dialogFinished(void *data, const char *const *files,
                                     int filter);
  std::string title, settingsPath, status;
  bool statusError = false;
  EngineSettings draft, defaults;
  WindowPtr window;
  RendererPtr renderer;
  ImGuiContext *context = nullptr;
  bool videoInitialized = false;
  State current = State::Running;
  float contentScale = 0;
  std::shared_ptr<Dialog> dialog;
  std::function<void(const std::string &)> dialogDone;
  std::unique_ptr<ImGuiTheme::ShaderFilePicker> shaderPicker;

  void draw();
  void drawVideo();
  void drawUpscaling();
  void drawTranslation();
  void drawAdvanced();
};

} // namespace Kotonoha
