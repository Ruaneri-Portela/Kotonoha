#pragma once
#include <ZeroNoTsukaima/Catalog.hpp>
#include <ZeroNoTsukaima/Scene.hpp>

#include <SDL3/SDL_rect.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// Runs the KAG-style scene scripts of the game: text pages, speaker names,
// choices (seladd/select), flags (eval/if), jumps (goto/next) and the
// presentation state the renderer draws (stage, event CG, characters, free
// layers) with the original environment timings: auto transitions, layer
// fades, actions (shakes, CG scrolls), timed waits and begintrans/endtrans
// crossfades. Animations run on a game clock advanced by the host, so a
// replay (which ends every timed wait at once) reaches the same state.
class Game {
public:
  // Returns the unpacked bytes of entry `index` of SCENE_ID.
  using SceneLoader = std::function<bool(size_t index, std::vector<uint8_t> *out)>;

  Game(std::vector<std::string> sceneNames, SceneLoader loader);

  // Character tags ("ルイズ {学生服 怒り 左 出}") are resolved through this table; without it
  // they are ignored.
  void SetCharacterTable(const CharacterTable *table) { table_ = table; }
  // Stage names (ENVINIT "stages", e.g. 黒 / 白 / サイトの居間) also work as bare tags.
  void SetStageNames(std::set<std::string> names) { stageNames_ = std::move(names); }

  struct Sprite {
    std::string character;
    std::string image; // "TCG01_1D3"
    int xpos = 0;      // centre offset from the middle of the screen
  };

  // One drawable of the scene at the current clock.
  struct Element {
    enum Kind { Stage, Event, Character, Layer } kind = Stage;
    std::string image; // stage words ("サイトの居間 昼", "黒"), event CG, sprite image or layer file
    float x = 0, y = 0; // Character: centre offset; others: top-left offset
    float alpha = 1;
    // Character light of the time of day (ENVINIT times 夕 / 夜 / 夜闇), a colour modulation.
    float tint[3] = {1, 1, 1};
  };

  // What the story is blocked on.
  // Coffee: the `coffee` tag opened the coffee-break menu (CommandState "coffee");
  // the host ends it with FinishWait() when the player picks NEXT.
  enum class Block { None, Click, Choice, Time, Voice, Se, Movie, Coffee, End };

  // Requests for the host, in script order (see TakeCues).
  struct Cue {
    enum Kind { Se, Voice, Movie } kind = Se;
    std::string name; // Se: SOUND_ID name ("seb12")
    int number = 0;   // Voice: VOICE_ID index, Movie: movie number
  };

  bool Start(const std::string &scriptName);
  bool DebugJump(const std::string &scriptName);
  void Pump();
  bool Click();
  bool Choose(size_t index);
  // Advances the game clock; a timed wait that ends lets the story run on.
  void Advance(int64_t ms);
  // Ends the current Time / Voice / Se / Movie wait now (player skip, replay,
  // or the host saw the sound/movie end). Inside beginskip..endskip the whole
  // skippable section is run through.
  void FinishWait(bool skipSection = false);

  Block Blocked() const;
  bool WaitingForClick() const { return waiting_; }
  bool Choosing() const { return choosing_; }
  bool Finished() const { return finished_; }
  bool MessageVisible() const { return msgOn_ && !text_.empty(); }
  int64_t Clock() const { return clock_; }

  const std::string &Text() const { return text_; }
  const std::string &Speaker() const { return speaker_; }
  int Voice() const { return voice_; }
  const std::vector<std::string> &Choices() const { return choiceText_; }
  std::string Stage() const;
  std::string Event() const { return event_.visible ? event_.image : std::string(); }
  const std::string &Bgm() const { return bgm_; }
  int Movie() const { return movie_; }
  std::vector<Cue> TakeCues() { return std::move(cues_); }
  // Characters standing on screen, back to front.
  std::vector<Sprite> Sprites() const;
  // Everything to draw, back to front, at the current clock.
  std::vector<Element> Scene() const { return SceneAt(clock_); }
  // A running whole-screen crossfade: the old scene (captured when it began)
  // fades out over the current one. `serial` changes with every transition.
  struct Transition {
    std::vector<Element> from;
    int64_t start = 0;
    int duration = 0;
    uint32_t serial = 0;
  };
  // Null when no transition runs; `progress` gets 0..1.
  const Transition *ActiveTransition(float *progress) const;
  const std::string &ScriptName() const { return scriptName_; }
  // Translation route of the running script: its SCENE_ID index.
  size_t SceneIndex() const { return sceneIndex_; }
  // Index of the running tag and the last *label at or before it ("" if none).
  size_t Pc() const { return pc_; }
  std::string CurrentLabel() const;
  // Optional text replacement for each [ch] segment, by route (scene index).
  std::function<std::string(size_t scene, const std::string &text)> translate;
  // Displayable texts of a scene, in order and without repeats: [ch]
  // segments, [dispname] names and [seladd] choices.
  static std::vector<std::string> SceneTexts(const std::vector<SceneTag> &tags);
  uint32_t SceneVersion() const { return version_; }
  // MoveState (moveinit / moveadd / move): while Moving() the choices are the
  // map destinations; Choose(i) goes to the i-th one.
  struct MoveDestination {
    std::string text;  // "図書室" (movePositions key)
    std::string pose;  // character shown next to the focused button ("TCG05_1A1"), may be empty
    std::string storage;
  };
  bool Moving() const { return choosing_ && moving_; }
  const std::string &MoveBack() const { return moveBack_; }
  const std::vector<MoveDestination> &MoveDestinations() const { return moveShown_; }
  // Overlays of OVERRIDE.NUT shown during a timed wait: LikeMeter (好感度
  // action=up/down name=RU..) and TndlMeter (ツンデレパラメータ param=n, the
  // needle moves from `from` to `to` = f.prm_tundere_eval).
  struct Meter {
    enum Kind { None, Like, Tundere } kind = None;
    std::string name; // Like: RU / CU / TA / SI / AN / CH / RI / YRI
    int delta = 0;    // Like: +1 up / -1 down; Tundere: the param
    int from = 0, to = 0;
    int64_t start = 0, end = 0;
  };
  const Meter *ActiveMeter() const { return meter_.kind != Meter::None && clock_ < meter_.end ? &meter_ : nullptr; }
  // RouletteState (tndlslot [tun=true]): a choice whose index is the
  // f.prm_tundere_boot the slot stopped at (0..17, +18 for the tsun slot).
  bool Roulette() const { return choosing_ && roulette_; }
  bool RouletteTsun() const { return rouletteTsun_; }
  // selopt: the area the select window spreads its buttons over.
  SDL_Rect SelectArea() const { return selectArea_; }
  // Identifies the page waiting for a click (read marks for skip mode "既読").
  std::string PageId() const { return waiting_ ? scriptName_ + "@" + std::to_string(pagePc_) : std::string(); }
  // String variables (f.title, the save title).
  std::string StringVar(const std::string &name) const {
    auto it = strings_.find(name);
    return it == strings_.end() ? std::string() : it->second;
  }
  int64_t Flag(const std::string &name) const;
  // System variables (sf.*) restored from the system save before Start.
  void SetFlag(const std::string &name, int64_t value) { flags_[name] = value; }
  const std::map<std::string, int64_t> &Flags() const { return flags_; }

private:
  struct Anim {
    float from = 0, to = 0;
    int64_t start = 0;
    int duration = 0;
    bool decel = false;
    float At(int64_t t) const;
    int64_t End() const { return start + duration; }
    void Set(float value) { from = to = value; duration = 0; }
    void To(int64_t now, float target, int ms, bool decelerate = false);
  };
  struct Props {
    Anim alpha{1, 1}, x, y;
    int shake = 0, shakeInterval = 50; // LayerVibrateActionModule
  };
  struct Image {
    std::string image;
    bool visible = false;
    Props p;
  };
  struct LayerState {
    std::string name, file;
    int level = 0;
    bool visible = true;
    Props p;
  };

  bool LoadScene(size_t index, const std::string &label);
  bool LoadByName(const std::string &name);
  bool Jump(const std::string &label);
  void Execute(const SceneTag &tag);
  void Assign(const std::string &expression);
  bool ExecuteCharacter(const SceneTag &tag);
  bool ExecuteStage(const SceneTag &tag, const std::string &bareName);
  void ExecuteEvent(const SceneTag &tag);
  bool ExecuteLayer(const SceneTag &tag);
  // Applies an action / reset key to `props`; false when `key` is no action.
  bool ApplyAction(const std::string &key, Props &props, bool isEvent);
  void ApplyActions(const SceneTag &tag, Props &props, bool isEvent = false);
  void AutoTransition(const SceneTag &tag, int ms, bool hideMessage);
  void WaitFor(int ms);
  int64_t ActionsEnd() const;
  void StopActions();
  std::vector<Element> SceneAt(int64_t t) const;
  int64_t Eval(const std::string &expression) const;

  std::vector<std::string> names_;
  SceneLoader loader_;
  std::vector<SceneTag> tags_;
  std::map<std::string, size_t> labels_;
  size_t sceneIndex_ = 0, pc_ = 0;
  std::string scriptName_;
  std::map<std::string, int64_t> flags_;
  std::map<std::string, std::string> strings_; // string variables (f.prm_tundere_next = "st05_00.ks")
  std::set<std::string> stageNames_;

  std::string text_, speaker_, bgm_;
  std::string stageName_, stageTime_ = "昼";
  Image stage_, event_;
  struct CharacterState {
    std::string name, pose, dress, face;
    int xpos = 0;
    bool visible = false;
    Props p;
  };
  const CharacterTable *table_ = nullptr;
  std::vector<CharacterState> characters_;
  std::vector<LayerState> layers_;
  std::vector<Cue> cues_;
  int voice_ = 0, entryVoice_ = 0, movie_ = -1;
  bool msgOn_ = false, waiting_ = false, choosing_ = false, finished_ = false;
  // Timed waits run on the game clock.
  int64_t clock_ = 0, waitUntil_ = -1;
  Block external_ = Block::None; // Voice / Se / Movie waits ended by the host
  bool transMode_ = false, inSkip_ = false, skipping_ = false;
  std::vector<Element> transFrom_;
  Transition transition_;
  std::vector<std::string> choiceText_, choiceTarget_;
  bool moving_ = false;
  bool roulette_ = false, rouletteTsun_ = false;
  Meter meter_;
  SDL_Rect selectArea_{0, 0, 640, 448};
  size_t pagePc_ = 0;
  int moveCount_ = 0;
  std::string moveBack_, moveBgm_, moveDone_;
  std::vector<MoveDestination> moves_, moveShown_;
  uint32_t version_ = 0;
};

} // namespace ZeroNoTsukaima
