#include "AirRuntime.hpp"
#include "AirSelection.hpp"
#include "AirTransition.hpp"
#include "TitleInput.hpp"
#include <Air/Images.hpp>
#include <Air/Session.hpp>
#include <Air/System.hpp>
#include <Air/Voice.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/ImageButtonBar.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/TransformTween.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/SurfaceBlend.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
namespace Air {
using namespace Kotonoha;
namespace {
// Keep the composed stage active while the separate presentation scenes are
// introduced; bind() still supplies its drawing and input callbacks.
struct Stage final : Scene {
  std::function<void(RendererRef)> draw;
  std::function<bool(EventRef)> input;
  explicit Stage(Engine &engine) : Scene(engine) {}
  void render(RendererRef renderer, float) override {
    if (draw)
      draw(renderer);
  }
  bool onEvent(EventRef event) override { return input && input(event); }
};
namespace Air {
struct Impl;
} // namespace Air

namespace {
struct AirBackgroundScene final : Scene {
  Air::Impl &impl;
  AirBackgroundScene(Engine &e, Air::Impl &impl) : Scene(e), impl(impl) {}
  void render(RendererRef r, float scale) override;
  bool handleEvent(EventRef e) override;
  bool designSpace() const override { return false; }
};

struct AirMovieScene final : Scene {
  Air::Impl &impl;
  AirMovieScene(Engine &e, Air::Impl &impl) : Scene(e), impl(impl) {}
  void render(RendererRef r, float scale) override;
  bool handleEvent(EventRef e) override { return false; }
  bool designSpace() const override { return false; }
};

struct AirEditFieldScene final : Scene {
  Air::Impl &impl;
  AirEditFieldScene(Engine &e, Air::Impl &impl) : Scene(e), impl(impl) {}
  void render(RendererRef r, float scale) override;
  bool handleEvent(EventRef e) override;
  bool designSpace() const override { return false; }
};

struct AirErrorScene final : Scene {
  Air::Impl &impl;
  AirErrorScene(Engine &e, Air::Impl &impl) : Scene(e), impl(impl) {}
  void render(RendererRef r, float scale) override;
  bool handleEvent(EventRef e) override { return false; }
  bool designSpace() const override { return false; }
};
} // namespace
struct Text : VNTextBox {
  AirAssets &assets;
  SurfacePtr frame, back, cursor, buttonSurface;
  Image buttonImage;
  ImageButtonBar controls;
  bool concealed = false;
  std::function<bool()> autoActive, skipActive;
  std::map<SDL_Surface *, TexturePtr> imageTextures;
  std::vector<Uint8> fontData;
  FontPtr font;
  Text(Engine &e, AirAssets &a) : VNTextBox(e, "", "", 40), assets(a) {
    hide();
    buttonImage = decodeG00(assets.read(assets.graphic("smw00c")));
    buttonSurface = assets.loadSurface(assets.graphic("smw00c"));
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
    if (!font) {
      fontData = FileResolver::loadFontMemory(e.GetSettings().fontPath);
      IOStreamPtr stream(SDL_IOFromConstMem(fontData.data(), fontData.size()));
      if (stream)
        font.reset(TTF_OpenFontIO(stream.release(), true, 25));
    }
    if (font)
      TTF_SetFontLineSkip(font.get(), 28);
  }
  GlossCursor textCursor(const std::string &value) const {
    GlossCursor p;
    auto width = [&](const std::string &v) {
      int w = 0, h = 0;
      if (font)
        TTF_GetStringSize(font.get(), v.data(), v.size(), &w, &h);
      else
        w = int(v.size()) * 13;
      return static_cast<float>(w);
    };
    const auto lines =
        Kotonoha::VNTextBoxUtility::WrapText(value, width, 528.0f);
    if (!lines.empty()) {
      p.y = static_cast<float>((lines.size() - 1) * 28);
      p.x = 53.0f + width(lines.back());
    } else {
      p.x = 53.0f;
      p.y = 0.0f;
    }
    return p;
  }
  bool designSpace() const override { return false; }
  void image(RendererRef r, SurfaceRef s, float x, float y,
             bool upscale = true) {
    auto &t = imageTextures[&s];
    if (!t) {
      t.reset(SDL_CreateTextureFromSurface(&r, &s));
      if (auto *pipeline = Upscaling::forRenderer(r))
        pipeline->process(*t, 0);
    }
    SDL_FRect dest{x, y, float(s.w), float(s.h)};
    if (upscale)
      RenderUpscaledTexture(&r, t.get(), nullptr, &dest);
    else
      SDL_RenderTexture(&r, t.get(), nullptr, &dest);
  }
  void print(RendererRef r, const std::string &s, float x, float y,
             int wrap = 528, bool speaker = false) {
    if (s.empty())
      return;
    if (!engine.GetSettings().vnFontPath.empty()) {
      const auto color = engine.GetSettings().vnTextColorOverride
                             ? (speaker ? engine.GetSettings().vnSpeakerColor
                                        : engine.GetSettings().vnTextColor)
                             : SDL_Color{255, 255, 255, 255};
      engine.GetVNUI().textSized(r, s, x + 1, y + 1, wrap, 25, {0, 0, 0, 255});
      engine.GetVNUI().textSized(r, s, x, y, wrap, 25, color);
      return;
    }
    if (!font) {
      engine.GetVNUI().text(r, s, x, y, wrap);
      return;
    }
    const float density = UI::rasterDensity(r);
    TTF_SetFontSize(font.get(), 25 * density);
    struct RestoreFont {
      TTF_Font *font;
      ~RestoreFont() { TTF_SetFontSize(font, 25); }
    } restore{font.get()};
    SurfacePtr shadow(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(), {0, 0, 0, 255},
        int(std::ceil(wrap * density))));
    SurfacePtr text(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(),
        engine.GetSettings().vnTextColorOverride
            ? (speaker ? engine.GetSettings().vnSpeakerColor
                       : engine.GetSettings().vnTextColor)
            : SDL_Color{255, 255, 255, 255},
        int(std::ceil(wrap * density))));
    if (shadow)
      drawRaster(r, *shadow, x + 1, y + 1, density);
    if (text)
      drawRaster(r, *text, x, y, density);
  }
  void drawRaster(RendererRef r, SurfaceRef surface, float x, float y,
                  float density) {
    TexturePtr texture(SDL_CreateTextureFromSurface(&r, &surface));
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    SDL_FRect destination{x, y, surface.w / density, surface.h / density};
    SDL_RenderTexture(&r, texture.get(), nullptr, &destination);
  }
  void update() override {
    if (concealed)
      clock.reset();
    else
      VNTextBox::update();
  }
  void render(RendererRef r, float scale) override {
    if (!concealed)
      VNTextBox::render(r, scale);
  }
  bool handleEvent(EventRef event) override {
    if (!isVisible() || concealed)
      return false;
    // The sprite toolbar is pointer-only. D-pad and arrow keys belong to the
    // dialogue/game menus; otherwise a stray D-pad press leaves hidden keyboard
    // focus on a toolbar button and East activates it instead of advancing.
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
      return false;
    return controls.handle(event);
  }
  void renderFrame(RendererRef r) override {
    image(r, *back, 0, 345);
    image(r, *frame, 0, 345);
    auto &texture = imageTextures[buttonSurface.get()];
    if (!texture)
      texture.reset(SDL_CreateTextureFromSurface(&r, buttonSurface.get()));
    constexpr int groups[]{8, 24, 32, 112, 104};
    for (std::size_t i = 0; i < controls.buttons.size(); ++i) {
      const bool active = (i == 3 && autoActive && autoActive()) ||
                          (i == 4 && skipActive && skipActive());
      const int index =
          groups[i] +
          (active && controls.state(int(i)) == 0 ? 3 : controls.state(int(i)));
      const auto &region = buttonImage.regions.at(index);
      SDL_FRect source{float(region.atlasX), float(region.atlasY),
                       float(region.x2 - region.x1 + 1),
                       float(region.y2 - region.y1 + 1)};
      RenderUpscaledTexture(&r, texture.get(), &source,
                            &controls.buttons[i].area);
    }
  }
  void renderSpeaker(RendererRef r) override {
    print(r, speakerName, 33, 315, 500, true);
  }
  void renderDialogue(RendererRef r) override {
    auto measure = [this](const std::string &text) {
      int w = 0, h = 0;
      if (!engine.GetSettings().vnFontPath.empty())
        return engine.GetVNUI().measureSized(text, 10000, 25).x;
      if (font)
        TTF_GetStringSize(font.get(), text.data(), text.size(), &w, &h);
      return float(w);
    };
    const auto lines = VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, 528);
    for (std::size_t n = 0; n < lines.size(); ++n)
      print(r, lines[n], 53, 364 + n * 28);
  }
  void renderIndicator(RendererRef r) override {
    auto &t = imageTextures[cursor.get()];
    if (!t) {
      t.reset(SDL_CreateTextureFromSurface(&r, cursor.get()));
      if (auto *pipeline = Upscaling::forRenderer(r))
        pipeline->process(*t, 0);
    }
    SDL_FRect src{float((SDL_GetTicks() % 850) * 4 / 850 * 16), 0, 16, 16},
        dst{591, 436, 16, 16};
    RenderUpscaledTexture(&r, t.get(), &src, &dst);
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
  std::array<SDL_Point, 16> adjustments{};
  int frameMs = 0;
  Uint64 animationElapsed = 0;
  size_t currentPattern(size_t count) const {
    return frameMs > 0 ? (animationElapsed / Uint64(frameMs)) % count
                       : size_t(std::clamp(pattern, 0, int(count - 1)));
  }
};
struct Graphic {
  struct Raster {
    TexturePtr original, filtered, display;
  };
  Image image;
  SurfacePtr surface;
  std::map<size_t, SurfacePtr> scaledPatterns;
  std::map<std::array<int, 4>, Raster> crops;
};
// Preserve native DC operations for saves and cache each completed raster.
struct DCImage {
  enum class Kind { Pixels, Graphic, Copy, Invert, Gray };
  Kind kind = Kind::Pixels;
  int width = 0, height = 0, alpha = 255;
  bool mask = false;
  SDL_Rect crop{}, destination{};
  std::string graphic;
  SurfacePtr native, raster;
  TexturePtr texture;
  std::shared_ptr<DCImage> source, previous;
  size_t generation = 0;
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
    SDL_Color color{0, 0, 0, 255};
    SDL_Color background{255, 255, 255, 255};
    std::string raw, text;
  };
  std::map<int, EditField> editFields;
  int focusedEdit = -1;
  bool autoMode = false, skipMode = false, skipHeld = false;
  ControllerTriggerSkip triggerSkip;
  Uint64 autoDeadline = 0, skipDeadline = 0;
  bool ctrlSkipEnabled = true, currentRead = false;
  std::pair<int, std::size_t> currentPage;
  std::set<std::pair<int, std::size_t>> readPages;
  bool refreshRequested = false;
  std::string speakerName, musicID, saveName;
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
  TexturePtr canvasTexture;
  SurfacePtr canvasPixels;
  TexturePtr compositionTexture, backgroundTexture;
  TexturePtr zoomTexture, transitionTexture;
  TexturePtr ditherTexture, maskedTransitionTexture;
  std::optional<bool> gpuDither;
  int ditherStep = -1, ditherStyle = -1;
  SurfacePtr backgroundPixels;
  std::map<std::pair<std::string, int>, TexturePtr> objectTextTextures;
  RenderStats renderStats;
  UpscaleSettings imageUpscaling;
  std::map<int, SurfacePtr> dc;
  std::map<int, std::shared_ptr<DCImage>> dcImages;
  size_t dcGeneration = 1;
  std::map<int, Object> fg, bg;
  std::string dialogue, error, lastGraphic;
  bool pause = false, clearText = true;
  int pauseKind = 17;
  Uint64 deadline = 0;
  float mouseX = -1, mouseY = -1;
  int button1 = 0, button2 = 0;
  std::array<Uint8, 32> syscomEntries{};
  TitleInput titleInput;
  std::map<std::pair<int, int>, int> settings;
  std::map<int, Uint64> timers;
  std::vector<std::array<int, 3>> shake;
  Uint64 shakeStart = 0;
  SurfacePtr transitionOld;
  Uint64 transitionStart = 0;
  int transitionMs = 0;
  int transitionStyle = 0;
  struct ZoomEffect {
    SurfacePtr source;
    std::shared_ptr<DCImage> image;
    SDL_Rect to{}, destinationRect{};
    TransformAnimation animation;
    Uint64 start = 0;
  };
  std::optional<ZoomEffect> zoom;
  SDL_Color flashColour{0, 0, 0, 255};
  Uint64 flashStart = 0;
  int flashDuration = 0;
  struct Frame {
    int from = 0, to = 0, duration = 0;
    Uint64 start = 0;
    bool loop = false;
    double value = 0;
    Uint64 lastCheck = 0;
  };
  std::map<int, Frame> frames;
  Impl(AirRuntime &r, std::shared_ptr<AirAssets> a, int scene)
      : runtime(r), assets(std::move(a)), archive(assets->scripts()),
        vm(archive,
           scene < 0 ? assets->settings().integer("SEEN_START", 9030) : scene),
        voices(assets->list("KOE/")),
        audio(parseAudioConfig(assets->settingsSource())) {
    syscomEntries.fill(1);
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
    auto image = dcImage(n);
    const SDL_Rect crop{0, 0, old.w, old.h};
    blit(old, *grown, nullptr, nullptr, 255, false);
    dc[n] = std::move(grown);
    auto extended = pixelImage(*dc[n]);
    extended->kind = DCImage::Kind::Copy;
    extended->source = std::move(image);
    extended->crop = crop;
    extended->destination = extended->crop;
    dcImages[n] = std::move(extended);
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
    if (alpha <= 0) {
      BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
      return;
    }
    const SDL_Rect requested = sr ? *sr : SDL_Rect{0, 0, source.w, source.h};
    const SDL_Rect bounds{0, 0, source.w, source.h};
    SDL_Rect crop{};
    if (!SDL_GetRectIntersection(&requested, &bounds, &crop)) {
      BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
      return;
    }
    const SDL_Rect destination{(dr ? dr->x : 0) + crop.x - requested.x,
                               (dr ? dr->y : 0) + crop.y - requested.y, crop.w,
                               crop.h};
    for (const auto &[id, pixels] : dc) {
      if (pixels.get() != &dest)
        continue;
      auto result = std::make_shared<DCImage>();
      result->kind = DCImage::Kind::Copy;
      result->width = dest.w;
      result->height = dest.h;
      result->crop = crop;
      result->destination = destination;
      result->alpha = alpha;
      result->mask = mask;
      result->previous = dcImage(id);
      for (const auto &[name, graphic] : graphics)
        if (graphic.surface.get() == &source) {
          result->source = std::make_shared<DCImage>();
          result->source->kind = DCImage::Kind::Graphic;
          result->source->graphic = name;
          result->source->crop = crop;
          result->source->width = crop.w;
          result->source->height = crop.h;
          result->crop = {0, 0, crop.w, crop.h};
          break;
        }
      if (!result->source)
        for (const auto &[sourceID, pixels] : dc)
          if (pixels.get() == &source) {
            result->source = dcImage(sourceID);
            break;
          }
      if (!result->source && zoom && zoom->source.get() == &source)
        result->source = zoom->image;
      if (!result->source)
        result->source = pixelImage(source);
      if (alpha >= 255 && destination.x <= 0 && destination.y <= 0 &&
          destination.x + destination.w >= dest.w &&
          destination.y + destination.h >= dest.h) {
        bool opaque = !mask || source.format == SDL_PIXELFORMAT_RGBX32;
        if (mask && source.format == SDL_PIXELFORMAT_RGBA32) {
          opaque = true;
          for (int y = crop.y; opaque && y < crop.y + crop.h; ++y) {
            const auto *row =
                static_cast<const Uint8 *>(source.pixels) + y * source.pitch;
            for (int x = crop.x; x < crop.x + crop.w; ++x)
              if (row[x * 4 + 3] != 255) {
                opaque = false;
                break;
              }
          }
        }
        if (opaque)
          result->previous.reset();
      }
      dcImages[id] = std::move(result);
      break;
    }
    BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
  }
  std::shared_ptr<DCImage> pixelImage(SurfaceRef pixels) {
    auto image = std::make_shared<DCImage>();
    image->width = pixels.w;
    image->height = pixels.h;
    image->native.reset(SDL_DuplicateSurface(&pixels));
    if (!image->native)
      throw std::runtime_error(SDL_GetError());
    return image;
  }
  std::shared_ptr<DCImage> dcImage(int id) {
    auto &image = dcImages[id];
    if (!image)
      image = pixelImage(surface(id));
    return image;
  }
  Graphic::Raster &prepareRaster(Graphic &g, const SDL_Rect &crop) {
    auto &cached = g.crops[{crop.x, crop.y, crop.w, crop.h}];
    auto &renderer = engine->GetRenderer();
    if (!cached.original) {
      SurfacePtr pixels(
          SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
      if (!pixels)
        throw std::runtime_error(SDL_GetError());
      BlitSurfaceOpacity(*g.surface, *pixels, &crop, nullptr, 255, false);
      cached.original.reset(
          SDL_CreateTextureFromSurface(&renderer, pixels.get()));
      if (!cached.original ||
          !SDL_SetTextureScaleMode(cached.original.get(), SDL_SCALEMODE_LINEAR))
        throw std::runtime_error(SDL_GetError());
      ++renderStats.textureUploads;
      ++renderStats.originalImageUploads;
    }
    if (presentationScale() > 1 && !cached.filtered) {
      if (!SDL_SetTextureAlphaMod(cached.original.get(), 255))
        throw std::runtime_error(SDL_GetError());
      cached.filtered =
          engine->GetUpscaler().scaled(*cached.original, imageUpscaling.factor);
      ++renderStats.filteredPatterns;
    }
    return cached;
  }
  TextureRef dcTexture(const std::shared_ptr<DCImage> &image,
                       SurfaceRef native) {
    refreshImageUpscaling();
    if (image->texture && image->generation == dcGeneration)
      return *image->texture;
    image->raster.reset();
    auto &renderer = engine->GetRenderer();
    TexturePtr original(SDL_CreateTextureFromSurface(&renderer, &native));
    if (!original)
      throw std::runtime_error(SDL_GetError());
    ++renderStats.textureUploads;
    image->texture =
        engine->GetUpscaler().scaled(*original, imageUpscaling.factor);
    ++renderStats.filteredDCFrames;
    const int scale = presentationScale();
    if (image->texture->w != native.w * scale ||
        image->texture->h != native.h * scale) {
      TexturePtr display(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET,
                                           native.w * scale, native.h * scale));
      struct Restore {
        SDL_Renderer &renderer;
        SDL_Texture *target;
        ~Restore() { SDL_SetRenderTarget(&renderer, target); }
      } restore{renderer, SDL_GetRenderTarget(&renderer)};
      if (!display ||
          !SDL_SetTextureBlendMode(image->texture.get(), SDL_BLENDMODE_NONE) ||
          !SDL_SetTextureScaleMode(image->texture.get(),
                                   SDL_SCALEMODE_LINEAR) ||
          !SDL_SetRenderTarget(&renderer, display.get()) ||
          !SDL_RenderTexture(&renderer, image->texture.get(), nullptr, nullptr))
        throw std::runtime_error(SDL_GetError());
      image->texture = std::move(display);
    }
    if (!SDL_SetTextureScaleMode(image->texture.get(), SDL_SCALEMODE_LINEAR) ||
        !SDL_SetTextureBlendMode(image->texture.get(), SDL_BLENDMODE_NONE))
      throw std::runtime_error(SDL_GetError());
    image->generation = dcGeneration;
    for (const auto &child : {image->source, image->previous}) {
      if (!child || (zoom && zoom->image == child))
        continue;
      const bool resident =
          std::any_of(dcImages.begin(), dcImages.end(),
                      [&](const auto &entry) { return entry.second == child; });
      if (!resident) {
        child->raster.reset();
        child->texture.reset();
      }
    }
    return *image->texture;
  }
  SurfaceRef dcRaster(const std::shared_ptr<DCImage> &image,
                      SurfaceRef native) {
    auto &texture = dcTexture(image, native);
    if (!image->raster) {
      auto &renderer = engine->GetRenderer();
      struct Restore {
        SDL_Renderer &renderer;
        SDL_Texture *target;
        ~Restore() { SDL_SetRenderTarget(&renderer, target); }
      } restore{renderer, SDL_GetRenderTarget(&renderer)};
      if (!SDL_SetRenderTarget(&renderer, &texture))
        throw std::runtime_error(SDL_GetError());
      image->raster.reset(SDL_RenderReadPixels(&renderer, nullptr));
      if (!image->raster)
        throw std::runtime_error(SDL_GetError());
      if (image->raster->format != SDL_PIXELFORMAT_RGBA32) {
        SurfacePtr converted(
            SDL_ConvertSurface(image->raster.get(), SDL_PIXELFORMAT_RGBA32));
        if (!converted)
          throw std::runtime_error(SDL_GetError());
        image->raster = std::move(converted);
      }
    }
    return *image->raster;
  }
  int presentationScale() const {
    const auto &settings = engine->GetUpscaler().getSettings();
    return settings.enabled() ? int(std::ceil(settings.factor)) : 1;
  }
  void refreshImageUpscaling() {
    const auto &settings = engine->GetUpscaler().getSettings();
    if (settings.backend == imageUpscaling.backend &&
        settings.preset == imageUpscaling.preset &&
        settings.factor == imageUpscaling.factor &&
        settings.shaderFiles == imageUpscaling.shaderFiles)
      return;
    for (auto &[id, g] : graphics) {
      g.scaledPatterns.clear();
      for (auto &[crop, raster] : g.crops) {
        raster.filtered.reset();
        raster.display.reset();
      }
    }
    ++dcGeneration;
    zoomTexture.reset();
    imageUpscaling = settings;
    objectTextTextures.clear();
  }
  SDL_Rect patternRect(const Graphic &g, size_t pattern) const {
    if (g.image.regions.empty())
      return {0, 0, int(g.image.width), int(g.image.height)};
    const auto &r = g.image.regions.at(pattern);
    return {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
  }
  TextureRef patternTexture(Graphic &g, size_t pattern) {
    const auto crop = patternRect(g, pattern);
    auto &cached = g.crops.at({crop.x, crop.y, crop.w, crop.h});
    return presentationScale() == 1 ? *cached.original
           : cached.display         ? *cached.display
                                    : *cached.filtered;
  }
  void prepareGraphic(Graphic &g) {
    refreshImageUpscaling();
    const int scale = presentationScale();
    const size_t count = std::max(size_t(1), g.image.regions.size());
    auto &renderer = engine->GetRenderer();
    for (size_t pattern = 0; pattern < count; ++pattern) {
      const auto crop = patternRect(g, pattern);
      auto &cached = prepareRaster(g, crop);
      if (scale == 1 || cached.display)
        continue;
      auto &texture = cached.filtered;
      if (texture->w != crop.w * scale || texture->h != crop.h * scale) {
        TexturePtr raster(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET,
                                            crop.w * scale, crop.h * scale));
        if (!raster)
          throw std::runtime_error(SDL_GetError());
        struct Restore {
          SDL_Renderer &renderer;
          SDL_Texture *target;
          ~Restore() { SDL_SetRenderTarget(&renderer, target); }
        } restore{renderer, SDL_GetRenderTarget(&renderer)};
        if (!SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE) ||
            !SDL_SetRenderTarget(&renderer, raster.get()) ||
            !SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr) ||
            !SDL_SetTextureScaleMode(raster.get(), SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(raster.get(), SDL_BLENDMODE_BLEND))
          throw std::runtime_error(SDL_GetError());
        cached.display = std::move(raster);
      }
    }
  }
  bool prepareDither(RendererRef renderer) {
    if (gpuDither)
      return *gpuDither;
    ditherTexture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, 8, 8));
    if (!ditherTexture)
      throw std::runtime_error(SDL_GetError());
    const auto maskBlend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
    gpuDither = SDL_SetTextureBlendMode(ditherTexture.get(), maskBlend);
    if (!*gpuDither) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "AIR renderer cannot blend dither masks; using native effect "
                  "pixels: %s",
                  SDL_GetError());
      ditherTexture.reset();
    } else if (!SDL_SetTextureScaleMode(ditherTexture.get(),
                                        SDL_SCALEMODE_NEAREST)) {
      throw std::runtime_error(SDL_GetError());
    }
    return *gpuDither;
  }
  void drawHardware(RendererRef r, int scale) {
    SDL_Texture *preparedBackground = nullptr;
    if (scale > 1)
      preparedBackground = &dcTexture(dcImage(0), surface(0));
    else {
      auto &background = surface(0);
      bool changed = !backgroundPixels || backgroundPixels->w != background.w ||
                     backgroundPixels->h != background.h;
      for (int y = 0; !changed && y < background.h; ++y)
        changed = std::memcmp(static_cast<Uint8 *>(backgroundPixels->pixels) +
                                  y * backgroundPixels->pitch,
                              static_cast<Uint8 *>(background.pixels) +
                                  y * background.pitch,
                              background.w * 4) != 0;
      if (changed) {
        if (!backgroundTexture || backgroundTexture->w != background.w ||
            backgroundTexture->h != background.h)
          backgroundTexture.reset(SDL_CreateTexture(
              &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
              background.w, background.h));
        if (!backgroundTexture ||
            !SDL_SetTextureBlendMode(backgroundTexture.get(),
                                     SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(backgroundTexture.get(),
                                     SDL_SCALEMODE_LINEAR) ||
            !SDL_UpdateTexture(backgroundTexture.get(), nullptr,
                               background.pixels, background.pitch))
          throw std::runtime_error(SDL_GetError());
        backgroundPixels.reset(SDL_DuplicateSurface(&background));
        if (!backgroundPixels)
          throw std::runtime_error(SDL_GetError());
        ++renderStats.textureUploads;
      }
      preparedBackground = backgroundTexture.get();
    }
    // Prepare all animation patterns before entering the composition target.
    for (const auto &[id, o] : fg)
      if (o.visible && !o.file.empty() && o.text.empty() &&
          !(o.width > 0 && o.height > 0))
        prepareGraphic(graphic(o.file));
    if (!compositionTexture || compositionTexture->w != 640 * scale ||
        compositionTexture->h != 480 * scale) {
      compositionTexture.reset(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                                 SDL_TEXTUREACCESS_TARGET,
                                                 640 * scale, 480 * scale));
      if (!compositionTexture ||
          !SDL_SetTextureScaleMode(compositionTexture.get(),
                                   SDL_SCALEMODE_LINEAR))
        throw std::runtime_error(SDL_GetError());
    }
    struct RestoreTarget {
      SDL_Renderer &renderer;
      SDL_Texture *target;
      SDL_Color color{};
      SDL_BlendMode blend = SDL_BLENDMODE_NONE;
      ~RestoreTarget() {
        SDL_SetRenderTarget(&renderer, target);
        SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b, color.a);
        SDL_SetRenderDrawBlendMode(&renderer, blend);
      }
    } restore{r, SDL_GetRenderTarget(&r)};
    if (!SDL_GetRenderDrawColor(&r, &restore.color.r, &restore.color.g,
                                &restore.color.b, &restore.color.a) ||
        !SDL_GetRenderDrawBlendMode(&r, &restore.blend) ||
        !SDL_SetRenderTarget(&r, compositionTexture.get()) ||
        !SDL_SetRenderScale(&r, 1, 1) || !SDL_SetRenderViewport(&r, nullptr) ||
        !SDL_SetRenderClipRect(&r, nullptr) ||
        !SDL_RenderTexture(&r, preparedBackground, nullptr, nullptr))
      throw std::runtime_error(SDL_GetError());
    for (const auto &[id, o] : fg) {
      if (!o.visible)
        continue;
      int x = o.x, y = o.y;
      for (const auto &adjustment : o.adjustments) {
        x += adjustment.x;
        y += adjustment.y;
      }
      const SDL_Rect clip{o.clip.x * scale, o.clip.y * scale, o.clip.w * scale,
                          o.clip.h * scale};
      if (!SDL_SetRenderClipRect(&r, o.clipped ? &clip : nullptr))
        throw std::runtime_error(SDL_GetError());
      if (o.width > 0 && o.height > 0) {
        const SDL_FRect area{float(x * scale), float(y * scale),
                             float(o.width * scale), float(o.height * scale)};
        if (!SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND) ||
            !SDL_SetRenderDrawColor(
                &r, o.colour.r, o.colour.g, o.colour.b,
                Uint8(o.colour.a * std::clamp(o.alpha, 0, 255) / 255)) ||
            !SDL_RenderFillRect(&r, &area))
          throw std::runtime_error(SDL_GetError());
      } else if (!o.file.empty() && o.text.empty()) {
        auto &g = graphic(o.file);
        const auto pattern = g.image.regions.empty()
                                 ? 0
                                 : o.currentPattern(g.image.regions.size());
        if (!g.image.regions.empty()) {
          x -= g.image.regions[pattern].originX;
          y -= g.image.regions[pattern].originY;
        }
        auto &texture = patternTexture(g, pattern);
        const SDL_FRect area{float(x * scale), float(y * scale),
                             float(texture.w), float(texture.h)};
        if (!SDL_SetTextureAlphaMod(&texture,
                                    Uint8(std::clamp(o.alpha, 0, 255))) ||
            !SDL_RenderTexture(&r, &texture, nullptr, &area))
          throw std::runtime_error(SDL_GetError());
      } else if (!o.text.empty()) {
        if (!textbox || !textbox->font)
          continue;
        const auto decoded =
            decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
        auto &texture = objectTextTextures[{decoded, scale}];
        if (!texture) {
          const auto oldSize = TTF_GetFontSize(textbox->font.get());
          if (!TTF_SetFontSize(textbox->font.get(), oldSize * scale))
            throw std::runtime_error(SDL_GetError());
          SurfacePtr pixels(
              TTF_RenderText_Blended(textbox->font.get(), decoded.data(),
                                     decoded.size(), {255, 255, 255, 255}));
          const bool restored = TTF_SetFontSize(textbox->font.get(), oldSize);
          if (!pixels || !restored)
            throw std::runtime_error(SDL_GetError());
          texture.reset(SDL_CreateTextureFromSurface(&r, pixels.get()));
          if (!texture)
            throw std::runtime_error(SDL_GetError());
          ++renderStats.textureUploads;
        }
        const SDL_FRect area{float(x * scale), float(y * scale),
                             float(texture->w), float(texture->h)};
        if (!SDL_SetTextureAlphaMod(texture.get(),
                                    Uint8(std::clamp(o.alpha, 0, 255))) ||
            !SDL_RenderTexture(&r, texture.get(), nullptr, &area))
          throw std::runtime_error(SDL_GetError());
      }
    }
    if (!SDL_SetRenderClipRect(&r, nullptr))
      throw std::runtime_error(SDL_GetError());
    if (flashDuration > 0 &&
        SDL_GetTicks() - flashStart < Uint64(flashDuration)) {
      const int step =
          std::min(16, int((SDL_GetTicks() - flashStart) * 16 / flashDuration));
      if (!SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND) ||
          !SDL_SetRenderDrawColor(&r, flashColour.r, flashColour.g,
                                  flashColour.b,
                                  Uint8((16 - step) * 255 / 16)) ||
          !SDL_RenderFillRect(&r, nullptr))
        throw std::runtime_error(SDL_GetError());
    }
    if (zoom) {
      if (scale == 1 && !zoomTexture) {
        zoomTexture.reset(SDL_CreateTextureFromSurface(&r, zoom->source.get()));
        if (!zoomTexture ||
            !SDL_SetTextureScaleMode(zoomTexture.get(), SDL_SCALEMODE_LINEAR))
          throw std::runtime_error(SDL_GetError());
        ++renderStats.textureUploads;
      }
      auto *preparedZoom = scale == 1 ? zoomTexture.get()
                                      : &dcTexture(zoom->image, *zoom->source);
      const auto elapsed = SDL_GetTicks() - zoom->start;
      const auto crop = zoom->animation.sample(double(elapsed));
      const SDL_FRect source{float(std::lround(crop.x) * scale),
                             float(std::lround(crop.y) * scale),
                             float(std::lround(crop.scaleX) * scale),
                             float(std::lround(crop.scaleY) * scale)};
      const SDL_FRect destination{float(zoom->destinationRect.x * scale),
                                  float(zoom->destinationRect.y * scale),
                                  float(zoom->destinationRect.w * scale),
                                  float(zoom->destinationRect.h * scale)};
      if (source.w > 0 && source.h > 0 &&
          !SDL_RenderTexture(&r, preparedZoom, &source, &destination))
        throw std::runtime_error(SDL_GetError());
      if (elapsed >= zoom->animation.duration()) {
        blit(*zoom->source, surface(0), &zoom->to, &zoom->destinationRect, 255,
             true);
        zoom.reset();
        zoomTexture.reset();
      }
    }
    if (transitionOld) {
      const auto elapsed = SDL_GetTicks() - transitionStart;
      if (elapsed < Uint64(transitionMs)) {
        if (!transitionTexture) {
          transitionTexture.reset(
              SDL_CreateTextureFromSurface(&r, transitionOld.get()));
          if (!transitionTexture ||
              !SDL_SetTextureScaleMode(transitionTexture.get(),
                                       SDL_SCALEMODE_LINEAR))
            throw std::runtime_error(SDL_GetError());
          ++renderStats.textureUploads;
        }
        const double progress = double(elapsed) / transitionMs;
        if (transitionStyle == 4 || transitionStyle == 5) {
          const int step = int(std::ceil(progress * 64));
          if (step != ditherStep || transitionStyle != ditherStyle) {
            std::array<Uint8, 8 * 8 * 4> mask{};
            for (int y = 0; y < 8; ++y)
              for (int x = 0; x < 8; ++x)
                mask[(y * 8 + x) * 4 + 3] =
                    transitionRevealsNewPixel(transitionStyle, progress, x, y)
                        ? 0
                        : 255;
            if (!SDL_UpdateTexture(ditherTexture.get(), nullptr, mask.data(),
                                   8 * 4))
              throw std::runtime_error(SDL_GetError());
            ditherStep = step;
            ditherStyle = transitionStyle;
          }
          if (!maskedTransitionTexture ||
              maskedTransitionTexture->w != compositionTexture->w ||
              maskedTransitionTexture->h != compositionTexture->h) {
            maskedTransitionTexture.reset(SDL_CreateTexture(
                &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                compositionTexture->w, compositionTexture->h));
            if (!maskedTransitionTexture ||
                !SDL_SetTextureBlendMode(maskedTransitionTexture.get(),
                                         SDL_BLENDMODE_BLEND))
              throw std::runtime_error(SDL_GetError());
          }
          if (!SDL_SetTextureAlphaMod(transitionTexture.get(), 255) ||
              !SDL_SetTextureBlendMode(transitionTexture.get(),
                                       SDL_BLENDMODE_NONE) ||
              !SDL_SetRenderTarget(&r, maskedTransitionTexture.get()) ||
              !SDL_RenderTexture(&r, transitionTexture.get(), nullptr,
                                 nullptr) ||
              !SDL_RenderTextureTiled(&r, ditherTexture.get(), nullptr,
                                      float(scale), nullptr) ||
              !SDL_SetRenderTarget(&r, compositionTexture.get()) ||
              !SDL_RenderTexture(&r, maskedTransitionTexture.get(), nullptr,
                                 nullptr))
            throw std::runtime_error(SDL_GetError());
        } else {
          const auto alpha = 255 - int(255.0 * progress);
          if (!SDL_SetTextureBlendMode(transitionTexture.get(),
                                       SDL_BLENDMODE_BLEND) ||
              !SDL_SetTextureAlphaMod(transitionTexture.get(), Uint8(alpha)) ||
              !SDL_RenderTexture(&r, transitionTexture.get(), nullptr, nullptr))
            throw std::runtime_error(SDL_GetError());
        }
      } else {
        transitionOld.reset();
        transitionTexture.reset();
      }
    }
    ++renderStats.gpuFrames;
  }
  void scaledBlit(SurfaceRef source, SurfaceRef dest, const SDL_Rect *src,
                  SDL_Rect target, int alpha = 255, bool mask = true) {
    const int width = src ? src->w : source.w;
    const int height = src ? src->h : source.h;
    if (width == target.w && height == target.h) {
      SDL_Rect point{target.x, target.y, 0, 0};
      blit(source, dest, src, &point, alpha, mask);
      return;
    }
    SurfacePtr resized(
        SDL_CreateSurface(target.w, target.h, SDL_PIXELFORMAT_RGBA32));
    SDL_BlendMode blend;
    if (!SDL_GetSurfaceBlendMode(&source, &blend))
      throw std::runtime_error(SDL_GetError());
    struct RestoreBlend {
      SDL_Surface &source;
      SDL_BlendMode blend;
      ~RestoreBlend() { SDL_SetSurfaceBlendMode(&source, blend); }
    } restore{source, blend};
    if (!resized || !SDL_SetSurfaceBlendMode(&source, SDL_BLENDMODE_NONE) ||
        !SDL_BlitSurfaceScaled(&source, src, resized.get(), nullptr,
                               SDL_SCALEMODE_LINEAR))
      throw std::runtime_error(SDL_GetError());
    SDL_Rect point{target.x, target.y, 0, 0};
    blit(*resized, dest, nullptr, &point, alpha, mask);
  }
  void drawObject(const Object &o, SurfaceRef dest, int scale) {
    int adjustX = 0, adjustY = 0;
    for (const auto &adjustment : o.adjustments) {
      adjustX += adjustment.x;
      adjustY += adjustment.y;
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
    if (o.clipped) {
      SDL_Rect clip{o.clip.x * scale, o.clip.y * scale, o.clip.w * scale,
                    o.clip.h * scale};
      SDL_SetSurfaceClipRect(&dest, &clip);
    }
    if (o.width > 0 && o.height > 0) {
      auto fill = blank(o.width, o.height);
      SDL_FillSurfaceRect(fill.get(), nullptr,
                          SDL_MapSurfaceRGBA(fill.get(), o.colour.r, o.colour.g,
                                             o.colour.b, o.colour.a));
      SDL_Rect dst{o.x + adjustX, o.y + adjustY, 0, 0};
      if (scale == 1)
        blit(*fill, dest, nullptr, &dst, o.alpha);
      else
        scaledBlit(
            *fill, dest, nullptr,
            {dst.x * scale, dst.y * scale, o.width * scale, o.height * scale},
            o.alpha);
      return;
    }
    if (!o.text.empty() && textbox && textbox->font) {
      auto decoded =
          decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
      const auto oldSize = TTF_GetFontSize(textbox->font.get());
      if (!TTF_SetFontSize(textbox->font.get(), oldSize * scale))
        throw std::runtime_error(SDL_GetError());
      SurfacePtr rendered(TTF_RenderText_Blended(textbox->font.get(),
                                                 decoded.data(), decoded.size(),
                                                 {255, 255, 255, 255}));
      TTF_SetFontSize(textbox->font.get(), oldSize);
      if (!rendered)
        throw std::runtime_error(SDL_GetError());
      SDL_Rect dst{(o.x + adjustX) * scale, (o.y + adjustY) * scale, 0, 0};
      blit(*rendered, dest, nullptr, &dst, o.alpha);
      return;
    }
    if (!o.file.empty()) {
      auto &g = graphic(o.file);
      SDL_Rect src{0, 0, int(g.image.width), int(g.image.height)},
          dst{o.x + adjustX, o.y + adjustY, 0, 0};
      const size_t pattern = g.image.regions.empty()
                                 ? 0
                                 : o.currentPattern(g.image.regions.size());
      if (!g.image.regions.empty()) {
        auto &r = g.image.regions.at(pattern);
        src = {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
        dst.x -= r.originX;
        dst.y -= r.originY;
      }
      if (scale == 1)
        blit(*g.surface, dest, &src, &dst, o.alpha);
      else {
        auto &pixels = g.scaledPatterns[pattern];
        if (!pixels) {
          prepareGraphic(g);
          auto &renderer = engine->GetRenderer();
          struct Restore {
            SDL_Renderer &renderer;
            SDL_Texture *target;
            ~Restore() { SDL_SetRenderTarget(&renderer, target); }
          } restore{renderer, SDL_GetRenderTarget(&renderer)};
          if (!SDL_SetRenderTarget(&renderer, &patternTexture(g, pattern)))
            throw std::runtime_error(SDL_GetError());
          pixels.reset(SDL_RenderReadPixels(&renderer, nullptr));
          if (!pixels)
            throw std::runtime_error(SDL_GetError());
          if (pixels->format != SDL_PIXELFORMAT_RGBA32) {
            SurfacePtr converted(
                SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32));
            if (!converted)
              throw std::runtime_error(SDL_GetError());
            pixels = std::move(converted);
          }
        }
        scaledBlit(*pixels, dest, nullptr,
                   {dst.x * scale, dst.y * scale, src.w * scale, src.h * scale},
                   o.alpha);
      }
    }
  }
  SurfacePtr compose(int scale) {
    refreshImageUpscaling();
    auto output = blank(640 * scale, 480 * scale);
    scaledBlit(scale == 1 ? surface(0) : dcRaster(dcImage(0), surface(0)),
               *output, nullptr, {0, 0, output->w, output->h}, 255, false);
    for (auto &[id, o] : fg)
      drawObject(o, *output, scale);
    if (flashDuration > 0 &&
        SDL_GetTicks() - flashStart < Uint64(flashDuration)) {
      auto colour = blank(output->w, output->h);
      SDL_FillSurfaceRect(colour.get(), nullptr,
                          SDL_MapSurfaceRGBA(colour.get(), flashColour.r,
                                             flashColour.g, flashColour.b,
                                             255));
      const int step =
          std::min(16, int((SDL_GetTicks() - flashStart) * 16 / flashDuration));
      blit(*colour, *output, nullptr, nullptr, (16 - step) * 255 / 16, true);
    }
    if (zoom) {
      const auto elapsed = SDL_GetTicks() - zoom->start;
      if (elapsed >= zoom->animation.duration()) {
        blit(*zoom->source, surface(0), &zoom->to, &zoom->destinationRect, 255,
             true);
        const SDL_Rect crop{zoom->to.x * scale, zoom->to.y * scale,
                            zoom->to.w * scale, zoom->to.h * scale};
        scaledBlit(
            scale == 1 ? *zoom->source : dcRaster(zoom->image, *zoom->source),
            *output, &crop,
            {zoom->destinationRect.x * scale, zoom->destinationRect.y * scale,
             zoom->destinationRect.w * scale, zoom->destinationRect.h * scale});
        zoom.reset();
      } else {
        const auto source = zoom->animation.sample(double(elapsed));
        SDL_Rect sourceCrop{int(std::lround(source.x)) * scale,
                            int(std::lround(source.y)) * scale,
                            int(std::lround(source.scaleX)) * scale,
                            int(std::lround(source.scaleY)) * scale};
        if (sourceCrop.w > 0 && sourceCrop.h > 0)
          scaledBlit(
              scale == 1 ? *zoom->source : dcRaster(zoom->image, *zoom->source),
              *output, &sourceCrop,
              {zoom->destinationRect.x * scale, zoom->destinationRect.y * scale,
               zoom->destinationRect.w * scale,
               zoom->destinationRect.h * scale});
      }
    }
    return output;
  }
  void drawSoftwareEffects(RendererRef r) {
    const int scale = presentationScale();
    auto output = compose(scale);
    if (transitionOld &&
        SDL_GetTicks() - transitionStart < Uint64(transitionMs)) {
      const double progress =
          double(SDL_GetTicks() - transitionStart) / transitionMs;
      auto old = blank(output->w, output->h);
      scaledBlit(*transitionOld, *old, nullptr, {0, 0, old->w, old->h}, 255,
                 false);
      if (transitionStyle == 4 || transitionStyle == 5) {
        for (int y = 0; y < output->h; ++y) {
          auto *oldRow = static_cast<Uint8 *>(old->pixels) + y * old->pitch;
          auto *newRow =
              static_cast<Uint8 *>(output->pixels) + y * output->pitch;
          for (int x = 0; x < output->w; ++x)
            if (!transitionRevealsNewPixel(transitionStyle, progress, x / scale,
                                           y / scale))
              std::memcpy(newRow + x * 4, oldRow + x * 4, 4);
        }
      } else {
        auto mixed = std::move(old);
        blit(*output, *mixed, nullptr, nullptr, int(255.0 * progress));
        output = std::move(mixed);
      }
    } else
      transitionOld.reset();
    bool changed = !canvasPixels || canvasPixels->w != output->w ||
                   canvasPixels->h != output->h;
    for (int y = 0; !changed && y < output->h; ++y)
      changed =
          std::memcmp(static_cast<Uint8 *>(canvasPixels->pixels) +
                          y * canvasPixels->pitch,
                      static_cast<Uint8 *>(output->pixels) + y * output->pitch,
                      output->w * 4) != 0;
    if (changed) {
      if (canvasTexture &&
          (canvasTexture->w != output->w || canvasTexture->h != output->h))
        canvasTexture.reset();
      if (!canvasTexture)
        canvasTexture.reset(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              output->w, output->h));
      if (!canvasTexture || !SDL_UpdateTexture(canvasTexture.get(), nullptr,
                                               output->pixels, output->pitch))
        throw std::runtime_error(SDL_GetError());
      canvasPixels = std::move(output);
    }
    ++renderStats.softwareEffectFrames;
  }
  void draw(RendererRef r) {
    refreshImageUpscaling();
    const bool softwareEffect =
        transitionOld && (transitionStyle == 4 || transitionStyle == 5) &&
        !prepareDither(r);
    if (softwareEffect)
      drawSoftwareEffects(r);
    else
      drawHardware(r, presentationScale());
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
    // Script rasters and sprite patterns are prepared separately, never twice.
    if (!SDL_RenderTexture(
            &r, softwareEffect ? canvasTexture.get() : compositionTexture.get(),
            nullptr, &destination))
      throw std::runtime_error(SDL_GetError());
    if (movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, {0, 0, 640, 480});
    }
    for (const auto &[id, field] : editFields) {
      SDL_Rect oldClip{};
      const bool clipped = SDL_RenderClipEnabled(&r);
      SDL_GetRenderClipRect(&r, &oldClip);
      SDL_Rect clip{int(field.area.x), int(field.area.y), int(field.area.w),
                    int(field.area.h)};
      if (clipped)
        SDL_GetRectIntersection(&oldClip, &clip, &clip);
      SDL_SetRenderClipRect(&r, &clip);
      engine->GetUI().panel(r, field.area, field.background);
      engine->GetUI().text(r, field.text + (focusedEdit == id ? "|" : ""),
                           field.area.x, field.area.y, field.area.w,
                           field.color);
      SDL_SetRenderClipRect(&r, clipped ? &oldClip : nullptr);
    }
    if (!error.empty()) {
      engine->GetUI().panel(r, {12, 15, 616, 155});
      engine->GetUI().text(r, error, 24, 26, 590);
    }
  }
  void play(PcmPlayer &p, const std::string &id, const std::string &channel,
            bool loop = false, int64_t start = 0, int64_t end = 0,
            int64_t loopStart = 0, int64_t offsetMilliseconds = 0) {
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
    if (offsetMilliseconds < 0)
      throw std::invalid_argument("Negative AIR music position");
    const auto offsetFrames = int64_t(offsetMilliseconds) * target.freq / 1000;
    if (offsetFrames > std::numeric_limits<int64_t>::max() - start)
      throw std::overflow_error("AIR music position is too large");
    clip.startFrame =
        std::min<size_t>(std::max<int64_t>(0, start + offsetFrames),
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
    if (f.loop) {
      if (f.duration <= 0)
        return f.to;
      const int distance = std::abs(f.to - f.from);
      if (distance == 0)
        return f.from;
      const Uint64 now = SDL_GetTicks();
      const double interval = double(f.duration) / distance;
      const double elapsed = double(now - f.lastCheck);
      f.value += (f.to >= f.from ? 1 : -1) * elapsed / interval;
      if ((f.to >= f.from && f.value >= f.to) ||
          (f.to < f.from && f.value <= f.to))
        f.value = f.from;
      f.lastCheck = now;
      return int(f.value);
    }
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
      textbox->autoActive = [this] { return autoMode; };
      textbox->skipActive = [this] { return skipMode || skipHeld; };
      textbox->controls.buttons = {
          {{584, 349, 31, 7}, [this] { textbox->concealed = true; }},
          {{616, 349, 12, 38}, [this] { GetVNBacklogRuntime(*engine).open(); }},
          {{616, 388, 12, 38}, [this] { advanceText(); }},
          {{616, 427, 12, 16},
           [this] {
             autoMode = !autoMode;
             if (autoMode)
               skipMode = false;
             autoDeadline = 0;
           }},
          {{616, 444, 12, 16}, [this] {
             skipMode = !skipMode;
             if (skipMode)
               autoMode = false;
           }}};
      choice = std::make_unique<AirSelection>(e, *assets);
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
      autoMode = skipMode = skipHeld = false;
      autoDeadline = 0;
      textbox->controls.reset();
      textbox->concealed = false;
      fg.clear();
      bg.clear();
      dc.clear();
      transitionOld.reset();
      zoom.reset();
      shake.clear();
      flashDuration = 0;
      editFields.clear();
      focusedEdit = -1;
      SDL_StopTextInput(&engine->GetWindow());
      deadline = 0;
      choice->hide();
      movie->stop();
      voice.Stop();
      bgm.Stop();
      musicID.clear();
      fadeDuration = fadeElapsed = 0;
      bgmGain = 1;
      sounds.clear();
      runtime.SetState(GameState::Menu);
      titleInput.reset();
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
    std::string dir = (Kotonoha::saveDirectory() / "saves").string();
    saves->open(dir, load, load ? "" : saveName);
    runtime.RemoveScene(saves.get());
    runtime.AddScene(saves.get());
  }
  void advanceText() {
    if (!pause)
      return;
    if (!textbox->isDialogueComplete())
      textbox->completeDialogue();
    else {
      readPages.insert(currentPage);
      pause = false;
      autoDeadline = 0;
      autoMode = false;
      skipMode = false;
      if (pauseKind != 205) {
        dialogue.clear();
        speakerName.clear();
        textbox->hide();
        voice.Stop();
      }
    }
  }
  // The title front-end idles after the native refresh command, so only then
  // is the script about to poll the mouse again.
  bool titleMenu() const {
    return vm.scenario() == 9032 && vm.index() == 860 &&
           runtime.GetState() == GameState::Menu && !pause &&
           !choice->isActive() && error.empty();
  }
  bool syscomAvailable() const {
    return std::any_of(syscomEntries.begin(), syscomEntries.end(),
                       [](Uint8 state) { return state != 0; });
  }
  // Other loops in SEEN 9032 poll intA and must stay pointer-only; the title
  // menu is the poll that writes the button register intF[1003].
  bool titlePoll(const libreallive::CommandElement &c) {
    if (vm.scenario() != 9032)
      return false;
    return Parameter(c, 2).GetIntegerReferenceIterator(vm.machine()) ==
           IntReferenceIterator(&vm.machine().memory(), 5, 1003);
  }
  bool event(EventRef e) {
    if (titleMenu() && titleInput.handle(e)) {
      mouseX = titleInput.mouseX();
      mouseY = titleInput.mouseY();
      return true;
    }
    if (syscomAvailable() && pause && e.type == SDL_EVENT_KEY_DOWN &&
        !e.key.repeat && e.key.key == SDLK_ESCAPE) {
      GetInGameMenuRuntime(*engine).open();
      return true;
    }
    if (pause && ShouldOpenBacklog(*engine, e)) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    // Controller Back/left shoulder maps to PAGEUP (see MapControllerEvent);
    // the AIR text box overrides the shared VNTextBox handler, so accept it
    // here as well.
    if (pause && textbox->isVisible() && e.type == SDL_EVENT_KEY_DOWN &&
        !e.key.repeat && e.key.key == SDLK_PAGEUP) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }

    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        e.type == SDL_EVENT_GAMEPAD_REMOVED)
      skipHeld = false;
    if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
        (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL)) {
      skipHeld = e.type == SDL_EVENT_KEY_DOWN;
      return true;
    }
    if (pause && textbox->isVisible()) {
      if (textbox->concealed) {
        if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             e.button.button == SDL_BUTTON_LEFT) ||
            (e.type == SDL_EVENT_KEY_DOWN &&
             (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN))) {
          textbox->concealed = false;
          return true;
        }
      } else if (textbox->handleEvent(e))
        return true;
      if (!engine->isStoryPaused() && e.type == SDL_EVENT_KEY_DOWN &&
          !e.key.repeat && e.key.key == SDLK_A) {
        autoMode = !autoMode;
        if (autoMode)
          skipMode = false;
        autoDeadline = 0;
        return true;
      }
      if (!engine->isStoryPaused() && e.type == SDL_EVENT_KEY_DOWN &&
          !e.key.repeat && e.key.key == SDLK_S) {
        skipMode = !skipMode;
        if (skipMode)
          autoMode = false;
        return true;
      }
    }
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
      if (syscomAvailable())
        GetInGameMenuRuntime(*engine).toggle();
      return true;
    }
    bool advance =
        (e.type == (pauseKind == 205 || e.button.which == SDL_TOUCH_MOUSEID
                        ? SDL_EVENT_MOUSE_BUTTON_UP
                        : SDL_EVENT_MOUSE_BUTTON_DOWN) &&
         e.button.button == SDL_BUTTON_LEFT) ||
        (e.type == SDL_EVENT_KEY_DOWN &&
         (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE));
    if (advance && pause) {
      advanceText();
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
      currentPage = {vm.scenario(), vm.index()};
      currentRead = readPages.count(currentPage) != 0;
      if (!currentRead)
        skipMode = false;
      autoDeadline = 0;
      textbox->concealed = false;
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
    static_cast<AirSelection &>(*choice).configure(selectionWindow);
    choice->setQuestion("", options, [this](int n, const std::string &) {
      vm.machine().set_store_register(n);
      choice->hide();
    });
    return true;
  }
  if (type == 1 && m == 4) {
    switch (op) {
    case 0:
      saveName =
          decodeText(str(c, 0), archive.Scenario(vm.scenario()).encoding());
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
    case 111:
    case 121: {
      const int target = value(c, 0);
      if (target < 0)
        throw std::runtime_error("Negative AIR timer wait target");
      const int timer = value(c, 1) + (op == 121 ? 256 : 0);
      const Uint64 elapsed = SDL_GetTicks() - timers[timer];
      if (Uint64(target) > elapsed)
        wait(int(Uint64(target) - elapsed));
      return true;
    }
    case 130:
      button1 = button2 = 0;
      return true;
    case 133: {
      // Keyboard/gamepad title input is injected at the native poll so it
      // cannot be lost to the script's own 1:4:130 register clear.
      const int synthetic = titlePoll(c) ? titleInput.poll() : -1;
      assign(c, 0, int(mouseX));
      assign(c, 1, int(mouseY));
      assign(c, 2, synthetic >= 0 ? synthetic : button1);
      assign(c, 3, button2);
      button1 = button1 == 2 ? 0 : button1;
      button2 = button2 == 2 ? 0 : button2;
      return true;
    }
    case 204:
      SDL_ShowCursor();
      return true;
    case 205:
      SDL_HideCursor();
      return true;
    case 332:
      machine.set_store_register(skipMode);
      return true;
    case 333:
    case 334:
      skipMode = op == 333;
      return true;
    case 350:
      machine.set_store_register(ctrlSkipEnabled);
      return true;
    case 353:
    case 354:
      machine.set_store_register(0);
      return true;
    case 351:
    case 352:
      ctrlSkipEnabled = op == 351;
      return true;
    case 410:
      for (int i = 0; i < c.GetParamCount(); ++i)
        assign(c, i, assets->settings().integer("WINDOW_ATTR", 0, i));
      return true;
    case 600:
    case 601:
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
        frames[key].loop = op == 601;
        frames[key].value = frames[key].from;
        frames[key].lastCheck = frames[key].start;
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
        if (f != frames.end()) {
          active |= f->second.loop && f->second.duration > 0;
          active |=
              !f->second.loop && f->second.duration > 0 &&
              SDL_GetTicks() - f->second.start < Uint64(f->second.duration);
        }
      }
      machine.set_store_register(active);
      return true;
    }
    case 3000:
    case 3001:
      openSaves(op == 3001);
      refreshRequested = true;
      return true;
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
      if (c.GetParamCount() == 0) {
        syscomEntries.fill(op == 1211 ? 1 : 0);
        return true;
      }
      for (int i = 0; i < c.GetParamCount(); ++i) {
        const int entry = value(c, i);
        if (entry < 0 || entry >= int(syscomEntries.size()))
          throw std::out_of_range("AIR system menu entry");
        syscomEntries[entry] = op == 1211 ? 1 : 0;
      }
      return true;
    case 3501:
    case 3502:
      return true;
    // The port's script-driven title UI replaces these original-only hooks.
    case 300:
    case 1231:
    case 3503:
      return true;
    case 1502:
      machine.set_store_register(0);
      return true;
    case 1504:
      machine.set_store_register(0);
      return true;
    case 2375:
      machine.set_store_register(
          (SDL_GetWindowFlags(&engine->GetWindow()) & SDL_WINDOW_FULLSCREEN)
              ? 1
              : 0);
      return true;
    case 1700: {
      EditField field;
      field.area = {float(value(c, 1)), float(value(c, 2)), float(value(c, 3)),
                    float(value(c, 4))};
      field.color = {Uint8(value(c, 6)), Uint8(value(c, 7)), Uint8(value(c, 8)),
                     255};
      field.background = {Uint8(value(c, 9, 255)), Uint8(value(c, 10, 255)),
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
    transitionOld = compose(presentationScale());
    transitionTexture.reset();
    transitionStart = SDL_GetTicks();
    std::ostringstream key;
    key << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
    transitionMs = assets->settings().integer(key.str(), 0, 6);
    transitionStyle = assets->settings().integer(key.str(), 0, 7);
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
    if (op == 1002) {
      auto name = str(c, 0);
      for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
          ch += 32;
      auto track = audio.tracks.find(name);
      if (track == audio.tracks.end())
        throw std::runtime_error("AIR BGM missing: " + name);
      std::string id = "BGM/" + track->second.file;
      if (!assets->contains(id))
        id += ".NWA";
      play(bgm, id, "BGM", false, track->second.from, track->second.to,
           track->second.loop, value(c, 1));
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
      play(voice, p->generic_string(), "Voice");
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
      play(*p, id, "PCM", op == 2);
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
  if (type == 1 && (m == 73 || m == 74) && op == 2001) {
    const int object = value(c, 0);
    if (object < 0 || object > 30000)
      throw std::out_of_range("AIR animation object");
    const int frameMs = value(c, 1);
    if (frameMs <= 0)
      throw std::out_of_range("AIR animation frame duration");
    auto &objects = m == 73 ? fg : bg;
    auto found = objects.find(object);
    if (found != objects.end() && !found->second.file.empty()) {
      auto &o = found->second;
      o.frameMs = frameMs;
      o.animationElapsed = 0;
    }
    return true;
  }
  if (type == 1 && (m == 71 || m == 72)) {
    auto &objects = m == 71 ? fg : bg;
    auto &o = objects[value(c, 0)];
    if (op == 1000) {
      o.width = o.height = 0;
      o.frameMs = 0;
      o.animationElapsed = 0;
      o.file = str(c, 1);
      prepareGraphic(graphic(o.file));
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
      o.frameMs = 0;
      o.animationElapsed = 0;
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
      o.frameMs = 0;
      o.animationElapsed = 0;
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
      o.adjustments[slot] = {value(c, 2), value(c, 3)};
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
          auto &r =
              g.image.regions.at(o.currentPattern(g.image.regions.size()));
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
    const int first = value(c, 0);
    const int last = c.GetParamCount() > 1 ? value(c, 1) : first;
    if (first < 0 || last < 0 || first > 30000 || last > 30000)
      throw std::out_of_range("AIR object copy range");
    for (int id = first; id <= last; ++id)
      bg[id] = fg[id];
    return true;
  }
  if (type == 1 && m == 60 && op == 1 && over == 0) {
    // AIR's objEraseWipeCopy(0) matches the port's default promotion behavior.
    value(c, 0);
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
  if (type == 1 && m == 31 && op == 0) {
    refreshRequested = true;
    return true;
  }
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
      transitionOld = compose(presentationScale());
      transitionTexture.reset();
      transitionStart = SDL_GetTicks();
      std::ostringstream selector;
      selector << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
      transitionMs = assets->settings().integer(selector.str(), 0, 6);
      transitionStyle = assets->settings().integer(selector.str(), 0, 7);
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
    if (op == 402) {
      if (c.GetParamCount() < 14)
        throw std::runtime_error("Malformed AIR grpZoom");
      const auto bounds = [&](int first) {
        SDL_Rect rect{value(c, first), value(c, first + 1),
                      value(c, first + 2) - value(c, first),
                      value(c, first + 3) - value(c, first + 1)};
        if (rect.w <= 0 || rect.h <= 0)
          throw std::runtime_error("Invalid AIR grpZoom rectangle");
        return rect;
      };
      const auto from = bounds(0);
      const auto to = bounds(4);
      const int sourceDC = value(c, 8);
      if (sourceDC < 0 || sourceDC > 15)
        throw std::out_of_range("AIR grpZoom source DC");
      const auto sourceRect = bounds(9);
      auto destinationRect = sourceRect;
      const int duration = std::max(0, value(c, 13));
      SurfacePtr source(SDL_DuplicateSurface(&surface(sourceDC)));
      if (!source)
        throw std::runtime_error(SDL_GetError());
      if (!duration) {
        blit(surface(sourceDC), surface(0), &to, &destinationRect, 255, true);
        return true;
      }
      zoom = ZoomEffect{
          std::move(source),
          dcImage(sourceDC),
          to,
          destinationRect,
          TransformAnimation(
              {float(from.x), float(from.y), float(from.w), float(from.h)},
              {float(to.x), float(to.y), float(to.w), float(to.h)}, duration),
          SDL_GetTicks()};
      zoomTexture.reset();
      wait(duration);
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
    if (op == 300 || op == 301 || op == 1300 || op == 1301) {
      const int id = value(c, 4);
      auto &s = surface(id);
      SDL_Rect rect{value(c, 0), value(c, 1), value(c, 2), value(c, 3)};
      if (op < 1000) {
        rect.w -= rect.x;
        rect.h -= rect.y;
      }
      auto image = std::make_shared<DCImage>();
      image->kind =
          op == 301 || op == 1301 ? DCImage::Kind::Gray : DCImage::Kind::Invert;
      image->width = s.w;
      image->height = s.h;
      image->crop = rect;
      image->previous = dcImage(id);
      dcImages[id] = std::move(image);
      for (int y = std::max(0, rect.y); y < std::min(s.h, rect.y + rect.h); ++y)
        for (int x = std::max(0, rect.x); x < std::min(s.w, rect.x + rect.w);
             ++x) {
          auto *p = static_cast<Uint8 *>(s.pixels) + y * s.pitch + x * 4;
          if (op == 301 || op == 1301) {
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
      auto image = std::make_shared<DCImage>();
      image->kind = DCImage::Kind::Copy;
      image->width = dest.w;
      image->height = dest.h;
      image->crop = sr;
      image->destination = dr;
      image->source = dcImage(value(c, 4));
      image->previous = dcImage(value(c, 9));
      dcImages[value(c, 9)] = std::move(image);
      SDL_SetSurfaceBlendMode(&source, SDL_BLENDMODE_NONE);
      SDL_BlitSurfaceScaled(&source, &sr, &dest, &dr, SDL_SCALEMODE_LINEAR);
      return true;
    }

    if (op == 70 || op == 1050 || op == 1051) {
      auto &g = graphic(str(c, 0));
      int n = value(c, 1);
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid AIR DC");
      if (n > 1 && over < 2) {
        dc[n] = blank(g.image.width, g.image.height);
        dcImages.erase(n);
      }
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
        dcImages.erase(1);
        return true;
      }
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid AIR DC");
      dc.erase(n);
      dcImages.erase(n);
      return true;
    }
    if (op == 31) {
      auto &s = surface(value(c, 0));
      SDL_FillSurfaceRect(
          &s, nullptr,
          SDL_MapSurfaceRGBA(&s, value(c, 1), value(c, 2), value(c, 3), 255));
      dcImages.erase(value(c, 0));
      return true;
    }
    if (op == 1053 || op == 73 || op == 74) {
      std::string name = str(c, 0);
      if (name == "???")
        name = lastGraphic;
      transitionOld = compose(presentationScale());
      transitionTexture.reset();
      transitionStart = SDL_GetTicks();
      std::ostringstream selKey;
      selKey << "SEL." << std::setw(3) << std::setfill('0') << value(c, 1);
      const auto sel = selKey.str();
      transitionMs =
          over == 4 ? value(c, 7) : assets->settings().integer(sel, 500, 6);
      transitionStyle =
          over == 4 ? value(c, 8) : assets->settings().integer(sel, 0, 7);
      wait(transitionMs);
      // "?" reuses DC1; copying DC0 here would destroy that prepared image.
      if (name != "?")
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
        blit(*g.surface, surface(1), &sr, &dst,
             over == 4 ? value(c, 15, 255) : 255, op == 74);
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
      })) {
    if (type == 1 && m == 4 &&
        ((op >= 2230 && op <= 2233) || (op >= 2240 && op <= 2243))) {
      struct Bank {
        const char *channel;
        int volume, enabled;
        const char *volumeKey;
        const char *enabledKey;
      };
      const Bank banks[]{{"BGM", 2330, 2340, "BGM_VOLUME", "BGM_ENABLED"},
                         {"Voice", 2331, 2341, "KOE_VOLUME", "KOE_ENABLED"},
                         {"PCM", 2332, 2342, "PCM_VOLUME", "PCM_ENABLED"},
                         {"Se", 2333, 2343, "SE_VOLUME", "SE_ENABLED"}};
      for (const auto &bank : banks) {
        const auto &values = machine.memory().integers;
        auto setting = [&](int key, const char *name, int fallback) {
          auto found = values.find({26, key});
          return found == values.end()
                     ? assets->settings().integer(name, fallback)
                     : found->second;
        };
        auto channel = engine->GetAudio().channel(bank.channel);
        channel->setVolume(
            std::clamp(setting(bank.volume, bank.volumeKey, 255), 0, 255) /
            255.0f);
        channel->setMuted(setting(bank.enabled, bank.enabledKey, 1) == 0);
      }
    }
    if (type == 1 && m == 4 && op == 2275) {
      auto display = engine->GetSettings();
      display.mode =
          value(c, 0) ? WindowMode::Borderless : WindowMode::Windowed;
      engine->applySettings(display);
    }
    return true;
  }
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
AirRuntime::RenderStats AirRuntime::renderStats() const {
  return impl->renderStats;
}
void AirRuntime::shutdown() {
  impl->bgm.Stop();
  impl->voice.Stop();
  impl->sounds.clear();
  if (impl->movie)
    impl->movie->stop();
  impl->graphics.clear();
  impl->dcImages.clear();
  impl->compositionTexture.reset();
  impl->backgroundTexture.reset();
  impl->zoomTexture.reset();
  impl->transitionTexture.reset();
  impl->ditherTexture.reset();
  impl->maskedTransitionTexture.reset();
  impl->objectTextTextures.clear();
  ClearScenes();
  impl->textbox.reset();
  impl->choice.reset();
  impl->saves.reset();
  impl->stage.reset();
  impl->movie.reset();
}
void AirRuntime::initMenu(Engine &e) {
  impl->bind(e);
  impl->titleInput.reset();
}
void AirRuntime::initGameplay(Engine &e) {
  impl->bind(e);
  impl->titleInput.reset();
}
void AirRuntime::update() {
  Runtime::update();
  auto &i = *impl;
  if (!i.engine)
    return;
  bool paused = isStoryPaused();
  if (paused)
    i.button1 = i.button2 = 0;
  const auto audioNow = SDL_GetTicks();
  const auto delta = i.audioTick ? audioNow - i.audioTick : 0;
  i.audioTick = audioNow;
  if (!paused) {
    const auto animate = [&](std::map<int, Object> &objects) {
      for (auto &[id, o] : objects)
        if (o.frameMs > 0)
          o.animationElapsed += delta;
    };
    animate(i.fg);
    animate(i.bg);
  }
  if (paused && i.autoDeadline)
    i.autoDeadline += delta;
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
  if (!paused && i.pause && !i.textbox->concealed) {
    if ((i.skipMode && i.currentRead) || (i.skipHeld && i.ctrlSkipEnabled)) {
      i.textbox->completeDialogue();
      i.advanceText();
    } else if (i.autoMode && i.textbox->isDialogueComplete() &&
               !i.voice.Playing()) {
      if (!i.autoDeadline) {
        const auto &values = i.vm.machine().memory().integers;
        auto get = [&](int key, const char *name, int fallback) {
          auto entry = values.find({26, key});
          return entry == values.end()
                     ? i.assets->settings().integer(name, fallback)
                     : entry->second;
        };
        const auto duration =
            std::max(0, get(2352, "MESSAGE_KEY_WAIT_TIME", 1000)) +
            std::max(0, get(2351, "AUTO_CHAR_TIME", 30)) *
                VNTextBoxUtility::CountUtf8Chars(i.dialogue);
        i.autoDeadline = SDL_GetTicks() + duration;
      }
      if (SDL_GetTicks() >= i.autoDeadline)
        i.advanceText();
    }
  }
  if (paused || i.pause || i.choice->isActive() || !i.error.empty() ||
      SDL_GetTicks() < i.deadline)
    return;
  if (i.movie->isOpen()) {
    if (!i.movie->finished())
      return;
    i.movie->stop();
  }
  i.refreshRequested = false;
  for (int n = 0;
       n < 3000 && !i.refreshRequested && !i.pause && !i.choice->isActive() &&
       i.error.empty() && SDL_GetTicks() >= i.deadline && !i.movie->isOpen();
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
  out << "AIR-PRESENTATION-5\n"
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
    for (auto &[id, o] : objects) {
      out << id << ' ' << std::quoted(o.file) << ' ' << std::quoted(o.text)
          << ' ' << o.x << ' ' << o.y << ' ' << o.pattern << ' ' << o.alpha
          << ' ' << o.visible << ' ' << o.width << ' ' << o.height << ' '
          << int(o.colour.r) << ' ' << int(o.colour.g) << ' ' << int(o.colour.b)
          << ' ' << int(o.colour.a) << ' ' << o.clipped << ' ' << o.clip.x
          << ' ' << o.clip.y << ' ' << o.clip.w << ' ' << o.clip.h << ' '
          << o.frameMs << ' ' << o.animationElapsed;
      for (const auto &adjustment : o.adjustments)
        out << ' ' << adjustment.x << ' ' << adjustment.y;
      out << '\n';
    }
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
  out << std::quoted(i.saveName) << '\n';
  std::map<const DCImage *, size_t> indices;
  std::vector<std::shared_ptr<DCImage>> images;
  const auto collect = [&](const auto &self,
                           const std::shared_ptr<DCImage> &image) -> void {
    if (!image || indices.count(image.get()))
      return;
    self(self, image->source);
    self(self, image->previous);
    images.push_back(image);
    indices.emplace(image.get(), images.size());
  };
  for (const auto &[id, pixels] : i.dc)
    collect(collect, i.dcImage(id));
  out << images.size() << '\n';
  for (const auto &image : images) {
    const auto index = [&](const std::shared_ptr<DCImage> &child) {
      return child ? indices.at(child.get()) : 0;
    };
    std::string pixels;
    if (image->native)
      for (int y = 0; y < image->native->h; ++y)
        pixels.append(static_cast<const char *>(image->native->pixels) +
                          y * image->native->pitch,
                      size_t(image->native->w) * 4);
    out << int(image->kind) << ' ' << image->width << ' ' << image->height
        << ' ' << image->alpha << ' ' << image->mask << ' ' << image->crop.x
        << ' ' << image->crop.y << ' ' << image->crop.w << ' ' << image->crop.h
        << ' ' << image->destination.x << ' ' << image->destination.y << ' '
        << image->destination.w << ' ' << image->destination.h << ' '
        << index(image->source) << ' ' << index(image->previous) << ' '
        << std::quoted(image->graphic) << ' ' << std::quoted(pixels) << '\n';
  }
  out << i.dc.size() << '\n';
  for (const auto &[id, pixels] : i.dc)
    out << id << ' ' << indices.at(i.dcImage(id).get()) << '\n';
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
      (version != "AIR-PRESENTATION-1" && version != "AIR-PRESENTATION-2" &&
       version != "AIR-PRESENTATION-3" && version != "AIR-PRESENTATION-4" &&
       version != "AIR-PRESENTATION-5") ||
      (pauseKind != 17 && pauseKind != 205) || position < 0 ||
      !std::isfinite(position))
    invalid();
  float gain = 1, fadeFrom = 1, fadeTo = 1;
  int fadeElapsed = 0, fadeDuration = 0;
  bool fadeStops = false;
  if (version != "AIR-PRESENTATION-1" &&
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
      if ((version == "AIR-PRESENTATION-3" || version == "AIR-PRESENTATION-4" ||
           version == "AIR-PRESENTATION-5") &&
          (!(in >> o.frameMs >> o.animationElapsed) || o.frameMs < 0))
        invalid();
      if (version == "AIR-PRESENTATION-4" || version == "AIR-PRESENTATION-5")
        for (auto &adjustment : o.adjustments)
          if (!(in >> adjustment.x >> adjustment.y))
            invalid();
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
  std::string saveName;
  std::map<int, std::shared_ptr<DCImage>> dcImages;
  in >> std::ws;
  if (version == "AIR-PRESENTATION-5" && in.eof())
    invalid();
  if (!in.eof()) {
    if (!(in >> std::quoted(saveName)))
      invalid();
    if (version == "AIR-PRESENTATION-5") {
      size_t count;
      if (!(in >> count) || count > 65536)
        invalid();
      std::vector<std::shared_ptr<DCImage>> images(1);
      for (size_t n = 0; n < count; ++n) {
        auto image = std::make_shared<DCImage>();
        int kind;
        size_t source, previous;
        std::string pixels;
        if (!(in >> kind >> image->width >> image->height >> image->alpha >>
              image->mask >> image->crop.x >> image->crop.y >> image->crop.w >>
              image->crop.h >> image->destination.x >> image->destination.y >>
              image->destination.w >> image->destination.h >> source >>
              previous >> std::quoted(image->graphic) >> std::quoted(pixels)) ||
            kind < 0 || kind > int(DCImage::Kind::Gray) || image->width <= 0 ||
            image->height <= 0 || image->width > 8192 || image->height > 8192 ||
            image->alpha < 0 || image->alpha > 255 || source >= images.size() ||
            previous >= images.size())
          invalid();
        image->kind = DCImage::Kind(kind);
        image->source = images[source];
        image->previous = images[previous];
        if ((image->kind == DCImage::Kind::Copy && !image->source) ||
            ((image->kind == DCImage::Kind::Gray ||
              image->kind == DCImage::Kind::Invert) &&
             !image->previous) ||
            (image->kind == DCImage::Kind::Pixels && pixels.empty()))
          invalid();
        if (image->kind == DCImage::Kind::Graphic) {
          if (image->graphic.empty() || image->crop.w != image->width ||
              image->crop.h != image->height)
            invalid();
          i.graphic(image->graphic);
        }
        if (!pixels.empty()) {
          if (pixels.size() != size_t(image->width) * image->height * 4)
            invalid();
          total += pixels.size();
          if (total > 256 * 1024 * 1024)
            invalid();
          image->native = blank(image->width, image->height);
          for (int y = 0; y < image->height; ++y)
            std::memcpy(static_cast<char *>(image->native->pixels) +
                            y * image->native->pitch,
                        pixels.data() + size_t(y) * image->width * 4,
                        size_t(image->width) * 4);
        }
        images.push_back(std::move(image));
      }
      if (!(in >> count) || count != dc.size())
        invalid();
      for (size_t n = 0; n < count; ++n) {
        int id;
        size_t index;
        if (!(in >> id >> index) || !dc.count(id) || index == 0 ||
            index >= images.size() || images[index]->width != dc.at(id)->w ||
            images[index]->height != dc.at(id)->h ||
            !dcImages.emplace(id, images[index]).second)
          invalid();
      }
    }
    in >> std::ws;
    if (!in.eof())
      invalid();
  }
  if (!music.empty() && !i.assets->contains(music))
    invalid();
  i.vm.restore(state);
  if (version != "AIR-PRESENTATION-4" && version != "AIR-PRESENTATION-5") {
    const auto restoreAdjustments = [&](std::map<int, Object> &objects) {
      for (auto &[id, o] : objects)
        for (size_t slot = 0; slot < o.adjustments.size(); ++slot) {
          o.adjustments[slot].x = i.vm.machine().GetIntValue(
              libreallive::IntMemRef(27, 0, id * 32 + int(slot) * 2));
          o.adjustments[slot].y = i.vm.machine().GetIntValue(
              libreallive::IntMemRef(27, 0, id * 32 + int(slot) * 2 + 1));
        }
    };
    restoreAdjustments(fg);
    restoreAdjustments(bg);
  }
  if (saveName.empty()) {
    std::size_t index = 0;
    for (const auto &element : i.archive.Scenario(i.vm.scenario())) {
      if (index++ >= i.vm.index())
        break;
      auto *command =
          dynamic_cast<const libreallive::CommandElement *>(element.get());
      if (command && command->modtype() == 1 && command->module() == 4 &&
          command->opcode() == 0 && command->GetParamCount() > 0)
        saveName = decodeText(StringParameter(i.vm.machine(), *command, 0),
                              i.archive.Scenario(i.vm.scenario()).encoding());
    }
  }
  i.fg.swap(fg);
  i.bg.swap(bg);
  i.dc.swap(dc);
  i.dcImages = std::move(dcImages);
  i.dialogue = std::move(dialogue);
  i.speakerName = std::move(speaker);
  i.saveName = std::move(saveName);
  i.pause = true;
  i.pauseKind = pauseKind;
  i.deadline = 0;
  i.error.clear();
  i.transitionOld.reset();
  i.transitionTexture.reset();
  i.zoom.reset();
  i.zoomTexture.reset();
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

bool AirRuntime::onEvent(EventRef e) {
  if (!impl)
    return Runtime::onEvent(e);
  // Modal scenes translate controller input in their own context. Pre-mapping
  // would turn South into held skip while the pause/save menus are open and
  // would hide the history state from the backlog. Holding South while a modal
  // opens must also stop skipping in the background.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput()) {
      impl->skipHeld = false;
      return Runtime::onEvent(e);
    }
  const bool dialogue =
      impl->textbox && impl->textbox->isVisible() && !impl->textbox->concealed;
  return Runtime::onEvent(Kotonoha::MapControllerEvent(e, dialogue));
}
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
std::string AirRuntime::nodeClockKey() const {
  return std::to_string(impl->vm.scenario());
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
  impl->canvasTexture.reset();
  impl->canvasPixels.reset();
  impl->transitionOld.reset();
  impl->transitionTexture.reset();
  impl->zoom.reset();
  impl->zoomTexture.reset();
  if (impl->textbox)
    impl->textbox->hide();
}
bool AirRuntime::debugStep() {
  auto &i = *impl;
  try {
    return i.vm.step();
  } catch (const std::exception &e) {
    i.error = "AIR " + debugNode() + ": " + e.what();
    SDL_Log("%s", i.error.c_str());
    return false;
  }
}
SDL_FPoint AirRuntime::sceneSize(const Scene &s) const {
  return s.designSpace() ? SDL_FPoint{1280, 720} : SDL_FPoint{640, 480};
}
} // namespace Air
