// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <N2System/AudioSystem.hpp>
#include <N2System/DialogueSystem.hpp>
#include <N2System/NsbScript.hpp>
#include <N2System/SaveState.hpp>
#include <N2System/SceneGraph.hpp>
#include <N2System/VariableStore.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace N2System {

struct NsbValue {
  enum class Kind { Int, Float, String };
  Kind kind = Kind::Int;
  std::int64_t intVal = 0;
  double floatVal = 0.0;
  std::string strVal;
  bool isDelta = false;
  std::string lvalueKey;

  static NsbValue makeInt(std::int64_t v) {
    NsbValue out;
    out.kind = Kind::Int;
    out.intVal = v;
    out.floatVal = static_cast<double>(v);
    return out;
  }
  static NsbValue makeFloat(double v) {
    NsbValue out;
    out.kind = Kind::Float;
    out.floatVal = v;
    out.intVal = static_cast<std::int64_t>(v);
    return out;
  }
  static NsbValue makeString(std::string v) {
    NsbValue out;
    out.kind = Kind::String;
    out.strVal = std::move(v);
    return out;
  }

  std::int64_t asInt() const;
  double asFloat() const;
  std::string asString() const;
  bool isTruthy() const;
};

struct PreDialogueBlock {
  std::string id;
  std::string boxName;
  std::string speaker;
  std::string voicePath;
  std::string text;
  std::string rawXml;
};

struct PhoneState {
  bool initialized = false;
  bool phoneOpen = false;
  bool waitIconVisible = false;
  int mailBoxCursor = 0;
  std::string attachedCg;
  std::set<std::string> addressBook;
  std::set<std::string> mailReceiveFlags;
  std::set<std::string> mailViewFlags;
  std::set<std::string> mailReplyFlags;
  std::set<std::string> mailTriggerFlags;
  std::set<std::string> mailReplySendFlags;
  std::set<std::string> tipsUnlocked;
  std::set<std::string> achievementsUnlocked;
};

struct ActiveMovieRequest {
  std::string entityName;
  std::string path;
  bool loop = false;
  bool active = false;
  bool fullscreen = false;
};

// Callback to load a compiled .nsb and optional .map from the asset provider.
using NsbLoaderFn = std::function<bool(std::string_view nsbPath,
                                       std::vector<std::uint8_t> &nsbOut,
                                       std::vector<std::uint8_t> &mapOut)>;

// Callback to query image dimensions (for GetImageHorizon / GetImageVertical).
using ImageSizeFn = std::function<bool(std::string_view path, int &w, int &h)>;

enum class MediaTimeKind { Remaining, Duration, Elapsed };
enum class SaveActionKind { Save, Load, Delete };
struct PendingSaveAction {
  SaveActionKind kind;
  std::uint32_t slot = 0;
};
using MediaTimeQueryFn =
    std::function<std::int64_t(std::string_view, MediaTimeKind)>;
using SaveExistsFn = std::function<bool(std::uint32_t)>;

// Compiled Steins;Gate .nsb + .map stack-based bytecode VM.
class NsbVm {
public:
  NsbVm();

  void setLoader(NsbLoaderFn loader) { loader_ = std::move(loader); }
  void setImageSizeQuery(ImageSizeFn fn) { imageSizeFn_ = std::move(fn); }
  void setMediaTimeQuery(MediaTimeQueryFn fn) {
    mediaTimeQuery_ = std::move(fn);
  }
  void setSaveExistsQuery(SaveExistsFn fn) {
    saveExistsQuery_ = std::move(fn);
  }
  // Execute file operations only AFTER VM::step has returned. In particular,
  // Load can replace all threads and the scene tree.
  std::optional<PendingSaveAction> takePendingSaveAction() {
    auto operation = pendingSaveAction_;
    pendingSaveAction_.reset();
    return operation;
  }
  void setConfig(const SystemIniConfig &cfg);
  const SystemIniConfig &config() const { return config_; }

  bool boot(std::string_view startScript = "nss/0_boot.nss",
            std::string_view initialChapter = "chapter.main");
  bool jumpToScript(std::string_view scriptPath,
                    std::string_view chapterOrScene = "chapter.main");

  // Execute up to `budget` instructions across active threads.
  std::size_t step(std::size_t budget = 4096);
  void update(float dtSeconds);

  // User interaction hooks
  bool advance();
  void triggerChoice(std::string_view choiceEntityName);
  void triggerChoiceIndex(int index);
  void notifyMovieFinished(std::string_view entityName = "");
  void setMousePosition(int x, int y) {
    mouseX_ = x;
    mouseY_ = y;
  }

  bool finished() const { return finished_; }
  bool waitingForKey() const;
  bool waitingForChoice() const { return !activeChoices_.empty(); }
  const std::vector<std::string> &activeChoices() const {
    return activeChoices_;
  }
  const ActiveMovieRequest &activeMovie() const { return activeMovie_; }
  const PhoneState &phoneState() const { return phone_; }

  std::string currentScriptPath() const;
  std::uint32_t currentLine() const;
  std::uint64_t totalExecuted() const { return totalExecuted_; }
  std::size_t threadCount() const { return threads_.size(); }

  SceneGraph &sceneGraph() { return sceneGraph_; }
  const SceneGraph &sceneGraph() const { return sceneGraph_; }
  AudioSystem &audioSystem() { return audioSystem_; }
  const AudioSystem &audioSystem() const { return audioSystem_; }
  DialogueSystem &dialogueSystem() { return dialogueSystem_; }
  const DialogueSystem &dialogueSystem() const { return dialogueSystem_; }
  VariableStore &variables() { return legacyVars_; }
  const VariableStore &variables() const { return legacyVars_; }

  const NsbValue *findVar(std::string_view name) const;
  void setVar(std::string_view name, const NsbValue &val);

  SaveData saveState() const;
  bool loadState(const SaveData &data);

  static PreDialogueBlock parsePreBlock(std::string_view id,
                                        std::string_view boxName,
                                        std::string_view rawXml);

private:
  struct CallFrame {
    std::shared_ptr<NsbScript> script;
    std::size_t pc = 0;
    std::vector<std::pair<std::string, std::pair<bool, NsbValue>>> savedParams;
    bool isChapterOrScene = false;
  };

  struct SelectFrame {
    std::size_t selectPc = 0;
    std::string endLabel;
  };

  struct Thread {
    std::string name;
    std::shared_ptr<NsbScript> script;
    std::size_t pc = 0;
    std::vector<NsbValue> stack;
    std::vector<CallFrame> callStack;
    std::vector<SelectFrame> selectStack;
    std::vector<std::string> whileEndStack;
    bool running = true;
    bool locked = false;
    bool deleted = false; // Deferred reclamation; never erase a running thread.
    bool waitingKey = false;
    bool waitingText = false;
    bool waitingChoice = false;
    bool waitingMovie = false;
    float waitTimer = 0.0f;
    float waitKeyTimeout = -1.0f;
    std::string waitEntity;
    bool waitEntityAnim = false;
  };

  std::shared_ptr<NsbScript> loadScript(std::string_view path);
  bool resolveFunction(const std::shared_ptr<NsbScript> &fromScript,
                       std::string_view funcName,
                       std::shared_ptr<NsbScript> &outScript,
                       std::size_t &outPc);
  bool resolveTarget(const std::shared_ptr<NsbScript> &fromScript,
                     std::string_view rawTarget, bool isScene,
                     std::shared_ptr<NsbScript> &outScript,
                     std::size_t &outPc);

  std::size_t stepThread(Thread &t, std::size_t budget);
  void execInstruction(Thread &t, const NsbInstruction &ins, bool &yielded);
  std::vector<NsbValue> popArgs(Thread &t, std::size_t count);
  NsbValue popOne(Thread &t);
  void assignVar(Thread &t, std::string_view varName, const NsbValue &val);
  NsbValue readVar(std::string_view varName) const;
  void syncSystemVars();
  std::vector<uint8_t> writeRuntimeSnapshot() const;
  bool restoreRuntimeSnapshot(const std::vector<uint8_t> &snapshot);
  static std::uint32_t parseColorName(std::string_view colorStr);

  // N2 scripts address sound entities either by creation name ("BGM04") or by
  // entity-path queries ("@BGM04", "@BGM*"). Match both, including wildcards.
  std::vector<std::string> matchingSoundEntities(std::string_view query) const;
  // The NSB compiler sometimes emits a bare identifier where a variable value
  // is expected (e.g. the "ループ" loop argument). Resolve such names against
  // the VM's global variable slots before interpreting them as literals.
  NsbValue resolveVariableName(const NsbValue &value) const;

  NsbLoaderFn loader_;
  ImageSizeFn imageSizeFn_;
  MediaTimeQueryFn mediaTimeQuery_;
  SaveExistsFn saveExistsQuery_;
  std::optional<PendingSaveAction> pendingSaveAction_;
  SystemIniConfig config_;

  std::unordered_map<std::string, std::shared_ptr<NsbScript>> scripts_;
  std::unordered_map<std::string, NsbValue> vars_;
  std::unordered_map<std::string, std::vector<std::string>> arrayKeys_;
  std::unordered_map<std::string, PreDialogueBlock> preBlocks_;
  std::unordered_map<std::string, std::string> soundEntities_;
  std::unordered_map<std::string, std::string> soundTypes_;
  // Per-sound-entity state. The engine owns a single playback channel per
  // kind, but scripts create dozens of muted entities and start/loop/volume
  // them individually. A negative volume means "use the configured master".
  std::unordered_map<std::string, float> soundVolumes_;
  std::unordered_map<std::string, bool> soundLoops_;

  // Deque keeps Thread references stable when CreateProcess spawns during an
  // NSB instruction. Deletion is deferred until after the scheduler tick.
  std::deque<Thread> threads_;
  std::vector<std::string> activeChoices_;
  std::string triggeredChoice_;
  ActiveMovieRequest activeMovie_;
  PhoneState phone_;

  SceneGraph sceneGraph_;
  AudioSystem audioSystem_;
  DialogueSystem dialogueSystem_;
  VariableStore legacyVars_;

  double elapsedSeconds_ = 0.0;
  std::uint64_t totalExecuted_ = 0;
  std::uint32_t rngState_ = 0x5EED1234u;
  int mouseX_ = 0;
  int mouseY_ = 0;
  bool finished_ = false;
};

} // namespace N2System
