#pragma once
#include <ZeroNoTsukaima/Catalog.hpp>
#include <ZeroNoTsukaima/Scene.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// Runs the KAG-style scene scripts of the game: text pages, speaker names,
// choices (seladd/select), flags (eval/if), jumps (goto/next) and the
// presentation state (stage, event CG, BGM) the renderer draws.
class Game {
public:
  // Returns the unpacked bytes of entry `index` of SCENE_ID.
  using SceneLoader = std::function<bool(size_t index, std::vector<uint8_t> *out)>;

  Game(std::vector<std::string> sceneNames, SceneLoader loader);

  // Character tags ("ルイズ {学生服 怒り 左 出}") are resolved through this table; without it
  // they are ignored.
  void SetCharacterTable(const CharacterTable *table) { table_ = table; }

  struct Sprite {
    std::string character;
    std::string image; // "TCG01_1D3"
    int xpos = 0;      // centre offset from the middle of the screen
  };

  bool Start(const std::string &scriptName);
  void Pump();
  bool Click();
  bool Choose(size_t index);

  bool WaitingForClick() const { return waiting_; }
  bool Choosing() const { return choosing_; }
  bool Finished() const { return finished_; }
  bool MessageVisible() const { return msgOn_ && !text_.empty(); }

  const std::string &Text() const { return text_; }
  const std::string &Speaker() const { return speaker_; }
  int Voice() const { return voice_; }
  const std::vector<std::string> &Choices() const { return choiceText_; }
  const std::string &Stage() const { return stage_; }
  const std::string &Event() const { return event_; }
  const std::string &Bgm() const { return bgm_; }
  // Characters standing on screen, back to front.
  std::vector<Sprite> Sprites() const;
  const std::string &ScriptName() const { return scriptName_; }
  uint32_t SceneVersion() const { return version_; }
  int64_t Flag(const std::string &name) const;

private:
  bool LoadScene(size_t index, const std::string &label);
  bool LoadByName(const std::string &name);
  bool Jump(const std::string &label);
  void Execute(const SceneTag &tag);
  void Assign(const std::string &expression);
  bool ExecuteCharacter(const SceneTag &tag);
  int64_t Eval(const std::string &expression) const;

  std::vector<std::string> names_;
  SceneLoader loader_;
  std::vector<SceneTag> tags_;
  std::map<std::string, size_t> labels_;
  size_t sceneIndex_ = 0, pc_ = 0;
  std::string scriptName_;
  std::map<std::string, int64_t> flags_;

  std::string text_, speaker_, stage_, event_, bgm_;
  struct CharacterState {
    std::string name, pose, dress, face;
    int xpos = 0;
    bool visible = false;
  };
  const CharacterTable *table_ = nullptr;
  std::vector<CharacterState> characters_;
  int voice_ = 0;
  bool msgOn_ = false, waiting_ = false, choosing_ = false, finished_ = false;
  std::vector<std::string> choiceText_, choiceTarget_;
  uint32_t version_ = 0;
};

} // namespace ZeroNoTsukaima
