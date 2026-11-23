#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/SplashChainloader.hpp>
#include <Kotonoha/components/SplashScreen.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <Kotonoha/events/InputActions.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha {

Runtime::Runtime(std::string name, std::string version, std::string identify)
    : name(std::move(name)), version(std::move(version)),
      identify(std::move(identify)) {
  Prefetch::Scheduler::Config config;
  // Every id dispatched by this Scheduler must be a manifest asset path. The
  // recency/sibling/sequential heuristics would emit raw debugNode() labels
  // ("8005:1505", "luca_system | ...") that FetchAssetProvider::stat() cannot
  // resolve. The preload.json `nodes` graph is the authoritative predictor.
  config.learnFromObservation = false;
  config.siblingsEnabled = false;
  config.sequentialEnabled = false;
  config.recencyCapacity = 0;
  prefetchScheduler_ = Prefetch::Scheduler(config);
}

Runtime::~Runtime() = default;

bool Runtime::isStoryPaused() const {
  return splashEngine ? splashEngine->isStoryPaused() : hasPausingScene();
}

int Runtime::translationRoute() const {
  auto node = debugNode();
  size_t start = 0;
  bool tagged = false;
  for (auto tag : {" | scene ", " | route "})
    if (auto pos = node.find(tag); pos != node.npos) {
      start = pos + std::char_traits<char>::length(tag);
      tagged = true;
      break;
    }
  int id = -1;
  auto parsed =
      std::from_chars(node.data() + start, node.data() + node.size(), id);
  return parsed.ec == std::errc{} && id >= 0 &&
                 (tagged || parsed.ptr == node.data() + node.size() ||
                  *parsed.ptr == ':')
             ? id
             : -1;
}

void Runtime::saveGame(const std::string &) {
  throw std::logic_error("This game has not implemented saveGame");
}

void Runtime::loadGame(const std::string &) {
  throw std::logic_error("This game has not implemented loadGame");
}

// One node notification per story transition, shared by every game port.
// nodeClockKey() is the preferred stable scene identity; other runtimes expose
// their existing debugNode() without any per-port browser code. Kept before
// init() so the EM_ASM scene bridge stays the first EM_ASM in this translation
// unit (tools/test_em_asm_fetch.js matches it by source order).
void Runtime::publishPredictiveNode(std::string node) {
  if (node.empty() || node == lastPredictiveNode_)
    return;
  lastPredictiveNode_ = node;
#ifdef __EMSCRIPTEN__
  EM_ASM(
      {
        // 'typeof' returns a string; use JS '==' because '===' is
        // broken into '== =' by C++ macro tokenization in EM_ASM.
        if (typeof window.KotonohaSceneEntered == "function")
          window.KotonohaSceneEntered(UTF8ToString($0));
      },
      node.c_str());
#endif
  if (!recognizedPrefetchNodes_.count(node))
    return;
  ++predictiveTransitions_;
  prefetchScheduler_.observeAndPrefetch(std::move(node));
}

void Runtime::init(Engine &engine) {
  splashEngine = &engine;
#ifdef __EMSCRIPTEN__
  // Rebind after the browser has indexed the remote manifest: WasmBackend
  // captures remoteGameAssets() in its constructor, while Runtime objects may
  // be created before the asynchronous bootstrap finishes. Then import the
  // `nodes` graph exported by KotonohaPreloadCache.js so the Scheduler only
  // ever predicts real asset paths from the very same map the JS cache uses.
  wasmPrefetchBackend_ = Prefetch::WasmBackend();
  prefetchScheduler_.setBackend(&wasmPrefetchBackend_);
  std::string graph;
  const int graphBytes = EM_ASM_INT({
    var cache = window.KotonohaAssetCache;
    // Avoid JS strict equality inside EM_ASM: C++ formatters can split it.
    if (!cache || typeof cache.prefetchGraph != "function") return 0;
    var text = cache.prefetchGraph();
    return typeof text == "string" ? lengthBytesUTF8(text) : 0;
  });
  if (graphBytes > 0 && graphBytes <= 2 * 1024 * 1024) {
    graph.resize(static_cast<std::size_t>(graphBytes));
    EM_ASM({
      var cache = window.KotonohaAssetCache;
      stringToUTF8(cache.prefetchGraph(), $0, $1 + 1);
    }, graph.data(), graphBytes);
  }
  registerPrefetchGraph(graph);
  // One predictive pathway: C++ predicts and dispatches through WasmBackend;
  // the JS sceneEntered() keeps recording transitions but stops issuing its
  // own speculative range requests while this flag is set.
  EM_ASM({ window.KotonohaCppPrefetchActive = true; });
#endif
  engine.GetTimeline().stop();
  if (SDL_TextInputActive(&engine.GetWindow()) &&
      !SDL_StopTextInput(&engine.GetWindow()))
    throw std::runtime_error(SDL_GetError());
  ClearScenes();

  // Executa inicialização global primeiro
  initGlobal(engine);

  // Inicializa o estado atual
  switch (currentState) {
  case GameState::Splash:
    initSplash(engine);
    break;
  case GameState::Menu:
    initMenu(engine);
    break;
  case GameState::Gameplay:
    initGameplay(engine);
    break;
  }
}

SDL_FPoint Runtime::sceneSize(const Scene &scene) const {
  if (scene.designSpace())
    return {UI::width, UI::height};
  const SDL_Point view = viewport();
  return {static_cast<float>(view.x), static_cast<float>(view.y)};
}

void Runtime::renderScene(Scene &scene, RendererRef renderer,
                          float scale) const {
  const auto frame = UI::fit(sceneSize(scene), viewport());
  UI::FrameScope scope(renderer, frame);
  scene.render(renderer, scale * frame.scale);
}

bool Runtime::dispatchEvent(Scene &scene, EventRef event) const {
  return scene.handleEvent(
      UI::toFrame(event, UI::fit(sceneSize(scene), viewport())));
}

void Runtime::initGlobal(Engine &engine) {}
std::vector<std::filesystem::path> Runtime::splashPictures() const {
  for (const char *name :
       {"SPLASH.MP4", "SPLASH.MOV", "SPLASH.WEBM", "SPLASH.PNG"}) {
    if (auto splash = FileResolver::findEngineAsset(name))
      return {*splash};
  }
  return {};
}

void Runtime::initSplash(Engine &engine) {
  splashEngine = &engine;
  if (!splash) {
    std::vector<std::unique_ptr<Scene>> scenes;
    scenes.push_back(std::make_unique<SplashScreen>(engine, splashPictures()));
    splash = std::make_unique<SplashChainloader>(engine, std::move(scenes));
  } else
    splash->restart();
  AddScene(splash.get(), GameState::Splash);
}
void Runtime::initMenu(Engine &engine) {}
void Runtime::initGameplay(Engine &engine) {}

// Compact graph format produced by KotonohaPreloadCache.js::prefetchGraph():
//   N\t<node id>          start a node record (id is a preload.json key)
//   A\t<asset path>       manifest asset used by the current node
//   X\t<node id>\t<weight> weighted successor from the `next` array
// Both passes matter: successors are resolved to their own asset lists before
// hints are registered, so a hint target is always a valid manifest id.
void Runtime::registerPrefetchGraph(std::string_view graph) {
  struct Record {
    std::string id;
    std::vector<std::string> assets;
    std::vector<std::pair<std::string, float>> next;
  };
  std::vector<Record> records;
  Record *current = nullptr;
  std::size_t pos = 0;
  while (pos < graph.size()) {
    const std::size_t end = graph.find('\n', pos);
    const std::size_t stop = end == std::string_view::npos ? graph.size() : end;
    std::string_view line = graph.substr(pos, stop - pos);
    pos = stop + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.size() < 2 || line[1] != '\t')
      continue;
    const char kind = line[0];
    const std::string_view value = line.substr(2);
    if (kind == 'N') {
      records.push_back(Record{std::string(value), {}, {}});
      current = &records.back();
      continue;
    }
    if (!current)
      continue;
    if (kind == 'A') {
      if (!value.empty())
        current->assets.emplace_back(value);
    } else if (kind == 'X') {
      const auto tab = value.find('\t');
      if (tab == std::string_view::npos)
        continue;
      // std::from_chars for floating point needs a newer SDK than the
      // macOS/Android deployment targets. strtod is portable; validate that
      // the entire token was consumed, mirroring ArtemisUi::parseRect().
      const std::string token(value.substr(tab + 1));
      char *endPtr = nullptr;
      const double parsed = std::strtod(token.c_str(), &endPtr);
      if (endPtr != token.c_str() + token.size())
        continue;
      const float weight = static_cast<float>(parsed);
      if (std::isfinite(weight) && weight > 0.0f)
        current->next.emplace_back(std::string(value.substr(0, tab)), weight);
    }
  }

  std::unordered_map<std::string, const Record *> byId;
  byId.reserve(records.size());
  for (const Record &record : records)
    byId.emplace(record.id, &record);
  for (const Record &record : records) {
    if (record.id.empty())
      continue;
    recognizedPrefetchNodes_.insert(record.id);
    for (const std::string &asset : record.assets) {
      prefetchScheduler_.registerKnown(asset);
      prefetchScheduler_.registerHint(record.id, asset, 1.0f);
    }
    for (const auto &[nextId, weight] : record.next) {
      const auto found = byId.find(nextId);
      if (found == byId.end())
        continue;
      for (const std::string &asset : found->second->assets)
        prefetchScheduler_.registerHint(record.id, asset, 2.0f * weight);
    }
  }
}

void Runtime::render(RendererRef renderer, float scale) {
  switch (currentState) {
  case GameState::Splash:
    renderSplash(renderer, scale);
    break;
  case GameState::Menu:
    renderMenu(renderer, scale);
    break;
  case GameState::Gameplay:
    renderGameplay(renderer, scale);
    break;
  }
}

void Runtime::renderSplash(RendererRef renderer, float scale) {
  for (Scene *scene : splashScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::renderMenu(RendererRef renderer, float scale) {
  for (Scene *scene : menuScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::renderGameplay(RendererRef renderer, float scale) {
  for (Scene *scene : gameplayScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::update() {
  if (currentState == GameState::Splash && splash && splashEngine &&
      splash->finished()) {
    SetState(GameState::Menu);
    init(*splashEngine);
    return;
  }
  const auto activeScenes = GetActiveScenes();
  for (Scene *scene : activeScenes)
    if (scene)
      scene->update();
#ifdef __EMSCRIPTEN__
  // Always publish to the browser cache, even before a graph is available,
  // so its recorder can produce a first preload.json from real play.
  const bool publishNode = currentState == GameState::Gameplay;
#else
  // Native has no browser graph: keep the historical no-op unless a test or
  // host explicitly registered one (its Scheduler prefetch() is a no-op).
  const bool publishNode =
      currentState == GameState::Gameplay && !recognizedPrefetchNodes_.empty();
#endif
  if (publishNode) {
    const auto stable = nodeClockKey();
    const auto node = stable.empty() ? debugNode() : stable;
    publishPredictiveNode(node);
  }
}

bool Runtime::onEvent(EventRef event) {
  if (splashEngine && HandleNavigation(*splashEngine, event))
    return true;
  const auto activeScenes = GetActiveScenes();
  for (auto it = activeScenes.rbegin(); it != activeScenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Novel &&
        (*it)->capturesInput()) {
      dispatchEvent(**it, event);
      // A modal owns the input even when it has no binding for this event.
      return true;
    }
  }
  const auto state = currentState;
  for (auto it = activeScenes.rbegin(); it != activeScenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Novel &&
        dispatchEvent(**it, event))
      return true;
    if (state != currentState)
      return true;
    if (isStoryPaused())
      return true;
  }
  return false;
}

bool Runtime::onScreenEvent(EventRef event) {
  const auto scenes = GetActiveScenes();
  for (auto it = scenes.rbegin(); it != scenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Screen &&
        (*it)->capturesInput()) {
      (*it)->handleEvent(event);
      return true;
    }
  }
  const auto state = currentState;
  for (auto it = scenes.rbegin(); it != scenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Screen &&
        (*it)->handleEvent(event))
      return true;
    if (state != currentState)
      return true;
  }
  return false;
}

void Runtime::renderScreen(RendererRef renderer, float scale) {
  for (auto *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == RenderSpace::Screen)
      scene->render(renderer, scale);
}

void Runtime::renderBackground(RendererRef renderer, float scale) {
  for (auto *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Background)
      renderScene(*scene, renderer, scale);
}

void Runtime::renderOverlay(RendererRef renderer, float scale) {
  for (auto *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Overlay)
      renderScene(*scene, renderer, scale);
}

} // namespace Kotonoha
