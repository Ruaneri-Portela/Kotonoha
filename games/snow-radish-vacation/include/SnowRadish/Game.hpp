#pragma once
#include "SnowRadish/Pack.hpp"
#include "SnowRadish/Vm.hpp"

#include <deque>
#include <functional>
#include <map>
#include <string>

namespace SnowRadish {

inline constexpr const char* titleSuffix(bool cleared, bool hasSaves) {
    return cleared ? (hasSaves ? "a" : "") : (hasSaves ? "c" : "b");
}

// Converts the Shift-JIS strings stored in scripts to UTF-8.
std::string sjisToUtf8(const std::string& s);

struct Message {
    std::string name;  // speaker (UTF-8), empty for narration
    std::string text;  // UTF-8
    int actor = 0;
    std::string voice; // voice id such as V0121234, may be empty
};

// Headless game logic: owns the script pack, the script VM and the visual novel
// state that the presentation layer draws (message, background, sounds, ...).
class Game : public Host {
public:
    explicit Game(std::vector<uint8_t> scriptPack);

    bool init();
    // Switches the running script (as the goto native does).
    bool gotoScript(const std::string& name) { return loadScript(name, true); }
    // Runs the VM until it blocks on user input or maxSteps ran out.
    void pump(size_t maxSteps = 200000);
    // Advances past the current message. Returns false if nothing was pending.
    bool click();

    bool waitingForClick() const { return waiting_; }
    const Message& message() const { return current_; }
    bool hasMessage() const { return waiting_ && hasMsg_; }
    bool finished() const { return vm_.ended(); }
    Vm& vm() { return vm_; }
    int voiceActor() const { return voiceActor_; }
    std::function<void(bool)> cleared;
    // Seed/restore the system flag without treating replay as a new ending.
    void setClearFlag(bool flag) { clearedFlag_ = flag; vm_.setVar(0x50cb, flag ? 1 : 0); }

    const std::string& currentScript() const { return scriptName_; }
    const std::string& background() const { return background_; }

    struct Sprite {
        std::string name; // e.g. "stkgr1c" (file <name>.CHD)
        int x = 0, y = 0; // offset of the full-screen sprite canvas
        int id = 0;       // slot: a new sprite with the same id replaces the old one
        int tint[3] = {0x100, 0x100, 0x100}; // RGB gain, 0x100 = neutral
    };
    // Visible character sprites in draw order (later entries on top).
    const std::vector<Sprite>& sprites() const { return sprites_; }
    // Set by the colour-fill native: the screen is a solid colour instead of a picture.
    bool filled() const { return filled_; }
    uint32_t fillColor() const { return fillColor_; } // 0xRRGGBB
    // Bumps whenever background/sprites change so the presenter can reload.
    uint32_t sceneVersion() const { return sceneVersion_; }

    // True while the script waits for the voice, SE or a screen transition to finish.
    bool waitingForAudio() const { return audioWait_ != AudioWait::None; }
    // Click during a skippable audio wait. Returns true if a wait was cancelled.
    bool skipAudioWait();

    bool choosing() const { return choosing_; }
    bool mapChoosing() const { return choosing_ && map_; }
    int destination(size_t i) const { return i < choiceValue_.size() ? choiceValue_[i] : 0; }
    const std::vector<int>& mapArgs() const { return mapArgs_; }
    const std::vector<std::string>& choices() const { return choiceText_; }
    // Picks option i; stores its value into the script variable and resumes.
    bool choose(size_t i);

    // Sound requests raised by the script. kind: "se", "sestop", "voice", "voicestop",
    // "bgm", "bgmstop"; name is the pack entry base name (se049, V0310012, sr001);
    // loop is the script's loop flag (1 = repeat until stopped).
    std::function<void(const std::string& kind, const std::string& name, bool loop)> sound;
    // Queried by the wait natives; return whether the voice / SE channel still plays.
    std::function<bool()> voicePlaying, sePlaying;
    // Native 64: original effect, pattern and speed selector. The presenter owns
    // its 15 ms scheduler; the script waits while transitionPlaying() is true.
    std::function<void(int type, int pattern, int speed)> transition;
    std::function<bool()> transitionPlaying;
    // Native 213 reached: the original title menu runs here (the host shows it before the
    // story starts, so the script just continues with "start").
    std::function<void()> title;

    // Optional trace of every native call (id, rendered arguments).
    std::function<void(const std::string&)> trace;

    // Host
    void native(Vm& vm, int id, const std::vector<Arg>& args) override;
    ObjectPtr loadByName(const std::string& name) override;
    ObjectPtr loadById(uint32_t id12) override;
    void log(const std::string& s) override {
        if (trace) trace("! " + s);
    }

private:
    bool loadScript(const std::string& name, bool asMain);
    void showNext();
    bool popCall();

    std::vector<uint8_t> pack_;
    std::vector<PackEntry> entries_;
    std::map<std::string, ObjectPtr> byName_;
    std::map<uint32_t, ObjectPtr> byId_;
    Vm vm_;
    ObjectPtr sub_, lib_, main_;
    std::string scriptName_;

    struct CallFrame {
        std::string name;
        uint32_t pc;
    };
    std::vector<CallFrame> callStack_;

    std::deque<Message> queue_;
    Message current_;
    bool hasMsg_ = false;
    bool waiting_ = false;
    bool clearedFlag_ = false;
    int speakerMode_ = 1, voiceActor_ = 0;
    std::string speaker_;
    std::string background_;
    std::vector<Sprite> sprites_;
    bool filled_ = false;
    uint32_t fillColor_ = 0;
    uint32_t sceneVersion_ = 0;
    enum class AudioWait { None, Voice, VoiceSkippable, SeSkippable, Transition };
    AudioWait audioWait_ = AudioWait::None;
    bool choosing_ = false, map_ = false;
    std::vector<int> mapArgs_;
    std::vector<std::string> choiceText_;
    std::vector<int> choiceValue_;
    uint32_t choiceVar_ = 0;
};

} // namespace SnowRadish
