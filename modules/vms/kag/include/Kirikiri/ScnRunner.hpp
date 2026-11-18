#pragma once

#include <Kirikiri/KagScript.hpp>
#include <Kirikiri/PsbFile.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Kirikiri {

struct ScnKeyframe {
  std::string handler; // "MoveAction", "SinAction", "RandomAction", "wait"
  double startValue = 0.0;
  bool hasStart = false;
  bool startRelative = false;
  double targetValue = 0.0;
  bool targetRelative = false;
  double vibration = 0.0;
  double cycle = 0.0;
  int durationMs = 0;
  int elapsedMs = 0;
  std::string accel; // "", "accel", "decel", "acdec"
};

struct ScnCharacterState {
  std::string name;
  std::string file; // e.g. "e-mote3.0ショコラパジャマa.psb"
  std::string posName;
  std::string timeline;
  int resolution = 200;
  int showmode = 1;
  double xpos = 0.0;
  double ypos = 0.0;
  double zpos = 0.0;
  double offsetx = 0.0;
  double offsety = 0.0;
  double imagerotate = 0.0;
  double imagex = 0.0;
  double imagey = 0.0;
  double opacity = 255.0;
  std::map<std::string, double> variables; // $face_*, $head_*, $body_*, etc.
  std::map<std::string, std::vector<ScnKeyframe>> activeActions;
};

struct ScnLayerState {
  std::string name;
  std::string className; // "stage", "event", "emotion"
  std::string file;
  int showmode = 1;
  double xpos = 0.0;
  double ypos = 0.0;
  double zpos = 0.0;
  double opacity = 255.0;
  int blurx = 0;
  int blury = 0;
  std::map<std::string, std::vector<ScnKeyframe>> activeActions;
};

struct ScnTransition {
  std::string method; // "crossfade", "universal"
  std::string rule;   // "時計", "斜右上", "斜左下"
  int timeMs = 0;
};

class ScnRunner {
public:
  struct Callbacks {
    KagScript::Callbacks kag;
    std::function<void(std::string speaker, std::string text,
                       std::string nameplate, std::vector<std::string> voices)>
        showDialogue;
    std::function<void(std::string flag)> unlockFlag;
    std::function<void(bool visible)> setMessageWindowVisible;
    std::function<void(bool enabled)> setTouchMode;
    std::function<void()> returnToTitle;
  };

  struct State {
    std::size_t sceneIndex = 0;
    std::size_t lineIndex = 0;
    std::int64_t savepointId = 0;
    std::string snapshotJson;
    std::set<std::string> unlockedFlags;
    bool finished = false;
  };

  ScnRunner(Kotonoha::Providers::PsbFile psb, Callbacks cb,
            std::size_t languageIndex = 2);

  bool isBlocked() const noexcept { return blocked_; }
  bool isFinished() const noexcept { return finished_; }
  void setBlocked(bool blocked) noexcept { blocked_ = blocked; }

  std::size_t sceneIndex() const noexcept { return sceneIdx_; }
  std::size_t position() const noexcept { return pc_; }
  std::size_t languageIndex() const noexcept { return langIdx_; }
  void setLanguageIndex(std::size_t index) noexcept { langIdx_ = index; }

  const ScnLayerState &stage() const noexcept { return stage_; }
  const std::map<std::string, ScnLayerState> &events() const noexcept {
    return events_;
  }
  const std::map<std::string, ScnCharacterState> &characters() const noexcept {
    return characters_;
  }
  const std::string &currentBgm() const noexcept { return bgmFile_; }
  const std::string &currentSe() const noexcept { return seFile_; }
  const ScnTransition &lastTransition() const noexcept {
    return lastTransition_;
  }
  bool messageWindowVisible() const noexcept { return messageWindowVisible_; }
  bool touchMode() const noexcept { return touchMode_; }
  bool inSkipSection() const noexcept { return skipSection_; }
  const std::set<std::string> &unlockedFlags() const noexcept {
    return unlockedFlags_;
  }
  const std::string &currentNameplate() const noexcept {
    return currentNameplate_;
  }
  const std::vector<std::string> &currentVoices() const noexcept {
    return currentVoices_;
  }

  bool jumpToLabel(const std::string &label);
  void run();
  void resumeDialogue();
  void resumeWait();
  void tickAnimations(int elapsedMs);
  void finishAllActions();

  State saveState() const;
  void restoreState(const State &state);
  void applySavepointSnapshot(const Kotonoha::Providers::PsbValue &snapshot);

  static std::string stripFormatting(std::string text);
  static std::pair<std::string, std::string>
  resolveSpeaker(const std::string &primary, const std::string &combined,
                 const std::string &fallbackDisplay = {});

private:
  struct PendingDelay {
    std::string trigger; // "vlstop", "ラベル0".."ラベル5", "w6", or "ms"
    int remainingMs = 0;
    Kotonoha::Providers::PsbValue command;
  };

  void executeLine(const Kotonoha::Providers::PsbValue &line, bool &blocks);
  void executeCommand(const Kotonoha::Providers::PsbValue &cmdList,
                      bool &blocks);
  void executeEnvUpdate(const Kotonoha::Providers::PsbValue &updateList);
  void applyObjectDict(const std::string &objName, const std::string &cls,
                       const Kotonoha::Providers::PsbValue &dict);
  void fireDelayTrigger(const std::string &trigger);
  void initDefaultEmoteVars(ScnCharacterState &ch);
  double &characterPropertyRef(ScnCharacterState &ch, const std::string &prop);
  double &layerPropertyRef(ScnLayerState &layer, const std::string &prop);

  Kotonoha::Providers::PsbFile psb_;
  Callbacks cb_;
  std::size_t langIdx_ = 2;
  std::size_t sceneIdx_ = 0;
  std::size_t pc_ = 0;
  bool blocked_ = false;
  bool finished_ = false;
  bool messageWindowVisible_ = true;
  bool touchMode_ = false;
  bool skipSection_ = false;

  std::int64_t lastSavepointId_ = 0;
  Kotonoha::Providers::PsbValue lastSnapshot_;
  std::set<std::string> unlockedFlags_;

  ScnLayerState stage_;
  std::map<std::string, ScnLayerState> events_;
  std::map<std::string, ScnCharacterState> characters_;
  std::string bgmFile_;
  bool bgmLoop_ = true;
  std::string seFile_;
  ScnTransition lastTransition_;
  std::string currentNameplate_;
  std::vector<std::string> currentVoices_;
  std::vector<PendingDelay> pendingDelays_;
};

} // namespace Kirikiri
