#include <Kirikiri/ScnRunner.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Kirikiri {

namespace {

constexpr double kPi = 3.14159265358979323846;

double evalEasing(double t, const std::string &accel) noexcept {
  t = std::clamp(t, 0.0, 1.0);
  if (accel == "accel")
    return t * t;
  if (accel == "decel")
    return 1.0 - (1.0 - t) * (1.0 - t);
  if (accel == "acdec")
    return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) * 0.5;
  return t;
}

double parseRelativeDouble(const Kotonoha::Providers::PsbValue &v,
                           double current, bool *isRel = nullptr) {
  if (isRel)
    *isRel = false;
  if (v.isString()) {
    const std::string &s = v.asString();
    if (!s.empty() && s[0] == '@') {
      if (isRel)
        *isRel = true;
      if (s.size() == 1)
        return current;
      return current + std::strtod(s.c_str() + 1, nullptr);
    }
    return v.toDouble(current);
  }
  return v.toDouble(current);
}

int defaultPosX(const std::string &posName) noexcept {
  if (posName.find("左") != std::string::npos)
    return -250;
  if (posName.find("右") != std::string::npos)
    return 250;
  return 0;
}

} // namespace

std::string ScnRunner::stripFormatting(std::string text) {
  std::size_t fpos = 0;
  while ((fpos = text.find("%f", fpos)) != std::string::npos) {
    const auto semi = text.find(';', fpos);
    if (semi != std::string::npos && semi - fpos < 64)
      text.erase(fpos, semi - fpos + 1);
    else
      fpos += 2;
  }
  if (text.size() >= 6 && std::memcmp(text.data(), "\xE3\x80\x8C", 3) == 0 &&
      std::memcmp(text.data() + text.size() - 3, "\xE3\x80\x8D", 3) == 0) {
    text = text.substr(3, text.size() - 6);
  }
  return text;
}

std::pair<std::string, std::string>
ScnRunner::resolveSpeaker(const std::string &primary,
                          const std::string &combined,
                          const std::string &fallbackDisplay) {
  const std::string key = !combined.empty() ? combined : primary;
  static const std::map<std::string, std::pair<std::string, std::string>> kMap =
      {
          {"ショコラ", {"Chocola", "VNX+/cn_name_chocola.png"}},
          {"バニラ", {"Vanilla", "VNX+/cn_name_vanilla.png"}},
          {"時雨", {"Shigure", "VNX+/cn_name_shigure.png"}},
          {"アズキ", {"Azuki", "VNX+/cn_name_azuki.png"}},
          {"メイプル", {"Maple", "VNX+/cn_name_maple.png"}},
          {"シナモン", {"Cinnamon", "VNX+/cn_name_cinnamon.png"}},
          {"ココナツ", {"Coconut", "VNX+/cn_name_coconut.png"}},
          {"全員", {"Todos", "VNX+/cn_name_all.png"}},
          {"ショコラ＆バニラ",
           {"Chocola & Vanilla", "VNX+/cn_name_chocola_vanilla.png"}},
          {"ショコラ＆時雨",
           {"Chocola & Shigure", "VNX+/cn_name_chocola_shigure.png"}},
          {"メイプル＆アズキ",
           {"Maple & Azuki", "VNX+/cn_name_maple_azuki.png"}},
          {"アズキ＆シナモン",
           {"Azuki & Cinnamon", "VNX+/cn_name_azuki_cinnamon.png"}},
          {"ココナツ＆アズキ",
           {"Coconut & Azuki", "VNX+/cn_name_coconut_azuki.png"}},
          {"ココナツ＆メイプル",
           {"Coconut & Maple", "VNX+/cn_name_coconut_maple.png"}},
          {"ココナツ＆シナモン",
           {"Coconut & Cinnamon", "VNX+/cn_name_coconut_cinnamon.png"}},
          {"メイプル＆シナモン",
           {"Maple & Cinnamon", "VNX+/cn_name_maple_cinnamon.png"}},
          {"メイプル＆時雨",
           {"Maple & Shigure", "VNX+/cn_name_maple_shigure.png"}},
          {"時雨＆ココナツ",
           {"Shigure & Coconut", "VNX+/cn_name_shigure_coconut.png"}},
          {"バニラ＆シナモン",
           {"Vanilla & Cinnamon", "VNX+/cn_name_vanilla_cinnamon.png"}},
          {"ショコラ＆バニラ＆ココナツ",
           {"Chocola, Vanilla & Coconut",
            "VNX+/cn_name_chocola_vanilla_coconut.png"}},
          {"ショコラ＆バニラ＆時雨",
           {"Chocola, Vanilla & Shigure",
            "VNX+/cn_name_chocola_vanilla_shigure.png"}},
  };
  if (auto it = kMap.find(key); it != kMap.end())
    return it->second;
  if (!fallbackDisplay.empty())
    return {fallbackDisplay, {}};
  return {key, {}};
}

ScnRunner::ScnRunner(Kotonoha::Providers::PsbFile psb, Callbacks cb,
                     std::size_t languageIndex)
    : psb_(std::move(psb)), cb_(std::move(cb)), langIdx_(languageIndex) {
  stage_.name = "bg";
  stage_.className = "stage";
}

void ScnRunner::initDefaultEmoteVars(ScnCharacterState &ch) {
  for (const char *v :
       {"$face_eye_open", "$face_eye_LR", "$face_eye_UD", "$face_eyebrow",
        "$face_mouth", "$face_cheek", "$face_tears", "$head_LR", "$head_UD",
        "$head_slant", "$body_LR", "$body_UD", "$body_slant", "$move_LR",
        "$move_UD", "$act_sp", "$easing"}) {
    ch.variables.try_emplace(v, 0.0);
  }
}

double &ScnRunner::characterPropertyRef(ScnCharacterState &ch,
                                        const std::string &prop) {
  if (prop == "xpos")
    return ch.xpos;
  if (prop == "ypos")
    return ch.ypos;
  if (prop == "zpos")
    return ch.zpos;
  if (prop == "offsetx")
    return ch.offsetx;
  if (prop == "offsety")
    return ch.offsety;
  if (prop == "imagerotate")
    return ch.imagerotate;
  if (prop == "imagex")
    return ch.imagex;
  if (prop == "imagey")
    return ch.imagey;
  if (prop == "opacity")
    return ch.opacity;
  return ch.variables[prop];
}

double &ScnRunner::layerPropertyRef(ScnLayerState &layer,
                                    const std::string &prop) {
  if (prop == "xpos")
    return layer.xpos;
  if (prop == "ypos")
    return layer.ypos;
  if (prop == "zpos")
    return layer.zpos;
  return layer.opacity;
}

bool ScnRunner::jumpToLabel(const std::string &label) {
  const auto &scenes = psb_.root()["scenes"];
  if (!scenes.isList())
    return false;
  const std::string norm =
      !label.empty() && label.front() == '*' ? label : "*" + label;
  for (std::size_t i = 0; i < scenes.size(); ++i) {
    std::string sl = scenes[i]["label"].asString();
    if (!sl.empty() && sl.front() != '*')
      sl.insert(sl.begin(), '*');
    if (sl == norm || (label == "start" && i == 0)) {
      sceneIdx_ = i;
      pc_ = 0;
      blocked_ = false;
      finished_ = false;
      return true;
    }
  }
  return false;
}

void ScnRunner::run() {
  const auto &scenes = psb_.root()["scenes"];
  if (!scenes.isList() || sceneIdx_ >= scenes.size()) {
    finished_ = true;
    return;
  }
  const auto &scene = scenes[sceneIdx_];
  const auto &lines = scene["lines"];
  if (!lines.isList()) {
    finished_ = true;
    return;
  }

  std::size_t budget = 8192;
  while (pc_ < lines.size() && !blocked_ && !finished_ && budget-- > 0) {
    bool blocks = false;
    const auto &line = lines[pc_];
    ++pc_;
    executeLine(line, blocks);
    if (blocks) {
      blocked_ = true;
      return;
    }
  }

  if (pc_ >= lines.size() && !blocked_ && !finished_) {
    const auto &nexts = scene["nexts"];
    if (nexts.isList() && nexts.size() > 0 && nexts[0].isDict()) {
      const std::string storage = nexts[0]["storage"].asString();
      const std::string target = nexts[0]["target"].asString();
      const std::int64_t type = nexts[0]["type"].toInt(0);
      if (type == 1 || target == "*gameend_title") {
        finished_ = true;
        if (cb_.returnToTitle)
          cb_.returnToTitle();
        return;
      }
      if (!storage.empty() && cb_.kag.jump) {
        finished_ = true;
        cb_.kag.jump(target.empty() ? storage : storage + "#" + target);
        return;
      }
    }
    finished_ = true;
  }
}

void ScnRunner::resumeDialogue() {
  if (!blocked_)
    return;
  blocked_ = false;
  fireDelayTrigger("vlstop");
  run();
}

void ScnRunner::resumeWait() {
  if (!blocked_)
    return;
  blocked_ = false;
  run();
}

void ScnRunner::fireDelayTrigger(const std::string &trigger) {
  std::vector<Kotonoha::Providers::PsbValue> toRun;
  for (auto it = pendingDelays_.begin(); it != pendingDelays_.end();) {
    if (it->trigger == trigger) {
      toRun.push_back(std::move(it->command));
      it = pendingDelays_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto &cmd : toRun) {
    bool dummyBlocks = false;
    executeCommand(cmd, dummyBlocks);
  }
}

void ScnRunner::finishAllActions() {
  auto finishMap = [](auto &obj, auto propGetter) {
    for (auto &[prop, keyframes] : obj.activeActions) {
      if (!keyframes.empty()) {
        const auto &last = keyframes.back();
        if (last.handler == "MoveAction")
          propGetter(obj, prop) = last.targetValue;
      }
    }
    obj.activeActions.clear();
  };
  finishMap(stage_, [this](ScnLayerState &l, const std::string &p) -> double & {
    return layerPropertyRef(l, p);
  });
  for (auto &[_, ev] : events_) {
    finishMap(ev, [this](ScnLayerState &l, const std::string &p) -> double & {
      return layerPropertyRef(l, p);
    });
  }
  for (auto &[_, ch] : characters_) {
    finishMap(ch,
              [this](ScnCharacterState &c, const std::string &p) -> double & {
                return characterPropertyRef(c, p);
              });
  }
}

void ScnRunner::tickAnimations(int elapsedMs) {
  if (elapsedMs <= 0)
    return;

  // Advance timed delayrun triggers
  std::vector<Kotonoha::Providers::PsbValue> readyDelays;
  for (auto it = pendingDelays_.begin(); it != pendingDelays_.end();) {
    if (it->trigger == "ms") {
      it->remainingMs -= elapsedMs;
      if (it->remainingMs <= 0) {
        readyDelays.push_back(std::move(it->command));
        it = pendingDelays_.erase(it);
        continue;
      }
    }
    ++it;
  }
  for (const auto &cmd : readyDelays) {
    bool dummy = false;
    executeCommand(cmd, dummy);
  }

  auto stepQueue = [&](double &val, std::vector<ScnKeyframe> &queue) {
    int rem = elapsedMs;
    while (rem > 0 && !queue.empty()) {
      auto &kf = queue.front();
      if (!kf.hasStart) {
        kf.startValue = kf.startRelative ? val + kf.startValue : val;
        if (kf.targetRelative)
          kf.targetValue = val + kf.targetValue;
        kf.hasStart = true;
      }
      const int need = std::max(0, kf.durationMs - kf.elapsedMs);
      const int step = std::min(rem, need);
      kf.elapsedMs += step;
      rem -= step;
      const double t = kf.durationMs > 0
                           ? static_cast<double>(kf.elapsedMs) /
                                 static_cast<double>(kf.durationMs)
                           : 1.0;
      if (kf.handler == "MoveAction") {
        const double eased = evalEasing(t, kf.accel);
        val = kf.startValue + (kf.targetValue - kf.startValue) * eased;
      } else if (kf.handler == "SinAction") {
        const double cycleMs = kf.cycle > 0.0 ? kf.cycle : 500.0;
        const double phase =
            (static_cast<double>(kf.elapsedMs) / cycleMs) * 2.0 * kPi;
        val = kf.startValue + std::sin(phase) * kf.vibration * (1.0 - t);
      } else if (kf.handler == "RandomAction") {
        const double phase = static_cast<double>(kf.elapsedMs) * 0.05;
        val = kf.startValue + std::sin(phase) * kf.vibration * (1.0 - t);
      }
      if (kf.elapsedMs >= kf.durationMs) {
        if (kf.handler == "MoveAction")
          val = kf.targetValue;
        else if (kf.handler == "SinAction" || kf.handler == "RandomAction")
          val = kf.startValue;
        queue.erase(queue.begin());
      }
    }
  };

  for (auto it = stage_.activeActions.begin();
       it != stage_.activeActions.end();) {
    stepQueue(layerPropertyRef(stage_, it->first), it->second);
    if (it->second.empty())
      it = stage_.activeActions.erase(it);
    else
      ++it;
  }
  for (auto &[_, ev] : events_) {
    for (auto it = ev.activeActions.begin(); it != ev.activeActions.end();) {
      stepQueue(layerPropertyRef(ev, it->first), it->second);
      if (it->second.empty())
        it = ev.activeActions.erase(it);
      else
        ++it;
    }
  }
  for (auto &[_, ch] : characters_) {
    for (auto it = ch.activeActions.begin(); it != ch.activeActions.end();) {
      stepQueue(characterPropertyRef(ch, it->first), it->second);
      if (it->second.empty())
        it = ch.activeActions.erase(it);
      else
        ++it;
    }
  }
}

void ScnRunner::executeLine(const Kotonoha::Providers::PsbValue &line,
                            bool &blocks) {
  const auto &scene = psb_.root()["scenes"][sceneIdx_];
  const auto &texts = scene["texts"];

  // 1. Integer k (1 <= k <= texts.size()): Dialogue line
  if (line.isInt()) {
    const std::int64_t k = line.asInt();
    if (k >= 1 && texts.isList() &&
        static_cast<std::size_t>(k) <= texts.size()) {
      const auto &tx = texts[static_cast<std::size_t>(k - 1)];
      if (tx.isList() && tx.size() >= 3) {
        const std::string primary = tx[0].asString();
        const std::string combined = tx[1].asString();
        const auto &langs = tx[2];
        std::string enDisp;
        std::string rawText;
        if (langs.isList() && langs.size() > 0) {
          if (langs.size() > 1 && langs[1].isList() && langs[1].size() >= 1)
            enDisp = langs[1][0].asString();
          std::size_t chosen =
              langIdx_ < langs.size() ? langIdx_ : langs.size() - 1;
          if (!langs[chosen].isList() || langs[chosen].size() < 2 ||
              langs[chosen][1].asString().empty()) {
            if (langs.size() > 2 && langs[2].isList() && langs[2].size() >= 2 &&
                !langs[2][1].asString().empty())
              chosen = 2;
            else if (langs.size() > 1 && langs[1].isList() &&
                     langs[1].size() >= 2 && !langs[1][1].asString().empty())
              chosen = 1;
            else
              chosen = 0;
          }
          if (langs[chosen].isList() && langs[chosen].size() >= 2)
            rawText = langs[chosen][1].asString();
        }
        const std::string cleanText = stripFormatting(rawText);
        auto [speakerName, nameplate] =
            resolveSpeaker(primary, combined, enDisp);
        currentNameplate_ = nameplate;
        currentVoices_.clear();
        if (tx.size() >= 4 && tx[3].isList()) {
          for (const auto &v : tx[3].asList()) {
            const std::string vf = v["voice"].asString();
            if (!vf.empty())
              currentVoices_.push_back(vf);
          }
        }
        messageWindowVisible_ = true;
        if (cb_.setMessageWindowVisible)
          cb_.setMessageWindowVisible(true);
        // Fire immediate voice cue labels if scheduled
        fireDelayTrigger("ラベル0");
        if (cb_.showDialogue) {
          cb_.showDialogue(speakerName, cleanText, currentNameplate_,
                           currentVoices_);
        } else if (cb_.kag.showText) {
          cb_.kag.showText(speakerName, cleanText);
        }
        blocks = true;
      }
    }
    return;
  }

  // 2. String flagName ("cg_*", "bgm_*", "ac_*")
  if (line.isString()) {
    const std::string &flag = line.asString();
    if (!flag.empty()) {
      unlockedFlags_.insert(flag);
      if (cb_.unlockFlag)
        cb_.unlockFlag(flag);
      if (cb_.kag.setVar)
        cb_.kag.setVar(flag, "1");
    }
    return;
  }

  // 3. Savepoint Array [spId, snapshotDict, ...]
  if (line.isList() && line.size() >= 2 && line[0].isInt()) {
    lastSavepointId_ = line[0].asInt();
    if (line[1].isDict())
      lastSnapshot_ = line[1];
    return;
  }

  // 4. Command Array [cmdName, ...]
  if (line.isList() && line.size() >= 1 && line[0].isString()) {
    executeCommand(line, blocks);
  }
}

void ScnRunner::executeCommand(const Kotonoha::Providers::PsbValue &cmdList,
                               bool &blocks) {
  if (!cmdList.isList() || cmdList.size() == 0)
    return;
  const std::string cmd = cmdList[0].asString();

  if (cmd == "envupdate") {
    for (std::size_t i = 1; i < cmdList.size(); ++i) {
      if (!cmdList[i].isString())
        continue;
      const std::string sub = cmdList[i].asString();
      if ((sub == "update" || sub == "pretrans") && i + 1 < cmdList.size()) {
        executeEnvUpdate(cmdList[i + 1]);
        ++i;
      } else if (sub == "stop" && i + 1 < cmdList.size() &&
                 cmdList[i + 1].isList()) {
        for (const auto &target : cmdList[i + 1].asList()) {
          const std::string name = target.asString();
          events_.erase(name);
          characters_.erase(name);
          if (cb_.kag.hideChar)
            cb_.kag.hideChar(name, 0);
        }
        ++i;
      } else if (sub == "trans" && i + 1 < cmdList.size() &&
                 cmdList[i + 1].isDict()) {
        const auto &tr = cmdList[i + 1];
        lastTransition_.method = tr["method"].asString();
        lastTransition_.rule = tr["rule"].asString();
        lastTransition_.timeMs = static_cast<int>(tr["time"].toInt(0));
        ++i;
      }
    }
    return;
  }

  if (cmd == "delayrun") {
    if (cmdList.size() >= 3) {
      Kotonoha::Providers::PsbValue::List subCmd;
      for (std::size_t i = 2; i < cmdList.size(); ++i)
        subCmd.push_back(cmdList[i]);
      const auto subVal =
          Kotonoha::Providers::PsbValue::makeList(std::move(subCmd));
      if (cmdList[1].isInt()) {
        const int ms = static_cast<int>(cmdList[1].asInt());
        if (ms <= 0) {
          bool dummy = false;
          executeCommand(subVal, dummy);
        } else {
          pendingDelays_.push_back({"ms", ms, subVal});
        }
      } else {
        const std::string trig = cmdList[1].asString();
        if (trig == "ラベル0" || trig == "ラベル1" || trig == "ラベル2" ||
            trig == "ラベル3" || trig == "ラベル4" || trig == "ラベル5") {
          // Execute voice-cue expression/pose updates immediately so poses are
          // visible even when dummy audio is used.
          bool dummy = false;
          executeCommand(subVal, dummy);
        } else {
          pendingDelays_.push_back({trig, 0, subVal});
        }
      }
    }
    return;
  }

  if (cmd == "stopaction") {
    if (cmdList.size() >= 3 && cmdList[1].asString() == "delayrun") {
      const std::string target = cmdList[2].asString();
      pendingDelays_.erase(std::remove_if(pendingDelays_.begin(),
                                          pendingDelays_.end(),
                                          [&](const PendingDelay &d) {
                                            return d.trigger == target;
                                          }),
                           pendingDelays_.end());
    } else {
      finishAllActions();
    }
    return;
  }

  if (cmd == "wait") {
    int ms = 0;
    for (std::size_t i = 1; i + 1 < cmdList.size(); ++i) {
      if (cmdList[i].asString() == "time")
        ms = static_cast<int>(cmdList[i + 1].toInt(0));
    }
    if (ms > 0 && cb_.kag.wait && !skipSection_) {
      cb_.kag.wait(ms, [this] { resumeWait(); });
      blocks = true;
    }
    return;
  }

  if (cmd == "msgoff") {
    messageWindowVisible_ = false;
    if (cb_.setMessageWindowVisible)
      cb_.setMessageWindowVisible(false);
    return;
  }

  if (cmd == "meswinload") {
    messageWindowVisible_ = true;
    if (cb_.setMessageWindowVisible)
      cb_.setMessageWindowVisible(true);
    return;
  }

  if (cmd == "touchmode") {
    if (cmdList.size() >= 2) {
      touchMode_ = cmdList[1].asString() == "on";
      if (cb_.setTouchMode)
        cb_.setTouchMode(touchMode_);
    }
    return;
  }

  if (cmd == "beginskip") {
    skipSection_ = true;
    return;
  }

  if (cmd == "endskip") {
    skipSection_ = false;
    return;
  }
}

void ScnRunner::executeEnvUpdate(
    const Kotonoha::Providers::PsbValue &updateList) {
  if (!updateList.isList())
    return;
  for (const auto &item : updateList.asList()) {
    if (item.isList() && item.size() >= 2) {
      const std::string op = item[0].asString();
      if (op == "init") {
        stage_ = ScnLayerState{"bg", "stage", "", 1, 0, 0, 0, 255, 0, 0, {}};
        events_.clear();
        characters_.clear();
        seFile_.clear();
        if (cb_.kag.showBg)
          cb_.kag.showBg("", 0);
      } else if (op == "new" && item.size() >= 3) {
        const std::string objName = item[1].asString();
        const std::string cls = item[2].asString();
        if (cls == "stage" || objName == "bg") {
          stage_.name = objName;
          stage_.className = "stage";
        } else if (cls == "event" || cls == "emotion") {
          auto &ev = events_[objName];
          ev.name = objName;
          ev.className = cls;
        } else if (cls == "character") {
          auto &ch = characters_[objName];
          ch.name = objName;
          initDefaultEmoteVars(ch);
        }
      } else if (op == "del") {
        const std::string objName = item[1].asString();
        if (objName == "bg") {
          stage_.file.clear();
          if (cb_.kag.showBg)
            cb_.kag.showBg("", 0);
        } else if (objName == "bgm") {
          bgmFile_.clear();
          if (cb_.kag.stopBgm)
            cb_.kag.stopBgm();
        } else {
          events_.erase(objName);
          characters_.erase(objName);
          if (cb_.kag.hideChar)
            cb_.kag.hideChar(objName, 0);
        }
      }
    } else if (item.isDict()) {
      const std::string objName = item["name"].asString();
      const std::string cls = item["class"].asString();
      applyObjectDict(objName, cls, item);
    }
  }
}

void ScnRunner::applyObjectDict(const std::string &objName,
                                const std::string &cls,
                                const Kotonoha::Providers::PsbValue &dict) {
  if (objName.empty() || objName == "env")
    return;

  if (objName == "bgm" || cls == "bgm") {
    if (dict.has("replay") && dict["replay"].isDict()) {
      const std::string file = dict["replay"]["filename"].asString();
      bgmLoop_ = dict["replay"]["loop"].toInt(1) != 0;
      if (file == "bgm_" || file.empty()) {
        bgmFile_.clear();
        if (!file.empty() && cb_.kag.stopBgm)
          cb_.kag.stopBgm();
      } else {
        bgmFile_ = file;
        if (cb_.kag.playBgm)
          cb_.kag.playBgm(bgmFile_, bgmLoop_);
      }
    } else if (dict.has("filename")) {
      const std::string file = dict["filename"].asString();
      if (file == "bgm_" || file.empty()) {
        bgmFile_.clear();
        if (cb_.kag.stopBgm)
          cb_.kag.stopBgm();
      } else {
        bgmFile_ = file;
        if (cb_.kag.playBgm)
          cb_.kag.playBgm(bgmFile_, true);
      }
    }
    return;
  }

  if (objName == "se" || objName == "se1" || cls == "se") {
    if (dict["stop"].toInt(0) != 0) {
      seFile_.clear();
    } else if (dict.has("replay") && dict["replay"].isDict()) {
      const std::string file = dict["replay"]["filename"].asString();
      if (!file.empty()) {
        seFile_ = file;
        if (cb_.kag.playSe)
          cb_.kag.playSe(seFile_);
      }
    }
    return;
  }

  auto parseActions = [&](const Kotonoha::Providers::PsbValue &actionList,
                          auto propGetter, auto &activeActionsMap) {
    if (!actionList.isList())
      return;
    for (const auto &act : actionList.asList()) {
      if (!act.isList() || act.size() < 2 || !act[0].isString())
        continue;
      const std::string prop = act[0].asString();
      double &propRef = propGetter(prop);
      if (act[1].isNumber() || act[1].isString()) {
        propRef = parseRelativeDouble(act[1], propRef);
        activeActionsMap.erase(prop);
      } else if (act[1].isList()) {
        std::vector<ScnKeyframe> kfs;
        for (const auto &kfDict : act[1].asList()) {
          if (!kfDict.isDict())
            continue;
          ScnKeyframe kf;
          kf.handler = kfDict["handler"].asString();
          if (kf.handler.empty())
            kf.handler = "MoveAction";
          kf.durationMs = static_cast<int>(kfDict.has("time")
                                               ? kfDict["time"].toInt(0)
                                               : kfDict["waittime"].toInt(0));
          kf.accel = kfDict["accel"].toString();
          kf.vibration = kfDict["vibration"].toDouble(0.0);
          kf.cycle = kfDict["cycle"].toDouble(500.0);
          if (kfDict.has("start")) {
            bool rel = false;
            const double sv =
                parseRelativeDouble(kfDict["start"], propRef, &rel);
            propRef = sv;
            kf.startValue = sv;
            kf.hasStart = true;
          }
          if (kfDict.has("value")) {
            bool rel = false;
            const double tv =
                parseRelativeDouble(kfDict["value"], propRef, &rel);
            kf.targetValue = tv;
            kf.targetRelative = false;
          }
          if (kf.durationMs <= 0 && kf.handler == "MoveAction") {
            propRef = kf.targetValue;
          } else {
            kfs.push_back(std::move(kf));
          }
        }
        if (!kfs.empty())
          activeActionsMap[prop] = std::move(kfs);
      }
    }
  };

  if (objName == "bg" || cls == "stage") {
    stage_.name = objName;
    stage_.className = "stage";
    if (dict.has("showmode"))
      stage_.showmode = static_cast<int>(dict["showmode"].toInt(1));
    if (dict.has("redraw") && dict["redraw"].isDict()) {
      const auto &img = dict["redraw"]["imageFile"];
      const std::string file =
          img.isDict() ? img["file"].asString() : img.asString();
      if (!file.empty())
        stage_.file = file;
    } else if (dict.has("imageFile")) {
      const std::string file = dict["imageFile"].asString();
      if (!file.empty())
        stage_.file = file;
    }
    const auto &acts = dict.has("action") ? dict["action"] : dict["actionList"];
    parseActions(
        acts,
        [this](const std::string &p) -> double & {
          return layerPropertyRef(stage_, p);
        },
        stage_.activeActions);
    if (cb_.kag.showBg && !stage_.file.empty())
      cb_.kag.showBg(stage_.file, lastTransition_.timeMs);
    return;
  }

  if (cls == "event" || cls == "emotion" || objName == "ev" ||
      objName == "ev1" || objName == "eventf" || objName == "cutin" ||
      events_.find(objName) != events_.end()) {
    auto &ev = events_[objName];
    ev.name = objName;
    if (!cls.empty())
      ev.className = cls;
    else if (ev.className.empty())
      ev.className = "event";
    if (dict.has("showmode"))
      ev.showmode = static_cast<int>(dict["showmode"].toInt(1));
    if (dict.has("redraw") && dict["redraw"].isDict()) {
      const auto &img = dict["redraw"]["imageFile"];
      const std::string file =
          img.isDict() ? img["file"].asString() : img.asString();
      if (!file.empty())
        ev.file = file;
    } else if (dict.has("imageFile")) {
      const std::string file = dict["imageFile"].asString();
      if (!file.empty())
        ev.file = file;
    }
    const auto &acts = dict.has("action") ? dict["action"] : dict["actionList"];
    parseActions(
        acts,
        [this, &ev](const std::string &p) -> double & {
          return layerPropertyRef(ev, p);
        },
        ev.activeActions);
    if (ev.showmode == 2) {
      if (cb_.kag.hideChar)
        cb_.kag.hideChar(objName, 0);
    } else if (!ev.file.empty()) {
      if ((objName == "ev" || objName == "eventf") && cb_.kag.showBg)
        cb_.kag.showBg(ev.file, lastTransition_.timeMs);
      else if (cb_.kag.showChar)
        cb_.kag.showChar(objName, ev.file, static_cast<int>(ev.xpos),
                         static_cast<int>(ev.ypos), lastTransition_.timeMs);
    }
    return;
  }

  // Character object
  auto &ch = characters_[objName];
  ch.name = objName;
  initDefaultEmoteVars(ch);
  if (dict.has("showmode"))
    ch.showmode = static_cast<int>(dict["showmode"].toInt(1));
  if (dict.has("redraw") && dict["redraw"].isDict()) {
    const auto &rd = dict["redraw"];
    if (rd.has("posName") && rd["posName"].isString()) {
      ch.posName = rd["posName"].asString();
      if (ch.xpos == 0.0)
        ch.xpos = defaultPosX(ch.posName);
    }
    const auto &img = rd["imageFile"];
    if (img.isDict()) {
      const std::string file = img["file"].asString();
      if (!file.empty())
        ch.file = file;
      const auto &opts = img["options"];
      if (opts.isDict()) {
        if (opts.has("resolution"))
          ch.resolution = static_cast<int>(opts["resolution"].toInt(200));
        if (opts.has("timeline") && opts["timeline"].isDict())
          ch.timeline = opts["timeline"]["timeline"].asString();
        if (opts.has("stoptimeline"))
          ch.timeline.clear();
      }
    }
  } else if (dict.has("imageFile")) {
    ch.file = dict["imageFile"].asString();
    if (dict.has("posName"))
      ch.posName = dict["posName"].asString();
  }
  const auto &acts = dict.has("action") ? dict["action"] : dict["actionList"];
  parseActions(
      acts,
      [this, &ch](const std::string &p) -> double & {
        return characterPropertyRef(ch, p);
      },
      ch.activeActions);
  if (ch.showmode == 2) {
    if (cb_.kag.hideChar)
      cb_.kag.hideChar(objName, 0);
  } else if (!ch.file.empty() && cb_.kag.showChar) {
    cb_.kag.showChar(objName, ch.file, static_cast<int>(ch.xpos),
                     static_cast<int>(ch.ypos), lastTransition_.timeMs);
  }
}

void ScnRunner::applySavepointSnapshot(
    const Kotonoha::Providers::PsbValue &snapshot) {
  if (!snapshot.isDict())
    return;
  lastSnapshot_ = snapshot;
  stage_ = ScnLayerState{"bg", "stage", "", 1, 0, 0, 0, 255, 0, 0, {}};
  events_.clear();
  characters_.clear();
  pendingDelays_.clear();
  seFile_.clear();

  if (snapshot.has("touchmode")) {
    touchMode_ = snapshot["touchmode"].toInt(0) != 0;
    if (cb_.setTouchMode)
      cb_.setTouchMode(touchMode_);
  }

  const auto &data = snapshot["data"];
  if (data.isList()) {
    for (const auto &entry : data.asList()) {
      if (entry.isList() && entry.size() >= 3 && entry[2].isDict()) {
        applyObjectDict(entry[0].asString(), entry[1].asString(), entry[2]);
      }
    }
  } else if (snapshot.has("env") && snapshot["env"].isDict()) {
    const auto &objList = snapshot["env"]["objectList"];
    if (objList.isList()) {
      for (const auto &obj : objList.asList()) {
        if (obj.isDict())
          applyObjectDict(obj["name"].asString(), obj["cname"].asString(), obj);
      }
    }
  }
  finishAllActions();
}

ScnRunner::State ScnRunner::saveState() const {
  return {sceneIdx_,
          pc_,
          lastSavepointId_,
          lastSnapshot_.isDict() ? lastSnapshot_.toJson() : std::string{},
          unlockedFlags_,
          finished_};
}

void ScnRunner::restoreState(const State &state) {
  const auto &scenes = psb_.root()["scenes"];
  if (!scenes.isList() || state.sceneIndex >= scenes.size())
    throw std::out_of_range("ScnRunner: save scene index out of bounds");
  const auto &lines = scenes[state.sceneIndex]["lines"];
  if (!lines.isList() || state.lineIndex > lines.size())
    throw std::out_of_range("ScnRunner: save line index out of bounds");

  sceneIdx_ = state.sceneIndex;
  pc_ = state.lineIndex;
  lastSavepointId_ = state.savepointId;
  unlockedFlags_ = state.unlockedFlags;
  finished_ = state.finished || pc_ == lines.size();
  blocked_ = false;
  pendingDelays_.clear();

  if (!state.snapshotJson.empty()) {
    const auto snap =
        Kotonoha::Providers::PsbValue::fromJson(state.snapshotJson);
    applySavepointSnapshot(snap);
  }
}

} // namespace Kirikiri
