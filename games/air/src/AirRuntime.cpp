#include "AirRuntime.hpp"
#include <Air/Images.hpp>
#include <Air/Session.hpp>
#include <Air/System.hpp>
#include <Air/Voice.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
namespace Air {
using namespace Kotonoha;
namespace {
struct Stage : Scene {
  std::function<void(RendererRef)> draw;
  std::function<bool(EventRef)> input;
  Stage(Engine &e) : Scene(e) {}
  void render(RendererRef r, float) override { draw(r); }
  bool handleEvent(EventRef e) override { return input(e); }
};
struct Text : VNTextBox {
  AirAssets &assets;
  SurfacePtr frame, back, cursor;
  FontPtr font;
  Text(Engine &e, AirAssets &a) : VNTextBox(e, "", "", 40), assets(a) {
    hide();
    cursor = assets.loadSurface(assets.graphic("kcur00"));
    frame = assets.loadSurface(assets.graphic("smw00a"));
    back = assets.loadSurface(assets.graphic("smw00b"));
    for (int y = 0; y < back->h; ++y)
      for (int x = 0; x < back->w; ++x) {
        auto *p = static_cast<Uint8 *>(back->pixels) + y * back->pitch + x * 4;
        p[0] = 100;
        p[1] = 100;
        p[2] = 160;
        p[3] = Uint8(int(p[3]) * 200 / 255);
      }
    const char *path = "/System/Library/Fonts/Supplemental/Verdana.ttf";
    if (std::filesystem::exists(path)) {
      font.reset(TTF_OpenFont(path, 25));
      if (font)
        TTF_SetFontLineSkip(font.get(), 28);
    }
  }
  GlossCursor textCursor(const std::string &value) const {
    GlossCursor p;
    std::string line;
    auto width = [&](const std::string &v) {
      int w = 0, h = 0;
      if (font)
        TTF_GetStringSize(font.get(), v.data(), v.size(), &w, &h);
      else
        w = int(v.size()) * 13;
      return w;
    };
    for (std::size_t i = 0; i < value.size();) {
      if (value[i] == '\n') {
        p.y += 28;
        line.clear();
        ++i;
        continue;
      }
      auto end = i;
      while (end < value.size() && value[end] != ' ' && value[end] != '\n')
        ++end;
      if (end == i)
        ++end;
      auto word = value.substr(i, end - i);
      if (!line.empty() && width(line + word) > 528) {
        p.y += 28;
        line.clear();
        if (word == " ") {
          i = end;
          continue;
        }
      }
      line += word;
      i = end;
    }
    p.x = 53 + width(line);
    return p;
  }
  bool designSpace() const override { return false; }
  void image(RendererRef r, SurfaceRef s, float x, float y,
             bool upscale = true) {
    TexturePtr t(SDL_CreateTextureFromSurface(&r, &s));
    SDL_FRect dest{x, y, float(s.w), float(s.h)};
    if (upscale)
      RenderUpscaledTexture(&r, t.get(), nullptr, &dest, SDL_GetTicksNS());
    else
      SDL_RenderTexture(&r, t.get(), nullptr, &dest);
  }
  void print(RendererRef r, const std::string &s, float x, float y,
             int wrap = 528) {
    if (s.empty())
      return;
    if (!font) {
      engine.GetUI().text(r, s, x, y, wrap);
      return;
    }
    SurfacePtr shadow(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(), {0, 0, 0, 255}, wrap));
    SurfacePtr text(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(), {255, 255, 255, 255}, wrap));
    if (shadow)
      image(r, *shadow, x + 1, y + 1, false);
    if (text)
      image(r, *text, x, y, false);
  }
  void renderFrame(RendererRef r) override {
    image(r, *back, 0, 345);
    image(r, *frame, 0, 345);
  }
  void renderSpeaker(RendererRef r) override {
    print(r, speakerName, 33, 315, 500);
  }
  void renderDialogue(RendererRef r) override {
    print(r, visibleDialogue, 53, 364);
  }
  void renderIndicator(RendererRef r) override {
    TexturePtr t(SDL_CreateTextureFromSurface(&r, cursor.get()));
    SDL_FRect src{float((SDL_GetTicks() % 850) * 4 / 850 * 16), 0, 16, 16},
        dst{591, 436, 16, 16};
    RenderUpscaledTexture(&r, t.get(), &src, &dst, SDL_GetTicksNS());
  }
};
struct Object {
  std::string file, text;
  int x = 0, y = 0, pattern = 0, alpha = 255;
  bool visible = false;
  int width = 0, height = 0;
  SDL_Color colour{0, 0, 0, 0};
  bool clipped = false;
  SDL_Rect clip{};
};
struct Graphic {
  Image image;
  SurfacePtr surface;
};
SurfacePtr blank(int w = 640, int h = 480) {
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
    throw std::runtime_error("Invalid AIR surface size");
  SurfacePtr s(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    throw std::runtime_error(SDL_GetError());
  SDL_FillSurfaceRect(s.get(), nullptr,
                      SDL_MapSurfaceRGBA(s.get(), 0, 0, 0, 255));
  return s;
}
} // namespace
struct AirRuntime::Impl {
  AirRuntime &runtime;
  std::shared_ptr<AirAssets> assets;
  ScriptArchive archive;
  Session vm;
  VoiceIndex voices;
  AudioConfig audio;
  Engine *engine = nullptr;
  std::unique_ptr<Stage> stage;
  std::unique_ptr<Text> textbox;
  std::unique_ptr<ChoiceQuestionScene> choice;
  std::unique_ptr<VNSaveMenu> saves;
  struct EditField {
    SDL_FRect area{};
    SDL_Color color{255, 255, 255, 255};
    std::string raw, text;
  };
  std::map<int, EditField> editFields;
  int focusedEdit = -1;
  std::string speakerName, musicID;
  std::map<std::string, std::string> selectionLabels;
  int selectionWindow = 0;
  bool musicLoop = false;
  int64_t musicEnd = 0, musicLoopStart = 0, musicFrames = 0;
  std::unique_ptr<MediaPlayer> movie;
  PcmPlayer bgm, voice;
  float bgmGain = 1, fadeFrom = 1, fadeTo = 1;
  int fadeElapsed = 0, fadeDuration = 0;
  bool fadeStops = false;
  Uint64 audioTick = 0;
  std::map<int, std::unique_ptr<PcmPlayer>> sounds;
  std::map<std::string, Graphic> graphics;
  std::map<int, SurfacePtr> dc;
  std::map<int, Object> fg, bg;
  std::string dialogue, error, lastGraphic;
  bool pause = false, clearText = true;
  int pauseKind = 17;
  Uint64 deadline = 0;
  float mouseX = -1, mouseY = -1;
  int button1 = 0, button2 = 0;
  std::map<std::pair<int, int>, int> settings;
  std::map<int, Uint64> timers;
  std::vector<std::array<int, 3>> shake;
  Uint64 shakeStart = 0;
  SurfacePtr transitionOld;
  Uint64 transitionStart = 0;
  int transitionMs = 0;
  SDL_Color flashColour{0, 0, 0, 255};
  Uint64 flashStart = 0;
  int flashDuration = 0;
  struct Frame {
    int from = 0, to = 0, duration = 0;
    Uint64 start = 0;
  };
  std::map<int, Frame> frames;
  Impl(AirRuntime &r, std::shared_ptr<AirAssets> a, int scene)
      : runtime(r), assets(std::move(a)),
        archive(assets->localPath("Seen.txt")),
        vm(archive,
           scene < 0 ? assets->settings().integer("SEEN_START", 9030) : scene),
        voices(assets->directory()),
        audio(parseAudioConfig(assets->settingsSource())) {
    vm.speaker = [this](std::string t) {
      speakerName = decodeText(t, archive.Scenario(vm.scenario()).encoding());
    };
    vm.resolveName = [this](bool global, int n) {
      return systemName(vm.machine(), global, n);
    };
    for (int n = 0; n < 26; ++n) {
      auto name = assets->settings().text(std::string("NAME.") + char('A' + n));
      vm.machine().memory().strings[{26, n}] = name;
      vm.machine().memory().strings[{27, n}] = name;
    }
    vm.glossCursor = [this] {
      return textbox ? textbox->textCursor(dialogue) : GlossCursor{};
    };
    vm.glyphPosition = [this](const std::string &raw) {
      auto glyph = decodeText(raw, archive.Scenario(vm.scenario()).encoding());
      return textbox ? textbox->textCursor(dialogue + glyph).x - 53 : 0;
    };
    vm.selectionText = [this](int window,
                              const std::vector<std::string> &labels) {
      selectionWindow =
          window < 0 ? assets->settings().integer("DEFAULT_SEL_WINDOW", 0)
                     : window;
      selectionLabels.clear();
      for (const auto &raw : labels)
        selectionLabels[raw] =
            decodeText(raw, archive.Scenario(vm.scenario()).encoding());
    };
    vm.command = [this](auto &c) { return command(c); };
    vm.text = [this](std::string t) {
      dialogue += decodeText(t, archive.Scenario(vm.scenario()).encoding());
    };
  }
  SurfaceRef surface(int n) {
    if (n < 0 || n > 15)
      throw std::runtime_error("Invalid AIR DC index");
    auto &i = dc[n];
    if (!i)
      i = blank();
    return *i;
  }
  void ensureDC(int n, int w, int h) {
    auto &old = surface(n);
    if (n <= 1 || (old.w >= w && old.h >= h))
      return;
    auto grown = blank(std::max(old.w, w), std::max(old.h, h));
    blit(old, *grown, nullptr, nullptr, 255, false);
    dc[n] = std::move(grown);
  }
  Graphic &graphic(const std::string &name) {
    auto id = assets->graphic(name);
    auto i = graphics.find(id);
    if (i == graphics.end()) {
      Graphic g;
      g.image =
          canonical(std::filesystem::path(id).extension().string()) == ".PDT"
              ? decodePDT(assets->read(id))
              : decodeG00(assets->read(id));
      g.surface = assets->loadSurface(id);
      i = graphics.emplace(id, std::move(g)).first;
    }
    return i->second;
  }
  void blit(SurfaceRef source, SurfaceRef dest, const SDL_Rect *sr = nullptr,
            SDL_Rect *dr = nullptr, int alpha = 255, bool mask = true) {
    SDL_SetSurfaceBlendMode(&source,
                            mask ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetSurfaceAlphaMod(&source, Uint8(std::clamp(alpha, 0, 255)));
    if (!SDL_BlitSurface(&source, sr, &dest, dr))
      throw std::runtime_error(SDL_GetError());
    SDL_SetSurfaceAlphaMod(&source, 255);
  }
  void drawObject(int id, const Object &o, SurfaceRef dest) {
    int adjustX = 0, adjustY = 0;
    for (int slot = 0; slot < 16; ++slot) {
      adjustX += vm.machine().GetIntValue(
          libreallive::IntMemRef(27, 0, id * 32 + slot * 2));
      adjustY += vm.machine().GetIntValue(
          libreallive::IntMemRef(27, 0, id * 32 + slot * 2 + 1));
    }
    if (!o.visible)
      return;
    SDL_Rect oldClip;
    SDL_GetSurfaceClipRect(&dest, &oldClip);
    struct Restore {
      SDL_Surface &s;
      SDL_Rect r;
      ~Restore() { SDL_SetSurfaceClipRect(&s, &r); }
    } restore{dest, oldClip};
    if (o.clipped)
      SDL_SetSurfaceClipRect(&dest, &o.clip);
    if (o.width > 0 && o.height > 0) {
      auto fill = blank(o.width, o.height);
      SDL_FillSurfaceRect(fill.get(), nullptr,
                          SDL_MapSurfaceRGBA(fill.get(), o.colour.r, o.colour.g,
                                             o.colour.b, o.colour.a));
      SDL_Rect dst{o.x + adjustX, o.y + adjustY, 0, 0};
      blit(*fill, dest, nullptr, &dst, o.alpha);
      return;
    }
    if (!o.text.empty() && textbox && textbox->font) {
      auto decoded =
          decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
      SurfacePtr rendered(TTF_RenderText_Blended(textbox->font.get(),
                                                 decoded.data(), decoded.size(),
                                                 {255, 255, 255, 255}));
      if (rendered) {
        SDL_Rect dst{o.x + adjustX, o.y + adjustY, 0, 0};
        blit(*rendered, dest, nullptr, &dst, o.alpha);
      }
      return;
    }
    if (!o.file.empty()) {
      auto &g = graphic(o.file);
      SDL_Rect src{0, 0, int(g.image.width), int(g.image.height)},
          dst{o.x + adjustX, o.y + adjustY, 0, 0};
      if (!g.image.regions.empty()) {
        auto &r = g.image.regions.at(
            size_t(std::clamp(o.pattern, 0, int(g.image.regions.size() - 1))));
        src = {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
        dst.x -= r.originX;
        dst.y -= r.originY;
      }
      blit(*g.surface, dest, &src, &dst, o.alpha);
    }
  }
  SurfacePtr compose() {
    auto output = blank();
    blit(surface(0), *output, nullptr, nullptr, 255, false);
    for (auto &[id, o] : fg)
      drawObject(id, o, *output);
    if (flashDuration > 0 &&
        SDL_GetTicks() - flashStart < Uint64(flashDuration)) {
      auto colour = blank();
      SDL_FillSurfaceRect(colour.get(), nullptr,
                          SDL_MapSurfaceRGBA(colour.get(), flashColour.r,
                                             flashColour.g, flashColour.b,
                                             255));
      const int step =
          std::min(16, int((SDL_GetTicks() - flashStart) * 16 / flashDuration));
      blit(*colour, *output, nullptr, nullptr, (16 - step) * 255 / 16, true);
    }
    return output;
  }
  void draw(RendererRef r) {
    auto output = compose();
    if (transitionOld &&
        SDL_GetTicks() - transitionStart < Uint64(transitionMs)) {
      auto mixed = blank();
      blit(*transitionOld, *mixed, nullptr, nullptr, 255, false);
      blit(*output, *mixed, nullptr, nullptr,
           int(255.0 * (SDL_GetTicks() - transitionStart) / transitionMs));
      output = std::move(mixed);
    } else
      transitionOld.reset();
    TexturePtr t(SDL_CreateTextureFromSurface(&r, output.get()));
    SDL_FRect destination{0, 0, 640, 480};
    auto elapsed = SDL_GetTicks() - shakeStart;
    for (const auto &step : shake) {
      if (elapsed < Uint64(step[2])) {
        destination.x = step[0];
        destination.y = step[1];
        break;
      }
      elapsed -= step[2];
    }
    RenderUpscaledTexture(&r, t.get(), nullptr, &destination, SDL_GetTicksNS());
    if (movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, {0, 0, 640, 480});
    }
    for (const auto &[id, field] : editFields) {
      engine->GetUI().text(r, field.text + (focusedEdit == id ? "|" : ""),
                           field.area.x, field.area.y, field.area.w,
                           field.color);
    }
    if (!error.empty()) {
      engine->GetUI().panel(r, {12, 15, 616, 155});
      engine->GetUI().text(r, error, 24, 26, 590);
    }
  }
  void play(PcmPlayer &p, const std::string &id, const std::string &channel,
            bool loop = false, int64_t start = 0, int64_t end = 0,
            int64_t loopStart = 0) {
    auto a = assets->loadAudio(id);
    SDL_AudioSpec target{SDL_AUDIO_S16, a.spec.channels, a.spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&a.spec, a.data.data(), int(a.data.size()),
                                 &target, &converted, &size))
      throw std::runtime_error(SDL_GetError());
    PcmPlayer::Clip clip;
    clip.samples.resize(size / 2);
    std::memcpy(clip.samples.data(), converted, size);
    SDL_free(converted);
    clip.channels = target.channels;
    clip.rate = target.freq;
    auto count = clip.samples.size() / clip.channels;
    if (end > 0 && uint64_t(end) < count)
      clip.samples.resize(size_t(end) * clip.channels);
    if (&p == &bgm) {
      bgmGain = 1;
      fadeDuration = fadeElapsed = 0;
      fadeStops = false;
      musicID = id;
      musicLoop = loop;
      musicEnd = end;
      musicLoopStart = loopStart;
      musicFrames = clip.samples.size() / clip.channels;
    }
    clip.loop = loop;
    clip.startFrame = std::min<size_t>(std::max<int64_t>(0, start),
                                       clip.samples.size() / clip.channels);
    clip.loopStart = std::min<size_t>(std::max<int64_t>(0, loopStart),
                                      clip.samples.size() / clip.channels);
    if (!p.Play(engine->GetAudio().channel(channel), std::move(clip)))
      throw std::runtime_error(SDL_GetError());
  }
  int value(const libreallive::CommandElement &c, int n, int fallback = 0) {
    return n < c.GetParamCount() ? IntegerParameter(vm.machine(), c, n)
                                 : fallback;
  }
  std::string str(const libreallive::CommandElement &c, int n) {
    return StringParameter(vm.machine(), c, n);
  }
  void assign(const libreallive::CommandElement &c, int n, int v) {
    Parameter(c, n).SetIntegerValue(vm.machine(), v);
  }
  void wait(int ms) { deadline = SDL_GetTicks() + std::max(0, ms); }
  int frameValue(int n) {
    auto i = frames.find(n);
    if (i == frames.end())
      return 0;
    auto &f = i->second;
    double t = f.duration > 0
                   ? std::min(1., double(SDL_GetTicks() - f.start) / f.duration)
                   : 1.;
    return int(f.from + (f.to - f.from) * t);
  }
  bool command(const libreallive::CommandElement &c);
  void bind(Engine &e) {
    engine = &e;
    if (!stage) {
      stage = std::make_unique<Stage>(e);
      textbox = std::make_unique<Text>(e, *assets);
      choice = std::make_unique<ChoiceQuestionScene>(
          e, "", std::vector<ChoiceOption>{}, nullptr);
      stage->draw = [this](auto &r) { draw(r); };
      stage->input = [this](auto &e) { return event(e); };
      movie = std::make_unique<MediaPlayer>(e.GetAudio());
    }
    runtime.AddScene(stage.get());
    runtime.AddScene(textbox.get());
    runtime.AddScene(choice.get());
    runtime.AddScene(&GetVNBacklogRuntime(e));
    auto &m = GetInGameMenuRuntime(e);
    m.SetOnSave([this] { openSaves(false); });
    m.SetOnLoad([this] { openSaves(true); });
    m.SetOnReturnToMenu([this] {
      GetInGameMenuRuntime(*engine).close();
      vm.jump(9032);
      pause = false;
      error.clear();
      dialogue.clear();
      textbox->hide();
    });
    runtime.AddScene(&m);
  }
  void openSaves(bool load) {
    if (!saves)
      saves = std::make_unique<VNSaveMenu>(*engine);
    char *pref = SDL_GetPrefPath("Kotonoha", "AIR");
    if (!pref)
      throw std::runtime_error(SDL_GetError());
    std::string dir = std::string(pref) + "saves";
    SDL_free(pref);
    saves->open(dir, load);
    runtime.RemoveScene(saves.get());
    runtime.AddScene(saves.get());
  }
  bool event(EventRef e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        e.button.button == SDL_BUTTON_LEFT) {
      SDL_FPoint point{e.button.x, e.button.y};
      for (const auto &[id, field] : editFields)
        if (SDL_PointInRectFloat(&point, &field.area)) {
          focusedEdit = id;
          SDL_SetTextInputArea(&engine->GetWindow(), nullptr, 0);
          SDL_StartTextInput(&engine->GetWindow());
          return true;
        }
    }
    auto edit = editFields.find(focusedEdit);
    if (edit != editFields.end()) {
      if (e.type == SDL_EVENT_TEXT_INPUT) {
        if (edit->second.text.size() + std::strlen(e.text.text) <= 255)
          edit->second.text += e.text.text;
        return true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_BACKSPACE) {
        eraseLastCharacter(edit->second.text);
        return true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB) {
        if (++edit == editFields.end())
          edit = editFields.begin();
        focusedEdit = edit->first;
        return true;
      }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
      mouseX = e.motion.x;
      mouseY = e.motion.y;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      mouseX = e.button.x;
      mouseY = e.button.y;
      auto &b = e.button.button == SDL_BUTTON_LEFT ? button1 : button2;
      b = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 2;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
      GetInGameMenuRuntime(*engine).toggle();
      return true;
    }
    bool advance =
        (e.type == (pauseKind == 205 ? SDL_EVENT_MOUSE_BUTTON_UP
                                     : SDL_EVENT_MOUSE_BUTTON_DOWN) &&
         e.button.button == SDL_BUTTON_LEFT) ||
        (e.type == SDL_EVENT_KEY_DOWN &&
         (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE));
    if (advance && pause) {
      if (!textbox->isDialogueComplete())
        textbox->completeDialogue();
      else {
        pause = false;
        if (pauseKind != 205) {
          dialogue.clear();
          speakerName.clear();
          textbox->hide();
          voice.Stop();
        }
      }
      return true;
    }
    return false;
  }
};
bool AirRuntime::Impl::command(const libreallive::CommandElement &c) {
  const int type = c.modtype(), m = c.module(), op = c.opcode(),
            over = c.overload();
  auto &machine = vm.machine();
  if (type == 0 && m == 3) {
    switch (op) {
    case 17:
    case 205: {
      pauseKind = op;
      pause = true;
      auto translated = engine->GetTranslation().text(vm.scenario(), dialogue);
      if (translated == dialogue && !dialogue.empty() &&
          dialogue.front() == ' ')
        translated =
            engine->GetTranslation().text(vm.scenario(), dialogue.substr(1));
      textbox->setDialogue(
          engine->GetTranslation().text(vm.scenario(), speakerName),
          translated);
      textbox->show();
      return true;
    }
    case 151:
    case 152:
    case 161:
      dialogue.clear();
      textbox->hide();
      return true;
    case 201:
      dialogue += '\n';
      return true;
    case 102:
    case 103:
    case 104:
    case 105:
    case 210:
    case 300:
    case 301:
    case 310:
    case 311:
      return true;
    }
  }
  if (type == 0 && m == 2 && op == 1) {
    auto *s = dynamic_cast<const libreallive::SelectElement *>(&c);
    if (!s)
      throw std::runtime_error("Invalid AIR selection");
    std::vector<ChoiceOption> options;
    int id = 0;
    for (auto &p : s->raw_params()) {
      bool hidden = false;
      for (auto &cond : p.cond_parsed)
        if (cond.effect == libreallive::SelectElement::OPTION_HIDE &&
            !cond.condition.empty()) {
          const char *e = cond.condition.c_str();
          hidden = libreallive::GetExpression(e).GetIntegerValue(machine) != 0;
        }
      if (!hidden) {
        auto raw = libreallive::EvaluatePRINT(machine, p.text);
        auto cached = selectionLabels.find(raw);
        auto decoded =
            cached == selectionLabels.end()
                ? decodeText(raw, archive.Scenario(vm.scenario()).encoding())
                : cached->second;
        options.push_back(
            {engine->GetTranslation().text(vm.scenario(), decoded), id});
      }
      ++id;
    }
    textbox->hide();
    choice->setQuestion("", options, [this](int n, const std::string &) {
      vm.machine().set_store_register(n);
      choice->hide();
    });
    return true;
  }
  if (type == 1 && m == 4) {
    switch (op) {
    case 0:
      SDL_SetWindowTitle(
          &engine->GetWindow(),
          ("AIR — " +
           decodeText(str(c, 0), archive.Scenario(vm.scenario()).encoding()))
              .c_str());
      return true;
    case 100:
    case 101:
      wait(value(c, 0));
      return true;
    case 110:
    case 120:
      timers[(op == 120 ? 256 : 0) + value(c, 0)] = SDL_GetTicks();
      return true;
    case 114:
      machine.set_store_register(int(SDL_GetTicks() - timers[value(c, 0)]));
      return true;
    case 116:
      timers[value(c, 1)] = SDL_GetTicks() - value(c, 0);
      return true;
    case 121: {
      int target = value(c, 0), timer = value(c, 1) + 256;
      int elapsed = int(SDL_GetTicks() - timers[timer]);
      if (elapsed < target)
        wait(target - elapsed);
      return true;
    }
    case 130:
      button1 = button2 = 0;
      return true;
    case 133:
      assign(c, 0, int(mouseX));
      assign(c, 1, int(mouseY));
      assign(c, 2, button1);
      assign(c, 3, button2);
      button1 = button1 == 2 ? 0 : button1;
      button2 = button2 == 2 ? 0 : button2;
      return true;
    case 204:
      SDL_ShowCursor();
      return true;
    case 205:
      SDL_HideCursor();
      return true;
    case 350:
    case 332:
    case 353:
    case 354:
      machine.set_store_register(0);
      return true;
    case 351:
    case 352:
      return true;
    case 410:
      for (int i = 0; i < c.GetParamCount(); ++i)
        assign(c, i, assets->settings().integer("WINDOW_ATTR", 0, i));
      return true;
    case 600:
    case 620: {
      for (int i = 0; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(i);
        const char *cursor = raw.c_str();
        auto p = libreallive::GetComplexParam(cursor).GetContainedPieces();
        if (p.size() != 4)
          throw std::runtime_error("Invalid AIR frame counter");
        int key = p[0].GetIntegerValue(machine) + (op == 620 ? 256 : 0);
        frames[key] = {p[1].GetIntegerValue(machine),
                       p[2].GetIntegerValue(machine),
                       p[3].GetIntegerValue(machine), SDL_GetTicks()};
      }
      return true;
    }
    case 610:
    case 630: {
      bool active = false;
      for (int i = 0; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(i);
        const char *cursor = raw.c_str();
        auto p = libreallive::GetComplexParam(cursor).GetContainedPieces();
        int key = p.at(0).GetIntegerValue(machine) + (op == 630 ? 256 : 0);
        p.at(1).SetIntegerValue(machine, frameValue(key));
        auto f = frames.find(key);
        if (f != frames.end())
          active |=
              SDL_GetTicks() - f->second.start < Uint64(f->second.duration);
      }
      machine.set_store_register(active);
      return true;
    }
    case 1200: {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
      pause = true;
      return true;
    case 1203:
      vm.jump(9032);
      return true;
    case 1211:
    case 1212:
    case 3501:
    case 3502:
      return true;
    case 1502:
      machine.set_store_register(0);
      return true;
    case 1504:
      machine.set_store_register(0);
      return true;
    case 1700: {
      EditField field;
      field.area = {float(value(c, 1)), float(value(c, 2)), float(value(c, 3)),
                    float(value(c, 4))};
      field.color = {Uint8(value(c, 9, 255)), Uint8(value(c, 10, 255)),
                     Uint8(value(c, 11, 255)), 255};
      editFields[value(c, 0)] = std::move(field);
      return true;
    }
    case 1701:
      editFields.erase(value(c, 0));
      if (focusedEdit == value(c, 0)) {
        focusedEdit = -1;
        SDL_StopTextInput(&engine->GetWindow());
      }
      return true;
    case 1703:
      if (!editFields.count(value(c, 0)))
        throw std::runtime_error("AIR edit focus references a missing field");
      focusedEdit = value(c, 0);
      SDL_StartTextInput(&engine->GetWindow());
      return true;
    case 1710: {
      auto &field = editFields.at(value(c, 0));
      field.raw = str(c, 1);
      field.text =
          decodeText(field.raw, archive.Scenario(vm.scenario()).encoding());
      return true;
    }
    case 1711: {
      auto &field = editFields.at(value(c, 0));
      auto raw = field.raw;
      const int encoding = archive.Scenario(vm.scenario()).encoding();
      if (field.text != decodeText(raw, encoding)) {
        raw = ConvertEncoding(field.text, "UTF-8",
                              encoding == 0 ? "CP932" : "CP1252");
        if (encoding == 2) {
          std::string mapped;
          for (unsigned char ch : raw) {
            if (ch < 128)
              mapped += char(ch);
            else if (ch <= 191) {
              mapped += char(0x89);
              mapped += char(ch);
            } else if (ch <= 254)
              mapped += char(ch - 31);
            else {
              mapped += char(0x89);
              mapped += char(0xc0);
            }
          }
          raw = std::move(mapped);
        }
      }
      Parameter(c, 1).SetStringValue(machine, raw);
      return true;
    }
    case 2001:
      machine.set_store_register(0);
      return true;
    case 2051:
    case 2224:
    case 2250:
    case 2275:
      settings[{m, op}] = value(c, 0);
      return true;
    case 2223:
    case 2375:
      machine.set_store_register(0);
      return true;
    }
  }
  if (type == 2 && m == 1 && op == 12 && value(c, 1) == 104) {
    int window = value(c, 2);
    if (window < 0)
      window = vm.currentWindow;
    std::ostringstream key;
    key << "WINDOW." << std::setw(3) << std::setfill('0') << window
        << ".R_COMMAND_MOD";
    machine.set_store_register(assets->settings().integer(key.str(), 0));
    return true;
  }
  if (type == 1 && m == 40 && op == 100) {
    std::string name = str(c, 0);
    if (name == "???")
      name = lastGraphic;
    transitionOld = compose();
    transitionStart = SDL_GetTicks();
    std::ostringstream key;
    key << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
    transitionMs = assets->settings().integer(key.str(), 0, 6);
    wait(transitionMs);
    auto &g = graphic(name);
    blit(*g.surface, surface(0));
    fg = std::move(bg);
    bg.clear();
    textbox->hide();
    return true;
  }
  if (type == 1 && m == 20) {
    if (op == 0 || op == 2) {
      auto name = str(c, 0);
      for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
          ch += 32;
      auto i = audio.tracks.find(name);
      if (i == audio.tracks.end())
        throw std::runtime_error("AIR BGM missing: " + name);
      auto &t = i->second;
      std::string id = "BGM/" + t.file;
      if (!assets->contains(id))
        id += ".NWA";
      play(bgm, id, "BGM", op == 0, t.from, t.to, t.loop);
      return true;
    }
    if (op == 105 || op == 106) {
      fadeFrom = bgmGain;
      fadeTo = 0;
      fadeElapsed = 0;
      fadeDuration = std::max(0, value(c, 0, 1000));
      fadeStops = true;
      if (!fadeDuration) {
        bgm.Stop();
        musicID.clear();
        bgmGain = 0;
      }
      if (op == 106)
        wait(fadeDuration);
      return true;
    }
    if (op == 5) {
      bgm.Stop();
      fadeDuration = 0;
      musicID.clear();
      return true;
    }
  }
  if (type == 1 && m == 23 && op == 0) {
    auto p = voices.find(value(c, 0));
    if (p)
      play(voice,
           std::filesystem::relative(*p, assets->directory()).generic_string(),
           "Voice");
    else
      throw std::runtime_error("AIR voice missing");
    return true;
  }
  if (type == 1 && m == 22 && op == 0) {
    auto mapping = audio.soundEffects.find(value(c, 0));
    if (mapping != audio.soundEffects.end()) {
      auto &sound = sounds[mapping->second.channel];
      if (!sound)
        sound = std::make_unique<PcmPlayer>();
      std::string id = "WAV/" + mapping->second.file;
      if (!assets->contains(id))
        id += ".WAV";
      play(*sound, id, "Se");
    }
    return true;
  }
  if (type == 1 && m == 21) {
    if (op == 0 || op == 2) {
      int channel = value(c, 1);
      auto &p = sounds[channel];
      if (!p)
        p = std::make_unique<PcmPlayer>();
      std::string id = "WAV/" + str(c, 0);
      if (!assets->contains(id))
        id += ".WAV";
      play(*p, id, "Se", op == 2);
      return true;
    }
    if (op == 5 || op == 20 || op == 105) {
      sounds.clear();
      return true;
    }
  }
  if (type == 1 && m == 26 && op == 0) {
    std::string id = "MOV/" + str(c, 0);
    if (!assets->contains(id))
      id += ".mpg";
    movie->open(*assets, id, false, "BGM");
    return true;
  }
  if (type == 1 && (m == 71 || m == 72)) {
    auto &objects = m == 71 ? fg : bg;
    auto &o = objects[value(c, 0)];
    if (op == 1000) {
      o.width = o.height = 0;
      o.file = str(c, 1);
      graphic(o.file);
      if (over >= 1)
        o.visible = value(c, 2);
      if (over >= 2) {
        o.x = value(c, 3);
        o.y = value(c, 4);
      }
      if (over >= 3)
        o.pattern = value(c, 5);
      return true;
    }
    if (op == 1101 && over == 2) {
      o.file.clear();
      if (c.GetParamCount() == 6) {
        o.x = value(c, 1);
        o.y = value(c, 2);
        o.width = value(c, 3);
        o.height = value(c, 4);
        o.visible = value(c, 5);
      } else {
        o.width = value(c, 1);
        o.height = value(c, 2);
        o.x = (640 - o.width) / 2;
        o.y = (480 - o.height) / 2;
        o.visible = value(c, 3);
      }
      return true;
    }
    if (op == 1200) {
      o.file.clear();
      o.width = o.height = 0;
      o.text = str(c, 1);
      return true;
    }
  }
  if (type == 1 && (m == 81 || m == 82)) {
    auto &objects = m == 81 ? fg : bg;
    auto &o = objects[value(c, 0)];
    switch (op) {
    case 1000:
      o.x = value(c, 1);
      o.y = value(c, 2);
      return true;
    case 1006: {
      int id = value(c, 0), slot = value(c, 1);
      if (id < 0 || id > 30000 || slot < 0 || slot >= 16)
        throw std::out_of_range("AIR object adjustment");
      machine.SetIntValue(libreallive::IntMemRef(27, 0, id * 32 + slot * 2),
                          value(c, 2));
      machine.SetIntValue(libreallive::IntMemRef(27, 0, id * 32 + slot * 2 + 1),
                          value(c, 3));
      return true;
    }
    case 1003:
      o.alpha = value(c, 1);
      return true;
    case 1004:
      o.visible = value(c, 1);
      return true;
    case 1039:
      o.pattern = value(c, 1);
      return true;
    case 1034:
      o.clipped = over == 1;
      o.clip = {value(c, 1), value(c, 2), value(c, 3), value(c, 4)};
      return true;
    case 1016:
      o.colour = {Uint8(value(c, 1)), Uint8(value(c, 2)), Uint8(value(c, 3)),
                  Uint8(value(c, 4))};
      return true;
    }
  }
  if (type == 1 && (m == 61 || m == 62)) {
    auto &objects = m == 61 ? fg : bg;
    int id = value(c, 0);
    if (op == 10) {
      objects[id] = Object{};
      return true;
    }
    if (op == 11) {
      objects.erase(id);
      return true;
    }
  }
  if (type == 1 && m == 84) {
    auto &o = fg[value(c, 0)];
    if (op == 1000) {
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1100) {
      int w = o.width, h = o.height;
      if (!o.text.empty() && textbox->font) {
        auto decoded =
            decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
        TTF_GetStringSize(textbox->font.get(), decoded.data(), decoded.size(),
                          &w, &h);
      }
      if (!o.file.empty()) {
        auto &g = graphic(o.file);
        w = g.image.sourceWidth;
        h = g.image.sourceHeight;
        if (!g.image.regions.empty()) {
          auto &r = g.image.regions.at(size_t(o.pattern));
          w = r.x2 - r.x1 + 1;
          h = r.y2 - r.y1 + 1;
        }
      }
      assign(c, 1, w);
      assign(c, 2, h);
      return true;
    }
  }
  if (type == 1 && m == 60 && op == 2) {
    int id = value(c, 0);
    bg[id] = fg[id];
    return true;
  }
  if (type == 1 && m == 62 && op == 2) {
    fg[value(c, 1)] = bg[value(c, 0)];
    return true;
  }
  if (type == 1 && m == 60 && op == 111) {
    fg.clear();
    bg.clear();
    return true;
  }
  if (type == 1 && m == 30 && op == 31) {
    auto &image = surface(value(c, 2));
    Uint8 r = 0, g = 0, b = 0, a = 0;
    float x = value(c, 0), y = value(c, 1);
    if (x >= 0 && y >= 0 && x < image.w && y < image.h)
      SDL_ReadSurfacePixel(&image, int(x), int(y), &r, &g, &b, &a);
    assign(c, 3, r);
    assign(c, 4, g);
    assign(c, 5, b);
    return true;
  }
  if (type == 1 && m == 30 && (op == 0 || op == 20 || op == 22))
    return true;
  if (type == 1 && m == 31 && op == 0)
    return true;
  if (type == 1 && m == 33) {
    // Native RealLive FUN_004544b0: flash(R,G,B,duration), sixteen
    // restoration steps, with a snapshot of the old visible framebuffer.
    if (op == 404 && over == 1) {
      flashColour = {Uint8(std::clamp(value(c, 0), 0, 255)),
                     Uint8(std::clamp(value(c, 1), 0, 255)),
                     Uint8(std::clamp(value(c, 2), 0, 255)), 255};
      flashStart = SDL_GetTicks();
      flashDuration = std::max(0, value(c, 3));
      wait(flashDuration);
      return true;
    }
    if (op == 75 || op == 77) {
      transitionOld = compose();
      transitionStart = SDL_GetTicks();
      std::ostringstream selector;
      selector << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
      transitionMs = assets->settings().integer(selector.str(), 0, 6);
      if (op == 75) {
        auto name = str(c, 0);
        if (name == "???")
          name = lastGraphic;
        if (name != "?")
          blit(*graphic(name).surface, surface(1), nullptr, nullptr, 255,
               false);
      } else if (value(c, 0) != 1)
        blit(surface(value(c, 0)), surface(1), nullptr, nullptr, 255, false);
      for (std::size_t i = 2; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(static_cast<int>(i));
        const char *source = raw.c_str();
        auto command = libreallive::GetData(source);
        if (!command.IsSpecialParameter())
          throw std::runtime_error("AIR grpMulti requires tagged operation");
        auto &pieces = command.GetContainedPieces();
        int tag = command.GetOverloadTag();
        if (pieces.empty() || tag < 0 || tag > 4)
          throw std::runtime_error("AIR grpMulti invalid operation");
        auto filename = pieces[0].GetStringValue(machine);
        auto &image = graphic(filename);
        SDL_Rect sr{0, 0, image.surface->w, image.surface->h}, dst{0, 0, 0, 0};
        int alpha = 255;
        auto n = [&](std::size_t k) {
          if (k >= pieces.size())
            throw std::runtime_error("AIR grpMulti missing parameter");
          return pieces[k].GetIntegerValue(machine);
        };
        if (tag == 1 || tag == 2) {
          std::ostringstream sel;
          sel << "SEL." << std::setw(3) << std::setfill('0') << n(1);
          sr = {assets->settings().integer(sel.str(), 0, 0),
                assets->settings().integer(sel.str(), 0, 1),
                assets->settings().integer(sel.str(), 639, 2) + 1,
                assets->settings().integer(sel.str(), 479, 3) + 1};
          sr.w -= sr.x;
          sr.h -= sr.y;
          dst.x = assets->settings().integer(sel.str(), 0, 4);
          dst.y = assets->settings().integer(sel.str(), 0, 5);
          if (tag == 2)
            alpha = n(2);
        }
        if (tag == 3 || tag == 4) {
          sr = {n(1), n(2), n(3) - n(1), n(4) - n(2)};
          dst = {n(5), n(6), 0, 0};
          if (tag == 4)
            alpha = n(7);
        }
        blit(*image.surface, surface(1), &sr, &dst, alpha, true);
      }
      blit(surface(1), surface(0), nullptr, nullptr, 255, false);
      fg = std::move(bg);
      bg.clear();
      textbox->hide();
      wait(transitionMs);
      return true;
    }
    if (op == 32) {
      std::ostringstream key;
      key << "SHAKE." << std::setw(3) << std::setfill('0') << value(c, 0);
      const auto &values = assets->settings().get(key.str());
      shake.clear();
      int time = 0;
      for (size_t n = 0; n + 2 < values.size(); n += 3) {
        std::array<int, 3> step{std::stoi(values[n]), std::stoi(values[n + 1]),
                                std::max(0, std::stoi(values[n + 2]))};
        shake.push_back(step);
        time += step[2];
      }
      shakeStart = SDL_GetTicks();
      wait(time);
      return true;
    }
    if (op == 300 || op == 301 || op == 1300) {
      auto &s = surface(value(c, 4));
      SDL_Rect rect{value(c, 0), value(c, 1), value(c, 2), value(c, 3)};
      if (op < 1000) {
        rect.w -= rect.x;
        rect.h -= rect.y;
      }
      for (int y = std::max(0, rect.y); y < std::min(s.h, rect.y + rect.h); ++y)
        for (int x = std::max(0, rect.x); x < std::min(s.w, rect.x + rect.w);
             ++x) {
          auto *p = static_cast<Uint8 *>(s.pixels) + y * s.pitch + x * 4;
          if (op == 301) {
            auto gray = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            p[0] = p[1] = p[2] = Uint8(gray);
          } else {
            p[0] = 255 - p[0];
            p[1] = 255 - p[1];
            p[2] = 255 - p[2];
          }
        }
      return true;
    }
    if (op == 1401) {
      auto &source = surface(value(c, 4));
      auto &dest = surface(value(c, 9));
      SDL_Rect sr{value(c, 0), value(c, 1), value(c, 2), value(c, 3)},
          dr{value(c, 5), value(c, 6), value(c, 7), value(c, 8)};
      SDL_SetSurfaceBlendMode(&source, SDL_BLENDMODE_NONE);
      SDL_BlitSurfaceScaled(&source, &sr, &dest, &dr, SDL_SCALEMODE_LINEAR);
      return true;
    }

    if (op == 70 || op == 1050 || op == 1051) {
      auto &g = graphic(str(c, 0));
      int n = value(c, 1);
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid AIR DC");
      if (n > 1 && over < 2)
        dc[n] = blank(g.image.width, g.image.height);
      SDL_Rect source{value(c, 2), value(c, 3), value(c, 4), value(c, 5)},
          dest{value(c, 6), value(c, 7), 0, 0};
      if (over >= 2)
        ensureDC(n, dest.x + source.w, dest.y + source.h);
      blit(*g.surface, surface(n), over >= 2 ? &source : nullptr,
           over >= 2 ? &dest : nullptr, value(c, over >= 2 ? 8 : 2, 255),
           op == 1051);
      return true;
    }
    if (op == 100 || op == 101 || op == 1100 || op == 1101) {
      int sourceDC = value(c, over >= 2 ? 4 : 0),
          targetDC = value(c, over >= 2 ? 7 : 1);
      if (sourceDC != targetDC) {
        SDL_Rect source{value(c, 0), value(c, 1), value(c, 2), value(c, 3)},
            dest{value(c, 5), value(c, 6), 0, 0};
        if (op < 1000) {
          source.w -= source.x;
          source.h -= source.y;
        }
        ensureDC(targetDC, over >= 2 ? dest.x + source.w : surface(sourceDC).w,
                 over >= 2 ? dest.y + source.h : surface(sourceDC).h);
        blit(surface(sourceDC), surface(targetDC),
             over >= 2 ? &source : nullptr, over >= 2 ? &dest : nullptr,
             value(c, over >= 2 ? 8 : 2, 255), op == 101 || op == 1101);
      }
      return true;
    }
    if (op == 16) {
      int n = value(c, 0);
      if (n == 0)
        throw std::runtime_error("Cannot free AIR display DC");
      if (n == 1) {
        dc[1] = blank();
        return true;
      }
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid AIR DC");
      dc.erase(n);
      return true;
    }
    if (op == 31) {
      auto &s = surface(value(c, 0));
      SDL_FillSurfaceRect(
          &s, nullptr,
          SDL_MapSurfaceRGBA(&s, value(c, 1), value(c, 2), value(c, 3), 255));
      return true;
    }
    if (op == 1053 || op == 73 || op == 74) {
      std::string name = str(c, 0);
      if (name == "???")
        name = lastGraphic;
      transitionOld = compose();
      transitionStart = SDL_GetTicks();
      std::ostringstream selKey;
      selKey << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
      const auto sel = selKey.str();
      transitionMs =
          over == 4 ? value(c, 7) : assets->settings().integer(sel, 500, 6);
      wait(transitionMs);
      blit(surface(0), surface(1));
      if (name != "?") {
        if (op != 74)
          lastGraphic = name;
        auto &g = graphic(name);
        SDL_Rect sr{value(c, 1), value(c, 2), value(c, 3) + int(op < 1000),
                    value(c, 4) + int(op < 1000)},
            dst{value(c, 5), value(c, 6), 0, 0};
        if (op < 1000) {
          sr.w -= sr.x;
          sr.h -= sr.y;
        }
        if (over == 0) {
          sr = {assets->settings().integer(sel, 0, 0),
                assets->settings().integer(sel, 0, 1),
                assets->settings().integer(sel, 639, 2) + 1,
                assets->settings().integer(sel, 479, 3) + 1};
          sr.w -= sr.x;
          sr.h -= sr.y;
          dst = {assets->settings().integer(sel, 0, 4),
                 assets->settings().integer(sel, 0, 5), 0, 0};
        }
        blit(*g.surface, surface(1), &sr, &dst, 255, op == 74);
      } else {
      }
      blit(surface(1), surface(0), nullptr, nullptr, 255, false);
      fg = std::move(bg);
      bg.clear();
      textbox->hide();
      return true;
    }
    if (op == 1201) {
      auto &s = surface(c.GetParamCount() > 4 ? value(c, 4) : value(c, 0));
      int offset = c.GetParamCount() > 4 ? 5 : 1;
      SDL_Rect r{value(c, 0), value(c, 1), value(c, 2), value(c, 3)};
      auto fill = blank(c.GetParamCount() > 4 ? std::max(1, r.w) : s.w,
                        c.GetParamCount() > 4 ? std::max(1, r.h) : s.h);
      int alpha = value(c, offset + 3, 255);
      SDL_FillSurfaceRect(fill.get(), nullptr,
                          SDL_MapSurfaceRGBA(fill.get(), value(c, offset),
                                             value(c, offset + 1),
                                             value(c, offset + 2), 255));
      SDL_Rect dest{r.x, r.y, 0, 0};
      blit(*fill, s, nullptr, c.GetParamCount() > 4 ? &dest : nullptr, alpha);
      return true;
    }
  }
  if (systemCommand(machine, c, [this](const std::string &key, int fallback) {
        return assets->settings().integer(key, fallback);
      }))
    return true;
  std::ostringstream message;
  message << "Comando AIR pendente em " << vm.scenario() << ":" << vm.index()
          << " (" << type << ":" << m << ":" << op << "/" << over << ").";
  error = message.str();
  SDL_Log("%s", error.c_str());
  return false;
}
AirRuntime::AirRuntime(std::shared_ptr<AirAssets> a, int scene)
    : Runtime("AIR", "0.1.0", "me.hirameki.kotonoha.air"),
      impl(std::make_unique<Impl>(*this, std::move(a), scene)) {}
AirRuntime::~AirRuntime() = default;
std::string AirRuntime::diagnostic() const { return impl->error; }
void AirRuntime::shutdown() {
  impl->bgm.Stop();
  impl->voice.Stop();
  impl->sounds.clear();
  if (impl->movie)
    impl->movie->stop();
  impl->graphics.clear();
  ClearScenes();
  impl->textbox.reset();
  impl->choice.reset();
  impl->saves.reset();
  impl->stage.reset();
  impl->movie.reset();
}
void AirRuntime::initMenu(Engine &e) { impl->bind(e); }
void AirRuntime::initGameplay(Engine &e) { impl->bind(e); }
void AirRuntime::update() {
  Runtime::update();
  auto &i = *impl;
  if (!i.engine)
    return;
  bool paused = isStoryPaused();
  const auto audioNow = SDL_GetTicks();
  const auto delta = i.audioTick ? audioNow - i.audioTick : 0;
  i.audioTick = audioNow;
  if (!paused && i.fadeDuration > 0) {
    i.fadeElapsed =
        int(std::min<Uint64>(i.fadeDuration, Uint64(i.fadeElapsed) + delta));
    i.bgmGain = i.fadeFrom +
                (i.fadeTo - i.fadeFrom) * float(i.fadeElapsed) / i.fadeDuration;
    i.bgm.SetGain(i.bgmGain);
    if (i.fadeElapsed == i.fadeDuration) {
      i.fadeDuration = 0;
      if (i.fadeStops) {
        i.bgm.Stop();
        i.musicID.clear();
      }
    }
  }
  i.bgm.SetPaused(paused);
  i.voice.SetPaused(paused);
  for (auto &[n, s] : i.sounds)
    s->SetPaused(paused);
  if (i.movie)
    i.movie->setPaused(paused);
  if (paused || i.pause || i.choice->isActive() || !i.error.empty() ||
      SDL_GetTicks() < i.deadline)
    return;
  if (i.movie->isOpen()) {
    if (!i.movie->finished())
      return;
    i.movie->stop();
  }
  for (int n = 0;
       n < 3000 && !i.pause && !i.choice->isActive() && i.error.empty() &&
       SDL_GetTicks() >= i.deadline && !i.movie->isOpen();
       ++n)
    try {
      if (!i.vm.step())
        break;
    } catch (const std::exception &e) {
      i.error = "AIR " + debugNode() + ": " + e.what();
      SDL_Log("%s", i.error.c_str());
      break;
    }
  if (i.vm.scenario() < 8000 && GetState() == GameState::Menu) {
    if (GetScenes(GameState::Gameplay).empty())
      for (auto *scene : GetScenes(GameState::Menu))
        AddScene(scene, GameState::Gameplay);
    SetState(GameState::Gameplay);
  }
}
void AirRuntime::saveGame(const std::string &path) {
  auto &i = *impl;
  if (!i.pause || !i.error.empty() || i.choice->isActive())
    throw std::runtime_error(
        "Salve durante uma fala, após terminar a transição.");
  std::ostringstream out;
  out << "AIR-PRESENTATION-2\n"
      << std::quoted(i.vm.snapshot()) << '\n'
      << std::quoted(i.dialogue) << ' ' << std::quoted(i.speakerName) << ' '
      << i.pauseKind << '\n';
  out << std::quoted(i.musicID) << ' ' << i.musicLoop << ' ' << i.musicEnd
      << ' ' << i.musicLoopStart << ' ' <<
      [&]() {
        auto frame = i.bgm.Position() * 44100;
        if (i.musicLoop && i.musicFrames > i.musicLoopStart &&
            frame >= i.musicFrames)
          frame = i.musicLoopStart +
                  std::fmod(frame - i.musicLoopStart,
                            double(i.musicFrames - i.musicLoopStart));
        return frame / 44100.;
      }()
      << '\n';
  out << i.bgmGain << ' ' << i.fadeFrom << ' ' << i.fadeTo << ' '
      << i.fadeElapsed << ' ' << i.fadeDuration << ' ' << i.fadeStops << '\n';
  auto objects = [&](const std::map<int, Object> &objects) {
    out << objects.size() << '\n';
    for (auto &[id, o] : objects)
      out << id << ' ' << std::quoted(o.file) << ' ' << std::quoted(o.text)
          << ' ' << o.x << ' ' << o.y << ' ' << o.pattern << ' ' << o.alpha
          << ' ' << o.visible << ' ' << o.width << ' ' << o.height << ' '
          << int(o.colour.r) << ' ' << int(o.colour.g) << ' ' << int(o.colour.b)
          << ' ' << int(o.colour.a) << ' ' << o.clipped << ' ' << o.clip.x
          << ' ' << o.clip.y << ' ' << o.clip.w << ' ' << o.clip.h << '\n';
  };
  objects(i.fg);
  objects(i.bg);
  out << i.dc.size() << '\n';
  for (auto &[id, s] : i.dc) {
    std::string pixels;
    pixels.reserve(size_t(s->w) * s->h * 4);
    for (int y = 0; y < s->h; ++y)
      pixels.append(static_cast<const char *>(s->pixels) + y * s->pitch,
                    size_t(s->w) * 4);
    out << id << ' ' << s->w << ' ' << s->h << ' ' << std::quoted(pixels)
        << '\n';
  }
  VNSave save;
  save.nodeId = i.vm.scenario();
  save.gameData = out.str();
  if (!std::filesystem::path(path).parent_path().empty())
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
  save.write(path);
}
void AirRuntime::loadGame(const std::string &path) {
  auto &i = *impl;
  auto data = VNSave::read(path);
  std::istringstream in(data.gameData);
  auto invalid = []() {
    throw std::runtime_error("Save AIR inválido ou incompatível.");
  };
  std::string version, state, dialogue, speaker, music;
  bool loop;
  int64_t end, loopStart;
  double position;
  int pauseKind;
  if (!(in >> version >> std::quoted(state) >> std::quoted(dialogue) >>
        std::quoted(speaker) >> pauseKind >> std::quoted(music) >> loop >>
        end >> loopStart >> position) ||
      (version != "AIR-PRESENTATION-1" && version != "AIR-PRESENTATION-2") ||
      (pauseKind != 17 && pauseKind != 205) || position < 0 ||
      !std::isfinite(position))
    invalid();
  float gain = 1, fadeFrom = 1, fadeTo = 1;
  int fadeElapsed = 0, fadeDuration = 0;
  bool fadeStops = false;
  if (version == "AIR-PRESENTATION-2" &&
      (!(in >> gain >> fadeFrom >> fadeTo >> fadeElapsed >> fadeDuration >>
         fadeStops) ||
       !std::isfinite(gain) || !std::isfinite(fadeFrom) ||
       !std::isfinite(fadeTo) || gain < 0 || gain > 1 || fadeFrom < 0 ||
       fadeFrom > 1 || fadeTo < 0 || fadeTo > 1 || fadeElapsed < 0 ||
       fadeDuration < 0 || (fadeDuration > 0 && fadeElapsed > fadeDuration)))
    invalid();
  std::map<int, Object> fg, bg;
  std::map<int, SurfacePtr> dc;
  auto objects = [&](std::map<int, Object> &objects) {
    size_t count;
    if (!(in >> count) || count > 65536)
      invalid();
    for (size_t n = 0; n < count; ++n) {
      int id, r, g, b, a;
      Object o;
      if (!(in >> id >> std::quoted(o.file) >> std::quoted(o.text) >> o.x >>
            o.y >> o.pattern >> o.alpha >> o.visible >> o.width >> o.height >>
            r >> g >> b >> a >> o.clipped >> o.clip.x >> o.clip.y >> o.clip.w >>
            o.clip.h) ||
          id < 0 || id > 65535 || o.alpha < 0 || o.alpha > 255 ||
          o.pattern < 0 || o.width < 0 || o.height < 0 || r < 0 || r > 255 ||
          g < 0 || g > 255 || b < 0 || b > 255 || a < 0 || a > 255)
        invalid();
      o.colour = {Uint8(r), Uint8(g), Uint8(b), Uint8(a)};
      if (!o.file.empty())
        i.graphic(o.file);
      if (!objects.emplace(id, std::move(o)).second)
        invalid();
    }
  };
  objects(fg);
  objects(bg);
  size_t count, total = 0;
  if (!(in >> count) || count > 16)
    invalid();
  for (size_t n = 0; n < count; ++n) {
    int id, w, h;
    std::string pixels;
    if (!(in >> id >> w >> h >> std::quoted(pixels)) || id < 0 || id > 15 ||
        w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
        pixels.size() != size_t(w) * h * 4)
      invalid();
    total += pixels.size();
    if (total > 128 * 1024 * 1024)
      invalid();
    auto s = blank(w, h);
    for (int y = 0; y < h; ++y)
      std::memcpy(static_cast<char *>(s->pixels) + y * s->pitch,
                  pixels.data() + size_t(y) * w * 4, size_t(w) * 4);
    if (!dc.emplace(id, std::move(s)).second)
      invalid();
  }
  in >> std::ws;
  if (!in.eof())
    invalid();
  if (!music.empty() && !i.assets->contains(music))
    invalid();
  i.vm.restore(state);
  i.fg.swap(fg);
  i.bg.swap(bg);
  i.dc.swap(dc);
  i.dialogue = std::move(dialogue);
  i.speakerName = std::move(speaker);
  i.pause = true;
  i.pauseKind = pauseKind;
  i.deadline = 0;
  i.error.clear();
  i.transitionOld.reset();
  i.choice->hide();
  i.movie->stop();
  i.voice.Stop();
  i.sounds.clear();
  i.bgm.Stop();
  i.musicID.clear();
  auto translated =
      i.engine->GetTranslation().text(i.vm.scenario(), i.dialogue);
  if (translated == i.dialogue && !i.dialogue.empty() &&
      i.dialogue.front() == ' ')
    translated =
        i.engine->GetTranslation().text(i.vm.scenario(), i.dialogue.substr(1));
  i.textbox->setDialogue(
      i.engine->GetTranslation().text(i.vm.scenario(), i.speakerName),
      translated);
  i.textbox->completeDialogue();
  i.textbox->show();
  if (!music.empty())
    i.play(i.bgm, music, "BGM", loop, int64_t(position * 44100), end,
           loopStart);
  i.bgmGain = gain;
  i.fadeFrom = fadeFrom;
  i.fadeTo = fadeTo;
  i.fadeElapsed = fadeElapsed;
  i.fadeDuration = fadeDuration;
  i.fadeStops = fadeStops;
  i.audioTick = SDL_GetTicks();
  i.bgm.SetGain(gain);
}

void AirRuntime::onEvent(EventRef e) { Runtime::onEvent(e); }
int AirRuntime::translationRoute() const { return impl->vm.scenario(); }
std::vector<int> AirRuntime::translationRoutes() const {
  return impl->archive.ScenarioNumbers();
}
std::vector<std::string> AirRuntime::translationTexts(int route) const {
  std::vector<std::string> result;
  std::set<std::string> seen;
  auto &scene = impl->archive.Scenario(route);
  RLMachine staticMemory;
  auto add = [&](const std::string &raw) {
    if (raw.empty())
      return;
    std::string decoded;
    // Preserve PRINT expressions and native control/name bytes in source keys;
    // evaluate them only during execution, where the correct memory is known.
    try {
      decoded = raw.find(char(0)) != std::string::npos ||
                        raw.compare(0, 9, "###PRINT(") == 0
                    ? libreallive::ParsableToPrintableString(raw)
                    : decodeText(raw, scene.encoding());
    } catch (const std::exception &) {
      // Dynamic name references use CP932 codes outside the western alphabet.
      // Their printable expression form retains the exact bytes for
      // translators.
      decoded = libreallive::ParsableToPrintableString(raw);
    }
    if (!decoded.empty() && seen.insert(decoded).second)
      result.push_back(std::move(decoded));
  };
  for (const auto &element : scene) {
    if (auto *text =
            dynamic_cast<const libreallive::TextoutElement *>(element.get()))
      add(text->GetText());
    auto *command =
        dynamic_cast<const libreallive::CommandElement *>(element.get());
    if (!command)
      continue;
    if (auto *select =
            dynamic_cast<const libreallive::SelectElement *>(command))
      for (const auto &option : select->raw_params())
        add(option.text);
    int parameter = -1;
    if (command->modtype() == 1 && command->module() == 10 &&
        (command->opcode() == 0 || command->opcode() == 2))
      parameter = 1;
    if (command->modtype() == 1 && command->module() == 10 &&
        command->opcode() == 100 && command->overload() == 0)
      parameter = 0;
    if (command->modtype() == 1 && command->module() == 4 &&
        command->opcode() == 0)
      parameter = 0;
    if (command->modtype() == 1 && command->module() == 4 &&
        (command->opcode() == 1301 || command->opcode() == 1311 ||
         command->opcode() == 1710))
      parameter = 1;
    if (parameter >= 0 &&
        static_cast<std::size_t>(parameter) < command->GetParamCount()) {
      auto piece = Parameter(*command, parameter);
      if (!piece.IsMemoryReference() &&
          piece.GetExpressionValueType() == libreallive::ValueTypeString)
        add(piece.GetStringValue(staticMemory));
    }
  }
  return result;
}
Translation::SourceTexts AirRuntime::translationSources() const {
  Translation::SourceTexts result;
  for (int route : translationRoutes())
    result[route] = translationTexts(route);
  return result;
}
std::string AirRuntime::debugNode() const {
  return std::to_string(impl->vm.scenario()) + ":" +
         std::to_string(impl->vm.index());
}
std::vector<DebugNode> AirRuntime::debugNodes() const {
  std::vector<DebugNode> nodes;
  for (auto n : impl->archive.ScenarioNumbers())
    nodes.push_back({std::to_string(n), "SEEN " + std::to_string(n), {}});
  return nodes;
}
void AirRuntime::debugJumpToNode(const std::string &s) {
  auto colon = s.find(':');
  if (colon == s.npos)
    impl->vm.jump(std::stoi(s));
  else
    impl->vm.seek(std::stoi(s.substr(0, colon)),
                  std::stoull(s.substr(colon + 1)));
  impl->deadline = 0;
  impl->editFields.clear();
  impl->focusedEdit = -1;
  if (impl->engine)
    SDL_StopTextInput(&impl->engine->GetWindow());
  impl->error.clear();
  impl->pause = false;
  impl->dialogue.clear();
  if (impl->choice)
    impl->choice->hide();
  if (impl->movie)
    impl->movie->stop();
  impl->transitionOld.reset();
  if (impl->textbox)
    impl->textbox->hide();
}
SDL_FPoint AirRuntime::sceneSize(const Scene &s) const {
  return s.designSpace() ? SDL_FPoint{1280, 720} : SDL_FPoint{640, 480};
}
} // namespace Air
