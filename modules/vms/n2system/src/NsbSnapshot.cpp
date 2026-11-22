// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Full-fidelity NSB runtime checkpoint, versioned separately from N2SV v1.
// No C++ object representation or pointers are ever written to user saves.
#include <N2System/NsbVm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {
constexpr uint32_t kMagic = 0x5352324eu; // "N2RS" in little endian
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxCount = 8192;
constexpr size_t kMaxText = 1024u * 1024u;
struct Writer {
  std::vector<uint8_t> data;
  void byte(uint8_t v) { data.push_back(v); }
  void flag(bool v) { byte(v ? 1 : 0); }
  void u32(uint32_t v) {
    for (int i=0;i<4;++i) byte(uint8_t(v >> (8*i)));
  }
  void u64(uint64_t v) {
    for (int i=0;i<8;++i) byte(uint8_t(v >> (8*i)));
  }
  void f32(float v) {
    uint32_t bits; std::memcpy(&bits,&v,sizeof(bits)); u32(bits);
  }
  void f64(double v) {
    uint64_t bits; std::memcpy(&bits,&v,sizeof(bits)); u64(bits);
  }
  void str(const std::string &v) {
    if (v.size()>kMaxText) throw std::runtime_error("N2RS string too large");
    u32(uint32_t(v.size())); data.insert(data.end(),v.begin(),v.end());
  }
  void strings(const std::vector<std::string> &values) {
    u32(uint32_t(values.size()));
    for (const auto &v:values) str(v);
  }
  void strset(const std::set<std::string> &values) {
    u32(uint32_t(values.size()));
    for (const auto &v:values) str(v);
  }
  void value(const NsbValue &v) {
    byte(uint8_t(v.kind)); flag(v.isDelta); str(v.lvalueKey);
    switch(v.kind) {
    case NsbValue::Kind::Int: u64(uint64_t(v.intVal)); break;
    case NsbValue::Kind::Float: f64(v.floatVal); break;
    case NsbValue::Kind::String: str(v.strVal); break;
    }
  }
};
struct Reader {
  const std::vector<uint8_t> &data;
  size_t pos=0;
  bool available(size_t n) const {return n<=data.size()-pos;}
  bool byte(uint8_t &v) {
    if (!available(1)) return false;
    v = data[pos++];
    return true;
  }
  bool flag(bool &v) {
    uint8_t b; if(!byte(b)||b>1)return false;v=b==1;return true;
  }
  bool u32(uint32_t &v) {
    if (!available(4)) return false;
    v = 0;
    for(int i=0;i<4;++i)v|=uint32_t(data[pos++])<<(8*i);
    return true;
  }
  bool u64(uint64_t &v) {
    if (!available(8)) return false;
    v = 0;
    for(int i=0;i<8;++i)v|=uint64_t(data[pos++])<<(8*i);
    return true;
  }
  bool f32(float &v) {
    uint32_t bits; if(!u32(bits))return false;
    std::memcpy(&v,&bits,sizeof(bits));return std::isfinite(v);
  }
  bool f64(double &v) {
    uint64_t bits; if(!u64(bits))return false;
    std::memcpy(&v,&bits,sizeof(bits));return std::isfinite(v);
  }
  bool str(std::string &v) {
    uint32_t n;
    if(!u32(n)||n>kMaxText||!available(n))return false;
    v.assign(reinterpret_cast<const char*>(data.data()+pos),n);
    pos+=n;return true;
  }
  bool count(uint32_t &n) {
    return u32(n) && n <= kMaxCount;
  }
  bool strings(std::vector<std::string> &v) {
    uint32_t n; if(!count(n))return false;v.clear();v.reserve(n);
    for(uint32_t i=0;i<n;++i){std::string s;if(!str(s))return false;v.push_back(std::move(s));}
    return true;
  }
  bool strset(std::set<std::string> &v) {
    uint32_t n;if(!count(n))return false;v.clear();
    for(uint32_t i=0;i<n;++i){std::string s;if(!str(s))return false;v.insert(std::move(s));}
    return true;
  }
  bool value(NsbValue &v) {
    uint8_t kind;
    if(!byte(kind)||kind>2||!flag(v.isDelta)||!str(v.lvalueKey))return false;
    v.kind=static_cast<NsbValue::Kind>(kind);
    switch(v.kind) {
    case NsbValue::Kind::Int: {
      uint64_t n;if(!u64(n))return false;
      v.intVal=static_cast<int64_t>(n);v.floatVal=double(v.intVal);return true;
    }
    case NsbValue::Kind::Float: {
      if(!f64(v.floatVal))return false;
      v.intVal=static_cast<int64_t>(std::clamp(v.floatVal,
          double(std::numeric_limits<int64_t>::min()),
          double(std::numeric_limits<int64_t>::max())));return true;
    }
    case NsbValue::Kind::String: return str(v.strVal);
    }
    return false;
  }
};
void writeAudio(Writer &w,const AudioChannel &c) {
  w.str(c.assetPath);w.flag(c.isPlaying);w.flag(c.loop);
  w.f32(c.volume);w.f32(c.targetVolume);w.f32(c.fadeDuration);
  w.f32(c.fadeElapsed);w.flag(c.isFading);
}
bool readAudio(Reader &r,AudioChannel &c) {
  return r.str(c.assetPath)&&r.flag(c.isPlaying)&&r.flag(c.loop)&&
         r.f32(c.volume)&&r.f32(c.targetVolume)&&r.f32(c.fadeDuration)&&
         r.f32(c.fadeElapsed)&&r.flag(c.isFading);
}
void writeEntity(Writer &w, const Entity &e) {
  w.u32(static_cast<uint32_t>(e.type));
  w.u64(e.creationOrder);
  w.u32(e.solidColor);
  w.u32(static_cast<uint32_t>(e.fontSize));
  w.u32(static_cast<uint32_t>(e.selectedIndex));
  w.u64(e.visibleCharCount);
  w.str(e.name);
  w.str(e.alias);
  w.str(e.parent);
  w.str(e.imagePath);
  w.str(e.moviePath);
  w.str(e.text);
  w.str(e.speaker);
  w.str(e.transitionMaskPath);
  w.str(e.focusUp);
  w.str(e.focusDown);
  w.str(e.focusLeft);
  w.str(e.focusRight);
  w.f32(e.x);
  w.f32(e.y);
  w.f32(e.z);
  w.f32(e.width);
  w.f32(e.height);
  w.f32(e.anchorX);
  w.f32(e.anchorY);
  w.f32(e.scaleX);
  w.f32(e.scaleY);
  w.f32(e.angle);
  w.f32(e.opacity);
  w.f32(e.colorR);
  w.f32(e.colorG);
  w.f32(e.colorB);
  w.f32(e.scrollValue);
  w.f32(e.clipX);
  w.f32(e.clipY);
  w.f32(e.clipW);
  w.f32(e.clipH);
  w.f32(e.startX);
  w.f32(e.startY);
  w.f32(e.targetX);
  w.f32(e.targetY);
  w.f32(e.moveDuration);
  w.f32(e.moveElapsed);
  w.f32(e.bezierX1);
  w.f32(e.bezierY1);
  w.f32(e.bezierX2);
  w.f32(e.bezierY2);
  w.f32(e.startOpacity);
  w.f32(e.targetOpacity);
  w.f32(e.opacityDuration);
  w.f32(e.opacityElapsed);
  w.f32(e.startScaleX);
  w.f32(e.startScaleY);
  w.f32(e.targetScaleX);
  w.f32(e.targetScaleY);
  w.f32(e.scaleDuration);
  w.f32(e.scaleElapsed);
  w.f32(e.startAngle);
  w.f32(e.targetAngle);
  w.f32(e.rotateDuration);
  w.f32(e.rotateElapsed);
  w.f32(e.shakeAmpX);
  w.f32(e.shakeAmpY);
  w.f32(e.shakeDuration);
  w.f32(e.shakeElapsed);
  w.f32(e.shakeOffsetX);
  w.f32(e.shakeOffsetY);
  w.f32(e.transitionStart);
  w.f32(e.transitionEnd);
  w.f32(e.transitionDuration);
  w.f32(e.transitionElapsed);
  w.flag(e.alignCenterX);
  w.flag(e.alignRightX);
  w.flag(e.alignCenterY);
  w.flag(e.alignBottomY);
  w.flag(e.visible);
  w.flag(e.locked);
  w.flag(e.smoothing);
  w.flag(e.monochrome);
  w.byte(e.boxBlurPasses);
  w.flag(e.scrollHorizontal);
  w.flag(e.hasClipRect);
  w.flag(e.movieLoop);
  w.flag(e.moviePlaying);
  w.flag(e.isMoving);
  w.flag(e.isBezierMoving);
  w.flag(e.isFading);
  w.flag(e.isScaling);
  w.flag(e.isRotating);
  w.flag(e.isShaking);
  w.flag(e.isTransitioning);
  w.u32(static_cast<uint32_t>(e.moveEase));
  w.u32(static_cast<uint32_t>(e.opacityEase));
  w.u32(static_cast<uint32_t>(e.scaleEase));
  w.u32(static_cast<uint32_t>(e.rotateEase));
  w.u32(static_cast<uint32_t>(e.transitionEase));
  w.strings(e.choiceOptions);
}
bool readEntity(Reader &r, Entity &e) {
  uint32_t type, size, index;
  if (!r.u32(type) || type > uint32_t(EntityType::ScrollBar) ||
      !r.u64(e.creationOrder) || !r.u32(e.solidColor) ||
      !r.u32(size) || !r.u32(index) || !r.u64(e.visibleCharCount))
    return false;
  e.type = static_cast<EntityType>(type);
  e.fontSize = static_cast<int32_t>(size);
  e.selectedIndex = static_cast<int32_t>(index);
  if (!r.str(e.name)) return false;
  if (!r.str(e.alias)) return false;
  if (!r.str(e.parent)) return false;
  if (!r.str(e.imagePath)) return false;
  if (!r.str(e.moviePath)) return false;
  if (!r.str(e.text)) return false;
  if (!r.str(e.speaker)) return false;
  if (!r.str(e.transitionMaskPath)) return false;
  if (!r.str(e.focusUp)) return false;
  if (!r.str(e.focusDown)) return false;
  if (!r.str(e.focusLeft)) return false;
  if (!r.str(e.focusRight)) return false;
  if (!r.f32(e.x)) return false;
  if (!r.f32(e.y)) return false;
  if (!r.f32(e.z)) return false;
  if (!r.f32(e.width)) return false;
  if (!r.f32(e.height)) return false;
  if (!r.f32(e.anchorX)) return false;
  if (!r.f32(e.anchorY)) return false;
  if (!r.f32(e.scaleX)) return false;
  if (!r.f32(e.scaleY)) return false;
  if (!r.f32(e.angle)) return false;
  if (!r.f32(e.opacity)) return false;
  if (!r.f32(e.colorR)) return false;
  if (!r.f32(e.colorG)) return false;
  if (!r.f32(e.colorB)) return false;
  if (!r.f32(e.scrollValue)) return false;
  if (!r.f32(e.clipX)) return false;
  if (!r.f32(e.clipY)) return false;
  if (!r.f32(e.clipW)) return false;
  if (!r.f32(e.clipH)) return false;
  if (!r.f32(e.startX)) return false;
  if (!r.f32(e.startY)) return false;
  if (!r.f32(e.targetX)) return false;
  if (!r.f32(e.targetY)) return false;
  if (!r.f32(e.moveDuration)) return false;
  if (!r.f32(e.moveElapsed)) return false;
  if (!r.f32(e.bezierX1)) return false;
  if (!r.f32(e.bezierY1)) return false;
  if (!r.f32(e.bezierX2)) return false;
  if (!r.f32(e.bezierY2)) return false;
  if (!r.f32(e.startOpacity)) return false;
  if (!r.f32(e.targetOpacity)) return false;
  if (!r.f32(e.opacityDuration)) return false;
  if (!r.f32(e.opacityElapsed)) return false;
  if (!r.f32(e.startScaleX)) return false;
  if (!r.f32(e.startScaleY)) return false;
  if (!r.f32(e.targetScaleX)) return false;
  if (!r.f32(e.targetScaleY)) return false;
  if (!r.f32(e.scaleDuration)) return false;
  if (!r.f32(e.scaleElapsed)) return false;
  if (!r.f32(e.startAngle)) return false;
  if (!r.f32(e.targetAngle)) return false;
  if (!r.f32(e.rotateDuration)) return false;
  if (!r.f32(e.rotateElapsed)) return false;
  if (!r.f32(e.shakeAmpX)) return false;
  if (!r.f32(e.shakeAmpY)) return false;
  if (!r.f32(e.shakeDuration)) return false;
  if (!r.f32(e.shakeElapsed)) return false;
  if (!r.f32(e.shakeOffsetX)) return false;
  if (!r.f32(e.shakeOffsetY)) return false;
  if (!r.f32(e.transitionStart)) return false;
  if (!r.f32(e.transitionEnd)) return false;
  if (!r.f32(e.transitionDuration)) return false;
  if (!r.f32(e.transitionElapsed)) return false;
  if (!r.flag(e.alignCenterX)) return false;
  if (!r.flag(e.alignRightX)) return false;
  if (!r.flag(e.alignCenterY)) return false;
  if (!r.flag(e.alignBottomY)) return false;
  if (!r.flag(e.visible)) return false;
  if (!r.flag(e.locked)) return false;
  if (!r.flag(e.smoothing)) return false;
  if (!r.flag(e.monochrome) || !r.byte(e.boxBlurPasses) ||
      e.boxBlurPasses > 16) return false;
  if (!r.flag(e.scrollHorizontal)) return false;
  if (!r.flag(e.hasClipRect)) return false;
  if (!r.flag(e.movieLoop)) return false;
  if (!r.flag(e.moviePlaying)) return false;
  if (!r.flag(e.isMoving)) return false;
  if (!r.flag(e.isBezierMoving)) return false;
  if (!r.flag(e.isFading)) return false;
  if (!r.flag(e.isScaling)) return false;
  if (!r.flag(e.isRotating)) return false;
  if (!r.flag(e.isShaking)) return false;
  if (!r.flag(e.isTransitioning)) return false;
  { uint32_t value; if (!r.u32(value) || value > 10) return false;
    e.moveEase = static_cast<EaseFunction>(value); }
  { uint32_t value; if (!r.u32(value) || value > 10) return false;
    e.opacityEase = static_cast<EaseFunction>(value); }
  { uint32_t value; if (!r.u32(value) || value > 10) return false;
    e.scaleEase = static_cast<EaseFunction>(value); }
  { uint32_t value; if (!r.u32(value) || value > 10) return false;
    e.rotateEase = static_cast<EaseFunction>(value); }
  { uint32_t value; if (!r.u32(value) || value > 10) return false;
    e.transitionEase = static_cast<EaseFunction>(value); }
  return r.strings(e.choiceOptions);
}
void writePhone(Writer &w,const PhoneState &p) {
  w.flag(p.initialized);w.flag(p.phoneOpen);w.flag(p.waitIconVisible);
  w.u32(uint32_t(p.mailBoxCursor));w.str(p.attachedCg);
  w.strset(p.addressBook);
  w.strset(p.mailReceiveFlags);
  w.strset(p.mailViewFlags);
  w.strset(p.mailReplyFlags);
  w.strset(p.mailTriggerFlags);
  w.strset(p.mailReplySendFlags);
  w.strset(p.tipsUnlocked);
  w.strset(p.achievementsUnlocked);
}
bool readPhone(Reader &r,PhoneState &p) {
  uint32_t cursor;
  if(!r.flag(p.initialized)||!r.flag(p.phoneOpen)||
     !r.flag(p.waitIconVisible)||!r.u32(cursor)||!r.str(p.attachedCg))return false;
  p.mailBoxCursor=static_cast<int32_t>(cursor);
  return r.strset(p.addressBook)&&
         r.strset(p.mailReceiveFlags)&&
         r.strset(p.mailViewFlags)&&
         r.strset(p.mailReplyFlags)&&
         r.strset(p.mailTriggerFlags)&&
         r.strset(p.mailReplySendFlags)&&
         r.strset(p.tipsUnlocked)&&
         r.strset(p.achievementsUnlocked);
}
} // namespace

std::vector<uint8_t> NsbVm::writeRuntimeSnapshot() const {
  Writer w;
  w.u32(kMagic);w.u32(kVersion);
  w.f64(elapsedSeconds_);w.u64(totalExecuted_);w.u32(rngState_);
  w.u32(uint32_t(mouseX_));w.u32(uint32_t(mouseY_));w.flag(finished_);

  w.u32(uint32_t(vars_.size()));
  for(const auto &[name,value]:vars_){w.str(name);w.value(value);}
  w.u32(uint32_t(threads_.size()));
  for(const auto &t:threads_) {
    w.str(t.name);w.str(t.script ? t.script->path() : std::string{});
    w.u64(t.pc);
    w.flag(t.running);w.flag(t.locked);w.flag(t.deleted);
    w.flag(t.waitingKey);w.flag(t.waitingText);w.flag(t.waitingChoice);
    w.flag(t.waitingMovie);w.flag(t.waitEntityAnim);
    w.f32(t.waitTimer);w.f32(t.waitKeyTimeout);w.str(t.waitEntity);
    w.u32(uint32_t(t.stack.size()));
    for(const auto &v:t.stack)w.value(v);
    w.u32(uint32_t(t.callStack.size()));
    for(const auto &frame:t.callStack){
      w.str(frame.script ? frame.script->path() : std::string{});
      w.u64(frame.pc);w.flag(frame.isChapterOrScene);
      w.u32(uint32_t(frame.savedParams.size()));
      for(const auto &saved:frame.savedParams) {
        w.str(saved.first);w.flag(saved.second.first);
        w.value(saved.second.second);
      }
    }
    w.u32(uint32_t(t.selectStack.size()));
    for(const auto &sf:t.selectStack){w.u64(sf.selectPc);w.str(sf.endLabel);}
    w.strings(t.whileEndStack);
  }
  w.u32(uint32_t(sceneGraph_.entities().size()));
  for(const auto &[name,entity]:sceneGraph_.entities())writeEntity(w,entity);

  w.strings(activeChoices_);w.str(triggeredChoice_);
  w.str(activeMovie_.entityName);w.str(activeMovie_.path);
  w.flag(activeMovie_.active);w.flag(activeMovie_.loop);
  w.flag(activeMovie_.fullscreen);
  writePhone(w,phone_);
  w.str(std::string(dialogueSystem_.speaker()));
  w.str(std::string(dialogueSystem_.fullText()));
  w.str(std::string(dialogueSystem_.voicePath()));
  w.u64(dialogueSystem_.visibleCharCount());
  w.f32(dialogueSystem_.typewriterSpeed());
  w.flag(dialogueSystem_.isTyping());
  w.flag(dialogueSystem_.isWaitingForKey());
  w.u32(uint32_t(dialogueSystem_.backlog().size()));
  for(const auto &entry:dialogueSystem_.backlog()){
    w.str(entry.speaker);w.str(entry.text);w.str(entry.voicePath);
  }
  writeAudio(w,audioSystem_.bgmChannel());
  writeAudio(w,audioSystem_.voiceChannel());
  w.u32(uint32_t(audioSystem_.seChannels().size()));
  for(const auto &[id,channel]:audioSystem_.seChannels()) {
    w.u32(uint32_t(id));writeAudio(w,channel);
  }
  w.u32(uint32_t(preBlocks_.size()));
  for(const auto &[k,b]:preBlocks_) {
    w.str(k);w.str(b.id);w.str(b.boxName);w.str(b.speaker);
    w.str(b.voicePath);w.str(b.text);w.str(b.rawXml);
  }
  w.u32(uint32_t(arrayKeys_.size()));
  for(const auto &[k,keys]:arrayKeys_){w.str(k);w.strings(keys);}
  w.u32(uint32_t(soundEntities_.size()));
  for(const auto &[k,v]:soundEntities_){w.str(k);w.str(v);}
  w.u32(uint32_t(soundTypes_.size()));
  for(const auto &[k,v]:soundTypes_){w.str(k);w.str(v);}
  if(w.data.size()>16u*1024u*1024u)
    throw std::runtime_error("N2RS exceeds maximum snapshot size");
  return std::move(w.data);
}

bool NsbVm::restoreRuntimeSnapshot(const std::vector<uint8_t> &snapshot) {
  if(snapshot.size()<8||snapshot.size()>16u*1024u*1024u)return false;
  Reader r{snapshot};
  uint32_t magic,version;
  if(!r.u32(magic)||!r.u32(version)||magic!=kMagic||version!=kVersion)
    return false;
  double elapsed;
  uint64_t executed;
  uint32_t rng,mx,my;
  bool finished;
  if(!r.f64(elapsed)||!r.u64(executed)||!r.u32(rng)||
     !r.u32(mx)||!r.u32(my)||!r.flag(finished))return false;

  uint32_t count;
  if(!r.count(count))return false;
  std::unordered_map<std::string,NsbValue> vars;
  for(uint32_t i=0;i<count;++i){
    std::string name;NsbValue v;
    if(!r.str(name)||!r.value(v))return false;
    vars.emplace(std::move(name),std::move(v));
  }
  if(!r.count(count)||count==0)return false;
  std::deque<Thread> threads;
  for(uint32_t i=0;i<count;++i){
    Thread t;
    std::string scriptPath;
    uint64_t pc;
    if(!r.str(t.name)||!r.str(scriptPath)||!r.u64(pc))return false;
    t.script=loadScript(scriptPath);
    if(!t.script||pc>t.script->instructions().size())return false;
    t.pc=static_cast<size_t>(pc);
    if(!r.flag(t.running)||!r.flag(t.locked)||!r.flag(t.deleted)||
       !r.flag(t.waitingKey)||!r.flag(t.waitingText)||
       !r.flag(t.waitingChoice)||!r.flag(t.waitingMovie)||
       !r.flag(t.waitEntityAnim)||!r.f32(t.waitTimer)||
       !r.f32(t.waitKeyTimeout)||!r.str(t.waitEntity))
      return false;
    uint32_t n;
    if(!r.count(n))return false;
    t.stack.reserve(n);
    for(uint32_t j=0;j<n;++j){NsbValue v;if(!r.value(v))return false;t.stack.push_back(std::move(v));}
    if(!r.count(n))return false;
    for(uint32_t j=0;j<n;++j){
      CallFrame frame;std::string target;
      uint64_t framePc;
      if(!r.str(target)||!r.u64(framePc)||
         !r.flag(frame.isChapterOrScene))return false;
      frame.script=loadScript(target);
      if(!frame.script||framePc>frame.script->instructions().size())return false;
      frame.pc=static_cast<size_t>(framePc);
      uint32_t p;
      if(!r.count(p))return false;
      for(uint32_t k=0;k<p;++k){
        std::string name;bool existed;NsbValue v;
        if(!r.str(name)||!r.flag(existed)||!r.value(v))return false;
        frame.savedParams.push_back({std::move(name),{existed,std::move(v)}});
      }
      t.callStack.push_back(std::move(frame));
    }
    if(!r.count(n))return false;
    for(uint32_t j=0;j<n;++j){
      SelectFrame sf;uint64_t pcVal;
      if(!r.u64(pcVal)||pcVal>t.script->instructions().size()||
         !r.str(sf.endLabel))return false;
      sf.selectPc=static_cast<size_t>(pcVal);
      t.selectStack.push_back(std::move(sf));
    }
    if(!r.strings(t.whileEndStack))return false;
    threads.push_back(std::move(t));
  }

  if(!r.count(count))return false;
  std::vector<Entity> objects;
  objects.reserve(count);
  for(uint32_t i=0;i<count;++i){
    Entity e;
    if(!readEntity(r,e)||e.name.empty())return false;
    objects.push_back(std::move(e));
  }
  std::vector<std::string> choices;std::string choice;
  ActiveMovieRequest movie;
  if(!r.strings(choices)||!r.str(choice)||!r.str(movie.entityName)||
     !r.str(movie.path)||!r.flag(movie.active)||!r.flag(movie.loop)||
     !r.flag(movie.fullscreen))return false;
  PhoneState phone;
  if(!readPhone(r,phone))return false;
  std::string speaker,text,voice;uint64_t visible;
  float speed;bool typing,waiting;
  if(!r.str(speaker)||!r.str(text)||!r.str(voice)||
     !r.u64(visible)||!r.f32(speed)||!r.flag(typing)||
     !r.flag(waiting))return false;
  if(!r.count(count))return false;
  std::vector<DialogueBacklogEntry> backlog;
  for(uint32_t i=0;i<count;++i) {
    DialogueBacklogEntry entry;
    if(!r.str(entry.speaker)||!r.str(entry.text)||
       !r.str(entry.voicePath))return false;
    backlog.push_back(std::move(entry));
  }
  AudioChannel bgm,audioVoice;
  if(!readAudio(r,bgm)||!readAudio(r,audioVoice)||!r.count(count))
    return false;
  std::unordered_map<int,AudioChannel> se;
  for(uint32_t i=0;i<count;++i){
    uint32_t id;AudioChannel channel;
    if(!r.u32(id)||!readAudio(r,channel))return false;
    se.emplace(static_cast<int32_t>(id),std::move(channel));
  }
  if(!r.count(count))return false;
  std::unordered_map<std::string,PreDialogueBlock> preBlocks;
  for(uint32_t i=0;i<count;++i){
    std::string key;PreDialogueBlock block;
    if(!r.str(key)||!r.str(block.id)||!r.str(block.boxName)||
       !r.str(block.speaker)||!r.str(block.voicePath)||
       !r.str(block.text)||!r.str(block.rawXml))return false;
    preBlocks.emplace(std::move(key),std::move(block));
  }
  if(!r.count(count))return false;
  std::unordered_map<std::string,std::vector<std::string>> arrays;
  for(uint32_t i=0;i<count;++i){
    std::string key;std::vector<std::string> values;
    if(!r.str(key)||!r.strings(values))return false;
    arrays.emplace(std::move(key),std::move(values));
  }
  const auto readMap=[&r](std::unordered_map<std::string,std::string> &map){
    uint32_t n;if(!r.count(n))return false;
    for(uint32_t i=0;i<n;++i){
      std::string key,value;
      if(!r.str(key)||!r.str(value))return false;
      map.emplace(std::move(key),std::move(value));
    }
    return true;
  };
  std::unordered_map<std::string,std::string> sounds,soundKinds;
  if(!readMap(sounds)||!readMap(soundKinds)||r.pos!=r.data.size())
    return false;

  // Stage a replacement tree before mutating the running VM.
  SceneGraph restored;
  std::stable_sort(objects.begin(),objects.end(),
       [](const Entity &a,const Entity &b){return a.creationOrder<b.creationOrder;});
  for(const auto &saved:objects){
    auto *e=restored.createEntity(saved.name,saved.type);
    if(!e)return false;
    *e=saved;
    e->parent.clear();
    e->children.clear();
  }
  for(const auto &saved:objects){
    if(!saved.parent.empty() &&
       !restored.setParent(saved.name,saved.parent))
      return false;
    restored.setPriority(saved.name,saved.z);
  }
  sceneGraph_=std::move(restored);
  threads_=std::move(threads);
  vars_=std::move(vars);
  activeChoices_=std::move(choices);
  triggeredChoice_=std::move(choice);
  activeMovie_=std::move(movie);
  phone_=std::move(phone);
  preBlocks_=std::move(preBlocks);
  arrayKeys_=std::move(arrays);
  soundEntities_=std::move(sounds);
  soundTypes_=std::move(soundKinds);
  audioSystem_.restore(bgm,audioVoice,se);
  dialogueSystem_.restore(std::move(speaker),std::move(text),std::move(voice),
                          std::move(backlog),size_t(visible),speed,typing,waiting);
  elapsedSeconds_=elapsed;totalExecuted_=executed;rngState_=rng;
  mouseX_=static_cast<int32_t>(mx);mouseY_=static_cast<int32_t>(my);
  finished_=finished;
  return true;
}
} // namespace N2System
