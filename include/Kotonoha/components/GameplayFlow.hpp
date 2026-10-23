#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/ChoiceMenu.hpp>
#include <Kotonoha/components/TextBox.hpp>

namespace Kotonoha {

class Kotonoha;
class Gameplay;

// What a game port provides to the engine. The port owns the story logic and
// its assets; the engine's GameplayFlow owns the player-facing flow (title, pause,
// save/load, text box, choices, input).
class GameSession {
public:
  virtual ~GameSession() = default;

  // Restarts from the opening scene and runs until the story blocks.
  virtual bool Start() = 0;
  // Applies one player input (-1 = click/advance, >=0 = chosen option) and runs until blocked.
  virtual void Step(int input) = 0;
  // Free-running progress between inputs (timed events). Called every frame while playing.
  virtual void Idle() {}
  // A click while the story waits on something timed (e.g. a voice). Return true to
  // consume it: it is not recorded, so replays (which skip such waits) stay in sync.
  virtual bool Skip() { return false; }

  virtual bool Choosing() const = 0;
  virtual std::vector<std::string> Choices() const = 0;
  virtual bool HasMessage() const = 0;
  virtual std::string Speaker() const = 0;
  virtual std::string Text() const = 0;
  virtual std::string Caption() const { return {}; }

  // Called after state changed (new text/scene): load pictures, start audio.
  virtual void Sync() {}
  // Per-frame upkeep (audio queues, animations).
  virtual void Tick() {}
  // True while saved inputs are replayed: sessions should stay silent.
  virtual void SetReplaying(bool) {}
  // Draws scene layers (background, characters, CGs) in logical coordinates.
  virtual void Draw(SDL_Renderer *renderer) = 0;

  // Optional original title screen. When TitleItems() is not empty the flow shows
  // it instead of the generic title menu: items are hit-tested in logical
  // coordinates and DrawTitle() paints the screen with item `hover` highlighted.
  enum class TitleAction { NewGame, Load, Quit };
  struct TitleItem {
    SDL_FRect rect;
    TitleAction action;
  };
  virtual std::vector<TitleItem> TitleItems(bool /*hasSaves*/) const { return {}; }
  virtual void DrawTitle(SDL_Renderer *, int /*hover*/) {}

  // Optional animated retail front end (boot logos, opening movie, original
  // title and mode menus). When HasFrontend() is true the flow shows it instead
  // of any title above: EnterFrontend(true) once at boot, EnterFrontend(false)
  // when the player returns to the title from the pause menu. While it shows,
  // events arrive with pointer coordinates in logical space, FrontendTick() runs
  // every frame and an action it returns is carried out by the flow (Load opens
  // the shared load page; backing out of it returns to the front end).
  virtual bool HasFrontend() const { return false; }
  virtual void EnterFrontend(bool /*boot*/) {}
  virtual void FrontendEvent(const SDL_Event &) {}
  virtual std::optional<TitleAction> FrontendTick() { return std::nullopt; }
  virtual void DrawFrontend(SDL_Renderer *) {}

  // A port with its own complete retail front end (title, menus, saves,
  // back log) returns true: the flow then forwards every event and
  // frame to the session instead of running its generic title/text/menu flow,
  // and only wraps it with the shared splash / debug UI / MCP / present steps.
  // The story hooks above are unused in that mode.
  virtual bool OwnsFrontend() const { return false; }
  // Per-event handling; non-CONTINUE ends the app.
  virtual SDL_AppResult HandleEvent(SDL_Event *, Kotonoha_Game *) { return SDL_APP_CONTINUE; }
  // Renders the whole game frame without presenting; non-CONTINUE ends the app.
  virtual SDL_AppResult RenderFrame(::Kotonoha::Kotonoha *, Kotonoha_Game *) { return SDL_APP_CONTINUE; }
  // Gameplay shown to the debug UI and the MCP server.
  virtual ::Kotonoha::Gameplay *RunningGameplay() { return nullptr; }
  // Debug UI pass; false when the player asked to quit from it.
  virtual bool RunDebugUi(::Kotonoha::Kotonoha *app, Kotonoha_Game &game);
};

class GameplayFlow {
public:
  struct Config {
    std::string id;    // save-slot namespace
    std::string title; // title screen / window title
    float width = 800, height = 600; // logical resolution
    TextBox::Style textBox;
  };

  explicit GameplayFlow(Config config);
  ~GameplayFlow();

  // Takes the session and starts it; the title screen is shown once a font is available.
  bool Attach(std::unique_ptr<GameSession> session);
  GameSession *Session() { return session_.get(); }

  SDL_AppResult HandleEvent(SDL_Event *event, Kotonoha_Game *game);
  // Advances the flow, draws the whole frame (without presenting). The game
  // scene (session Draw/DrawFrontend/DrawTitle) is the "game" layer of a
  // canvas sized to the letterboxed area in output pixels, so pictures drawn
  // through CanvasImage keep the upscaler's detail; the flow UI (text box,
  // choices, menus) follows in the logical resolution. The logical
  // presentation is disabled again before returning, so anything drawn
  // afterwards (splash, debug UI, overlays) uses the full window.
  void Frame(Kotonoha_Game *game, TTF_Font *font);
  // The shared per-frame driver used by every port's LocalIterate: splash,
  // game frame, debug UI, MCP service and present.
  SDL_AppResult RunFrame(::Kotonoha::Kotonoha *app, Kotonoha_Game *game, TTF_Font *font);

  bool Playing() const { return playing_; }
  size_t InputCount() const { return inputs_.size(); }
  Canvas &SceneCanvas() { return canvas_; }

private:
  // Applies the game viewport (letterboxed logical resolution) for its lifetime.
  struct GameViewport {
    GameViewport(SDL_Renderer *renderer, const Config &config);
    ~GameViewport();
    SDL_Renderer *renderer;
  };

  static Kotonoha_Scene_Status DrawSceneLayer(KOTONOHA_SCENE_CALL);
  void DrawScene(SDL_Renderer *renderer);
  SDL_FRect SceneRect(SDL_Renderer *renderer) const;

  bool Begin(bool sync = true);
  void Advance(int choice);
  void Replay(const std::vector<int> &inputs);
  void ShowTitle();
  void OnMenu(const GameMenu::Event &ev);
  void SyncChoices();
  void HandleTitleEvent(SDL_Event *event, Kotonoha_Game *game);
  void PickTitle(size_t index);
  void RunTitleAction(GameSession::TitleAction action);

  Config config_;
  std::unique_ptr<GameSession> session_;
  TextBox textBox_;
  GameMenu menu_;
  ChoiceMenu choices_;
  TTF_Font *font_ = nullptr;
  std::vector<int> inputs_; // replayable inputs: -1 click, >=0 choice
  bool playing_ = false;
  bool choicesOpen_ = false;
  std::vector<GameSession::TitleItem> titleItems_; // non-empty while the session's title shows
  int titleHover_ = -1;
  bool frontend_ = false; // the session's front end shows instead of the title
  bool booted_ = false;   // the front end already ran its boot sequence
  std::string lastLogged_;
  Canvas canvas_;
  bool canvasReady_ = false;
};

} // namespace Kotonoha
