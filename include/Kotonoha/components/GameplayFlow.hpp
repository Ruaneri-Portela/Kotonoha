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
  // True once the story has ended (no next scene): the flow returns to the
  // title / front end, which EnterFrontend(false) receives.
  virtual bool Ended() const { return false; }
  // Per-frame upkeep (audio queues, animations).
  virtual void Tick() {}
  // True while saved inputs are replayed: sessions should stay silent.
  virtual void SetReplaying(bool) {}
  // Draws scene layers (background, characters, CGs) in logical coordinates.
  virtual void Draw(SDL_Renderer *renderer) = 0;
  // Optional original message window, drawn in logical coordinates under the
  // text box text while a message shows (pair with TextBox::Style::frame = false).
  virtual void DrawMessageWindow(SDL_Renderer *) {}
  // Buttons of that window: left clicks (logical coordinates) while a message
  // shows go here first. Consumed swallows the click; ToggleAuto also flips
  // auto mode as the A key does.
  enum class WindowClick { None, Consumed, ToggleAuto };
  virtual WindowClick MessageWindowClick(float /*x*/, float /*y*/) { return WindowClick::None; }
  // Optional original choice window. When OwnsChoices() is true the flow does
  // not open its choice panel: while Choosing(), events go to ChoiceEvent()
  // (pointer in logical coordinates), which returns the picked index or -1, and
  // DrawChoices() paints the window over the text box.
  virtual bool OwnsChoices() const { return false; }
  virtual int ChoiceEvent(const SDL_Event &) { return -1; }
  virtual void DrawChoices(SDL_Renderer *) {}

  // Optional original in-game screens (command menu, save/load pages, back
  // log). The flow asks OpenScreen() on Esc / right click (Command) and on
  // wheel up / Page Up (History); true means the session handled the request
  // (false falls back to the generic pause menu). While ScreenActive(), every
  // event goes to ScreenEvent() (pointer in logical coordinates) and
  // DrawScreen() paints over the whole game.
  enum class Screen { Command, History };
  virtual bool OpenScreen(Screen) { return false; }
  virtual bool ScreenActive() const { return false; }
  virtual void ScreenEvent(const SDL_Event &) {}
  virtual void DrawScreen(SDL_Renderer *) {}

  // Requests the flow carries out for the session's own screens, polled every
  // frame (in play and in the front end): write the replay inputs into a slot
  // (labelled with SaveLabel()), load a slot, or return to the title.
  struct FlowRequest {
    enum Kind { None, Save, Load, Title } kind = None;
    int slot = 0;
  };
  virtual FlowRequest TakeRequest() { return {}; }
  virtual std::string SaveLabel() const { return {}; }

  // Text pacing. TextDelayMs(): milliseconds per revealed character (0 shows
  // the page at once; a click completes the page first). AutoDelayMs(): the
  // pause of auto mode (A key) after a page is shown and VoiceActive() ended;
  // negative disables auto mode. AutoChanged() reports toggles.
  virtual int TextDelayMs() const { return 0; }
  virtual int AutoDelayMs() const { return -1; }
  virtual bool VoiceActive() const { return false; }
  virtual void AutoChanged(bool) {}
  // Skip (Ctrl held): pages advance every frame while CanSkip() is true.
  virtual bool CanSkip() const { return false; }
  // True while the story waits for a click (not for a timed event).
  virtual bool WaitingForClick() const { return HasMessage(); }

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
  // and only wraps it with the shared splash / debug UI / present steps.
  // The story hooks above are unused in that mode.
  virtual bool OwnsFrontend() const { return false; }
  // Per-event handling; non-CONTINUE ends the app.
  virtual SDL_AppResult HandleEvent(SDL_Event *, Kotonoha_Game *) { return SDL_APP_CONTINUE; }
  // Renders the whole game frame without presenting; non-CONTINUE ends the app.
  virtual SDL_AppResult RenderFrame(::Kotonoha::Kotonoha *, Kotonoha_Game *) { return SDL_APP_CONTINUE; }
  // Gameplay shown to the debug UI.
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
    int saveSlots = SaveStore::kSlots;
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
  // game frame, debug UI and present.
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
  void HandleRequest(const GameSession::FlowRequest &request);
  void Pace();
  bool OpenScreen(GameSession::Screen screen);

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
  // Text pacing: the page being revealed, when it began, and auto mode.
  std::string pageKey_;
  uint64_t pageStart_ = 0, pageDoneAt_ = 0;
  size_t pageLength_ = 0;
  bool pageComplete_ = true;
  bool auto_ = false;
  Canvas canvas_;
  bool canvasReady_ = false;
};

} // namespace Kotonoha
