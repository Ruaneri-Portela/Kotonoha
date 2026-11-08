#pragma once

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/script/VNScript.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kirikiri {

// KAG (Kirikiri Adventure Game) script parser and runtime. A KAG script is a
// sequence of tags, e.g. `[chara_show layer="0" file="hero.png"]`.
// This is a minimal but functional subset: text display, backgrounds,
// characters, choices, jumps, labels, waits and basic variable operations.
class KagScript {
public:
  enum class TagType { Text, Command };

  struct Tag {
    TagType type = TagType::Command;
    // Raw tag name without brackets, e.g. "chara_show" or "cm"
    std::string name;
    // Attribute key/value pairs
    std::map<std::string, std::string> attrs;
    // For plain text outside tags
    std::string text;
    int line = 0;
    int column = 0;
  };

  // A parsed script: ordered tags plus named labels (jump targets).
  struct Script {
    std::vector<Tag> tags;
    std::map<std::string, std::size_t> labels; // label name -> tag index
  };

  // Parse a KAG script from UTF-8 text. Returns the script; on unrecoverable
  // syntax error throws std::runtime_error with the offending line.
  static Script parse(const std::string &text, const std::string &sourceName);

  // Execute a parsed script against a Kotonoha engine. The callbacks let the
  // host (KirikiriRuntime) perform presentation (bgm, sprites, textbox).
  using ShowTextCallback = std::function<void(std::string speaker, std::string text)>;
  using ShowChoiceCallback = std::function<void(std::string question,
                                                std::vector<std::string> options,
                                                std::function<void(int)> onChoose)>;
  using JumpCallback = std::function<void(std::string label)>;
  using WaitCallback = std::function<void(int milliseconds, std::function<void()> onDone)>;
  using ShowBgCallback = std::function<void(std::string file, int transitionMs)>;
  using ShowCharCallback = std::function<void(std::string layer, std::string file,
                                              int x, int y, int transitionMs)>;
  using HideCharCallback = std::function<void(std::string layer, int transitionMs)>;
  using PlayBgmCallback = std::function<void(std::string file, bool loop)>;
  using PlaySeCallback = std::function<void(std::string file)>;
  using StopBgmCallback = std::function<void()>;
  using SetVarCallback = std::function<void(std::string name, std::string value)>;
  using GetVarCallback = std::function<std::string(std::string name)>;

  struct Callbacks {
    ShowTextCallback showText;
    ShowChoiceCallback showChoice;
    JumpCallback jump;
    WaitCallback wait;
    ShowBgCallback showBg;
    ShowCharCallback showChar;
    HideCharCallback hideChar;
    PlayBgmCallback playBgm;
    PlaySeCallback playSe;
    StopBgmCallback stopBgm;
    SetVarCallback setVar;
    GetVarCallback getVar;
  };

  // Runs the script from the first tag until a blocking operation (choice,
  // wait, or end of script) is reached. `onBlocked` is invoked when the
  // script yields control to the host.
  class Runner {
  public:
    Runner(Script script, const Callbacks &cb);
    // Returns true while the runner is blocked waiting for the host
    // (choice/wait) and false when it has finished or reached the end.
    bool isBlocked() const { return blocked_; }
    bool isFinished() const { return finished_; }
    void setBlocked(bool value) { blocked_ = value; }
    // Pump the runner: continue executing non-blocking tags. The host calls
    // resumeChoice()/resumeWait() to unblock after a blocking op completes.
    void run();
    void resumeChoice(int index);
    void resumeWait();

  private:
    Script script_;        // owned copy; the source Script may go out of scope
    const Callbacks &cb_;
    std::size_t pc_ = 0;
    bool blocked_ = false;
    bool finished_ = false;
    std::function<void()> pendingResume_;
    std::map<std::string, std::string> vars_;

    bool executeTag(const Tag &tag); // returns true if tag blocks player
    bool executeTagBlocking(const Tag &tag); // returns true if tag blocks
    std::size_t findLabel(const std::string &name);
    bool getAttrBool(const Tag &tag, const std::string &key, bool def) const;
    int getAttrInt(const Tag &tag, const std::string &key, int def) const;
    std::string getAttr(const Tag &tag, const std::string &key,
                        const std::string &def) const;
  };
};

} // namespace Kirikiri
