#include <cstdio>
#include "SnowRadish/Game.hpp"

#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cctype>

namespace SnowRadish {

namespace {
const char* const kPlaceNames[] = {
    "ヒの部屋", "浅葱と巴の部屋", "踊り場", "玄関", "スノーラディッシュ",
    "店の外", "寝室", "リビング", "露天風呂", "倉庫"
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
} // namespace

Game::Game(std::vector<uint8_t> scriptPack) : pack_(std::move(scriptPack)), vm_(*this) {
    entries_ = readDirectory(pack_);
}

ObjectPtr Game::loadByName(const std::string& rawName) {
    std::string name = lower(rawName);
    if (name.find('.') == std::string::npos) name += ".obj";
    auto it = byName_.find(name);
    if (it != byName_.end()) return it->second;
    for (const auto& e : entries_) {
        if (lower(e.name) != name) continue;
        auto obj = std::make_shared<ScriptObject>();
        if (!obj->parse(e.name, extractEntry(pack_, e))) return nullptr;
        byName_[name] = obj;
        byId_.emplace(obj->id & 0xfff, obj);
        return obj;
    }
    return nullptr;
}

std::map<int, std::vector<std::string>> Game::texts() {
    std::map<int, std::vector<std::string>> out;
    for (const auto& e : entries_) {
        const ObjectPtr obj = loadByName(e.name);
        if (!obj) continue;
        auto& route = out[int(obj->id & 0xfff)];
        // Only strings with Shift-JIS characters are text; ASCII ones are
        // file names, voice ids and numbers.
        for (const std::string& raw : obj->strings)
            if (std::any_of(raw.begin(), raw.end(), [](char c) { return uint8_t(c) >= 0x80; }))
                route.push_back(Kotonoha::ShiftJisToUtf8(raw));
        if (route.empty()) out.erase(int(obj->id & 0xfff));
    }
    out[kMapRoute].assign(std::begin(kPlaceNames), std::end(kPlaceNames));
    return out;
}

ObjectPtr Game::loadById(uint32_t id12) {
    auto it = byId_.find(id12);
    if (it != byId_.end()) return it->second;
    for (const auto& e : entries_) loadByName(e.name);
    it = byId_.find(id12);
    return it != byId_.end() ? it->second : nullptr;
}

bool Game::loadScript(const std::string& name, bool asMain) {
    ObjectPtr obj = loadByName(name);
    if (!obj) {
        obj = loadByName("start.obj");
        if (!obj) return false;
    }
    if (asMain) {
        if (main_) vm_.unregisterObject(main_);
        main_ = obj;
        vm_.registerObject(main_);
    }
    scriptName_ = obj->name;
    vm_.jumpTo(obj, 0);
    queue_.clear();
    return true;
}

bool Game::init() {
    sub_ = loadByName("sub.obj");
    lib_ = loadByName("lib.obj");
    if (!sub_ || !lib_) return false;
    vm_.registerObject(sub_);
    vm_.registerObject(lib_);
    if (ObjectPtr m = loadByName("main.obj")) vm_.registerObject(m);
    ObjectPtr start = loadByName("start.obj");
    if (!start) return false;
    main_ = start;
    vm_.registerObject(main_);
    scriptName_ = start->name;
    vm_.start(start, 0);
    return true;
}

std::vector<std::string> Game::debugScripts() const {
    std::vector<std::string> result;
    for (const auto& entry : entries_) result.push_back(entry.name);
    return result;
}

bool Game::debugJump(const std::string& name) {
    const auto names = debugScripts();
    if (std::find(names.begin(), names.end(), name) == names.end()) return false;
    const auto object = loadByName(name);
    if (!object) return false;
    if (main_) vm_.unregisterObject(main_);
    main_ = object;
    vm_.registerObject(main_);
    vm_.start(main_);
    scriptName_ = main_->name;
    queue_.clear();
    callStack_.clear();
    current_ = {};
    hasMsg_ = waiting_ = choosing_ = false;
    audioWait_ = AudioWait::None;
    choiceText_.clear();
    choiceRoute_.clear();
    choiceValue_.clear();
    return true;
}

bool Game::popCall() {
    if (callStack_.empty()) return false;
    CallFrame f = callStack_.back();
    callStack_.pop_back();
    ObjectPtr obj = loadByName(f.name);
    if (!obj) return false;
    if (main_) vm_.unregisterObject(main_);
    main_ = obj;
    vm_.registerObject(main_);
    scriptName_ = obj->name;
    vm_.jumpTo(obj, f.pc);
    return true;
}

void Game::pump(size_t maxSteps) {
    if (waiting_) return;
    if (audioWait_ != AudioWait::None) {
        const bool playing = audioWait_ == AudioWait::SeSkippable  ? (sePlaying && sePlaying())
                             : audioWait_ == AudioWait::Transition ? (transitionPlaying && transitionPlaying())
                                                                   : (voicePlaying && voicePlaying());
        if (playing) return;
        audioWait_ = AudioWait::None;
        vm_.unblock();
    }
    vm_.run(maxSteps);
    // A called script that runs off its end returns to its caller.
    while (vm_.ended() && !waiting_ && !choosing_ && popCall()) vm_.run(maxSteps);
    const bool flag = vm_.var(0x50cb) != 0;
    if (flag != clearedFlag_) {
        clearedFlag_ = flag;
        if (cleared) cleared(flag);
    }
}

void Game::showNext() {
    if (queue_.empty()) {
        waiting_ = false;
        hasMsg_ = false;
        vm_.unblock();
        return;
    }
    current_ = std::move(queue_.front());
    queue_.pop_front();
    hasMsg_ = true;
    waiting_ = true;
    voiceActor_ = current_.actor;
    if (sound && !current_.voice.empty()) sound("voice", current_.voice, false);
}

bool Game::choose(size_t i) {
    if (!choosing_ || i >= choiceText_.size()) return false;
    vm_.setVar(choiceVar_, uint32_t(choiceValue_[i]));
    // The uniform select closes with its own fade (FUN_00413c69(1, 0, 3)) while it is
    // still on screen, and the script resumes once that fade is over.
    if (kind_ == ChoiceKind::Uniform && transition) {
        transition(1, 0, 3);
        if (transitionPlaying && transitionPlaying()) audioWait_ = AudioWait::Transition;
    }
    choosing_ = false;
    kind_ = ChoiceKind::Text;
    choiceText_.clear();
    choiceRoute_.clear();
    if (audioWait_ == AudioWait::None) vm_.unblock();
    return true;
}

bool Game::skipAudioWait() {
    if (audioWait_ == AudioWait::None || audioWait_ == AudioWait::Voice) return false;
    audioWait_ = AudioWait::None;
    vm_.unblock();
    return true;
}

bool Game::click() {
    if (!waiting_) return false;
    if (sound && !current_.voice.empty()) sound("voicestop", "", false);
    showNext();
    return true;
}

void Game::native(Vm& vm, int id, const std::vector<Arg>& args) {
    if (trace) {
        std::string t = "sys " + std::to_string(id) + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) t += ", ";
            const Arg& a = args[i];
            if (a.tag() == 2) t += "\"" + Kotonoha::ShiftJisToUtf8(vm.text(a)) + "\"";
            else if (a.tag() == 1) t += "v" + std::to_string(a.value()) + "=" + std::to_string(vm.var(a.value()));
            else t += std::to_string(a.value());
        }
        trace(t + ")");
    }

    std::vector<int> ints;
    for (const Arg& a : args)
        if (a.tag() == 0) ints.push_back(int(int16_t(a.value())));

    auto firstString = [&]() {
        for (const Arg& a : args)
            if (a.tag() == 2) return vm.text(a);
        return std::string();
    };

    switch (id) {
    case 1:
    case 7: {
        for (const auto& l : vm.lines()) {
            Message m;
            m.actor = speakerMode_;
            m.text = Kotonoha::ShiftJisToUtf8(vm.textAt(l.object, l.index));
            m.route = int(l.object);
            m.voice = vm.textAt(l.object, l.index + 1);
            if (speakerMode_ != 1) {
                m.name = Kotonoha::ShiftJisToUtf8(speaker_);
                m.nameRoute = speakerRoute_;
            }
            queue_.push_back(std::move(m));
        }
        vm.lines().clear();
        speakerMode_ = 1;
        speaker_.clear();
        speakerRoute_ = kNoRoute;
        if (!queue_.empty()) {
            vm.block();
            showNext();
        }
        break;
    }
    case 5: {
        for (const Arg& a : args) {
            if (a.tag() == 0) speakerMode_ = a.value();
            if (a.tag() == 2) {
                speaker_ = vm.text(a);
                speakerRoute_ = int(a.object());
            }
        }
        break;
    }
    case 16: {
        loadScript(firstString(), true);
        break;
    }
    case 28: {
        callStack_.push_back({scriptName_, vm.returnPc()});
        loadScript(firstString(), true);
        break;
    }
    case 29: {
        popCall();
        break;
    }
    case 219: {
        mapArgs_ = ints;
        kind_ = ChoiceKind::Map;
        initialFocus_ = -1;
        hasMsg_ = false;
        // Location map: a bitmask of destinations; the pick goes to the var ref.
        int ints = 0;
        uint32_t mask = 0;
        choiceVar_ = 0;
        for (const Arg& a : args) {
            if (a.tag() == 0 && ints++ == 6) mask = a.value();
            if (a.tag() == 1) choiceVar_ = a.value();
        }
        choiceText_.clear();
        choiceRoute_.clear();
        choiceValue_.clear();
        for (int i = 1; i <= 10; ++i)
            if (mask & (1u << (i - 1))) {
                choiceText_.push_back(kPlaceNames[i - 1]);
                choiceRoute_.push_back(kMapRoute);
                choiceValue_.push_back(i);
            }
        if (!choiceText_.empty()) {
            background_ = "mapbase";
            sprites_.clear();
            filled_ = false;
            ++sceneVersion_;
            choosing_ = true;
            vm.block();
        }
        break;
    }
    case 48: // background picture; drops every sprite
        background_ = firstString();
        sprites_.clear();
        filled_ = false;
        ++sceneVersion_;
        break;
    case 69: { // fill the screen with a colour (r, g, b); drops picture and sprites
        int c[3] = {0, 0, 0};
        int n = 0;
        for (const Arg& a : args)
            if (a.tag() == 0 && n < 3) c[n++] = std::clamp(int(a.value()), 0, 255);
        background_.clear();
        sprites_.clear();
        filled_ = true;
        fillColor_ = uint32_t(c[0] << 16 | c[1] << 8 | c[2]);
        ++sceneVersion_;
        break;
    }
    case 49: { // sprite (name, x, y, id, tint): replaces the same id, then goes on top
        Sprite s;
        s.name = firstString();
        int ints = 0;
        for (const Arg& a : args) {
            if (a.tag() != 0) continue;
            const int v = int16_t(a.value());
            if (ints == 0) s.x = v;
            else if (ints == 1) s.y = v;
            else if (ints == 2) s.id = v;
            else if (ints >= 3 && ints < 6) s.tint[ints - 3] = v;
            ++ints;
        }
        if (s.name.empty()) break;
        sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [&](const Sprite& o) { return o.id == s.id; }),
                       sprites_.end());
        sprites_.push_back(std::move(s));
        ++sceneVersion_;
        break;
    }
    case 64: { // transition (type, pattern, speed); 0 just shows the new frame
        const int type = ints.empty() ? 0 : ints[0];
        const int pattern = ints.size() > 1 ? ints[1] : 0;
        if (type == 11) {
            const int index = (pattern & 0xfff) < 96 ? pattern & 0xfff : 0;
            Sprite sp;
            sp.name = "STKGR" + std::to_string(index / 12 + 1) + char('a' + index % 12);
            sp.id = 1;
            if ((pattern & 0x3000) == 0x1000) sp.tint[2] = 200;
            if ((pattern & 0x3000) == 0x2000) sp.tint[0] = sp.tint[1] = 200;
            sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [](const Sprite& s) { return s.id == 1; }), sprites_.end());
            sprites_.push_back(std::move(sp));
            ++sceneVersion_;
        }
        const int speed = ints.size() > 2 && ints[2] >= 0 && ints[2] < 5 ? ints[2] : 0;
        if (type != 0 && transition) {
            transition(type, pattern, ints.size() > 2 && ints[2] == 5 ? 5 : speed);
            if (transitionPlaying && transitionPlaying()) {
                audioWait_ = AudioWait::Transition;
                vm.block();
            }
        }
        break;
    }
    case 96: // SE (name, loop)
    case 97: // SE with fade-in (name, loop, time)
        if (sound) sound("se", firstString(), !ints.empty() && ints[0] != 0);
        break;
    case 98:  // SE fade-out
    case 99:  // SE stop
    case 100: // SE fade-out (time)
        if (sound) sound("sestop", "", false);
        break;
    case 101: // wait for the SE, click skips
        if (sePlaying && sePlaying()) {
            audioWait_ = AudioWait::SeSkippable;
            vm.block();
        }
        break;
    case 102: // var = SE playing
        for (const Arg& a : args)
            if (a.tag() == 1) vm.setVar(a.value(), sePlaying && sePlaying() ? 1u : 0u);
        break;
    case 128: { // voice (character, loop, name, alt name): the alt (second) name wins when set
        std::vector<std::string> names;
        for (const Arg& a : args)
            if (a.tag() == 2) names.push_back(vm.text(a));
        std::string v = names.size() > 1 && !names[1].empty() ? names[1] : names.empty() ? "" : names[0];
        voiceActor_ = ints.empty() ? 0 : ints[0];
        if (sound && !v.empty()) sound("voice", v, ints.size() > 1 && ints[1] != 0);
        break;
    }
    case 131: // wait for the voice to end
    case 133: // same, click skips
        if (voicePlaying && voicePlaying()) {
            audioWait_ = id == 131 ? AudioWait::Voice : AudioWait::VoiceSkippable;
            vm.block();
        }
        break;
    case 132: // voice stop/fade
        if (sound) sound("voicestop", "", false);
        break;
    case 112: // BGM (number, loop); names come from the sr000..sr010 table
        if (sound && !ints.empty()) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "sr%03u", unsigned(ints[0]));
            sound("bgm", buf, ints.size() > 1 && ints[1] != 0);
        }
        break;
    case 115: // BGM stop
    case 116: // BGM fade-out (time)
        if (sound) sound("bgmstop", "", false);
        break;
    case 213: // title menu (mode, var): 1 start, 2 continue, 4 exit. The host's title
              // screen already ran, so the script always continues with "start".
              // Native 221 (the Overflow logo) is also played by the host.
        for (const Arg& a : args)
            if (a.tag() == 1) vm.setVar(a.value(), 1);
        if (title) title();
        break;
    case 220: { // uniform select (mode, var): FUN_004027dd, returns 1..3
        choiceVar_ = 0;
        for (const Arg& a : args)
            if (a.tag() == 1) choiceVar_ = a.value();
        const int current = int(vm.var(choiceVar_));
        if (transition) transition(1, 0, 3); // opens with a type 1 fade at speed 3
        kind_ = ChoiceKind::Uniform;
        initialFocus_ = current >= 1 && current <= 3 ? current - 1 : 0;
        choiceText_ = {"Uniform 1", "Uniform 2", "Uniform 3"};
        choiceRoute_.assign(3, kNoRoute);
        choiceValue_ = {1, 2, 3};
        hasMsg_ = false;
        choosing_ = true;
        ++sceneVersion_;
        vm.block();
        break;
    }
    case 9:
    case 11:
    case 240: {
        kind_ = ChoiceKind::Text;
        initialFocus_ = -1;
        choiceText_.clear();
        choiceRoute_.clear();
        choiceValue_.clear();
        choiceVar_ = 0;
        for (const Arg& a : args)
            if (a.tag() == 1) choiceVar_ = a.value();
        for (const auto& l : vm.lines()) {
            choiceText_.push_back(Kotonoha::ShiftJisToUtf8(vm.textAt(l.object, l.index)));
            choiceRoute_.push_back(int(l.object));
            choiceValue_.push_back(std::atoi(vm.textAt(l.object, l.index + 1).c_str()));
        }
        vm.lines().clear();
        if (!choiceText_.empty()) {
            choosing_ = true;
            vm.block();
        }
        break;
    }
    default: break;
    }
}

} // namespace SnowRadish
