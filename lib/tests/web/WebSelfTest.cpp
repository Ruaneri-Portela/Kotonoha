// Optional WebAssembly integration harness. It is only added to the dedicated
// KotonohaWebSelfTest target, so production web builds do not expose these
// entry points. The browser test drives the real VNSave and
// MediaPlayer implementations; nothing here reimplements their behaviour.
#ifdef KOTONOHA_WEB_SELFTEST
#ifdef __EMSCRIPTEN__

#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>

#include <filesystem>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

namespace {
std::unique_ptr<Kotonoha::MediaPlayer> webSelfTestPlayer;
std::unique_ptr<Kotonoha::MediaPlayer> webSelfTestSecondAudio;
std::string webSelfTestResult;
std::shared_ptr<Kotonoha::Filesystem::Filesystem> liveFolderMount;

std::filesystem::path webSelfTestSavePath() {
  const auto directory = Kotonoha::documentsDirectory("web-selftest");
  std::filesystem::create_directories(directory);
  return directory / "save-selftest.vn";
}

void logFailure(const char *operation, const std::exception &error) {
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[selftest] %s: %s", operation,
               error.what());
}
} // namespace

SDL_Renderer *KotonohaWebSelfTestRenderer();

void KotonohaWebSelfTestDrawMedia(SDL_Renderer *renderer) {
  if (!webSelfTestPlayer || !renderer)
    return;
  try {
    webSelfTestPlayer->update(*renderer);
    webSelfTestPlayer->render(*renderer);
  } catch (const std::exception &error) {
    logFailure("draw media", error);
  }
}

extern "C" {
// The HTTP-only test must not require a MEMFS placeholder or a real file path.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestRemoteSize(const char *name) {
  try {
    if (!name || !*name || !Kotonoha::Filesystem::remoteGameAssets())
      return -1;
    auto source = Kotonoha::Filesystem::mountGameAssets("/game-assets");
    auto bytes = Kotonoha::Filesystem::readAll(
        *source->open(name), 32u << 20);
    return static_cast<int>(bytes.size());
  } catch (const std::exception &error) {
    logFailure("remote FileHandle", error);
    return -1;
  }
}

// Regression: a multi-hundred-megabyte archive must allow a tiny remote
// trailer read via HTTP 206 without allocating a complete GPK into MEMFS.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestLargeRemoteTail() {
  try {
    if (!Kotonoha::Filesystem::remoteGameAssets())
      return -1;
    constexpr std::int64_t kFileSize = 320ll * 1024 * 1024;
    auto source = Kotonoha::Filesystem::mountGameAssets("/game-assets");
    auto file = source->open("event01.gpk");
    if (file->size() != kFileSize ||
        file->seek(-8, Kotonoha::Filesystem::SeekOrigin::End) !=
            kFileSize - 8)
      return -2;
    char trailer[8] = {};
    if (file->read(trailer, sizeof(trailer)) != 8 ||
        std::memcmp(trailer, "GPKTAIL!", sizeof(trailer)) != 0)
      return -3;
    if (file->seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) != 0)
      return -4;
    char header[4] = {};
    if (file->read(header, sizeof(header)) != 4 ||
        std::memcmp(header, "GPK!", sizeof(header)) != 0)
      return -5;
    return 1;
  } catch (const std::exception &error) {
    logFailure("large GPK Range", error);
    return -6;
  }
}

// Keep the same directory mount across browser imports: this catches a
// stale VFS index that would otherwise require restarting every game.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestLiveLocalSize(const char *id) {
  try {
    if (!id || !*id) return -1;
    if (!liveFolderMount)
      liveFolderMount = Kotonoha::Filesystem::mountGameAssets("/game-assets");
    auto file = liveFolderMount->open(id);
    return static_cast<int>(Kotonoha::Filesystem::readAll(*file, 32u << 20).size());
  } catch (const std::exception &error) {
    logFailure("live browser folder", error);
    return -1;
  }
}

// Regression: the C++ exception must be caught after the Asyncify
// continuation resumes. Previous browser builds escaped to the XHR
// JavaScript callback as an uncaught EmscriptenEH CppException.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestCatchAfterAsyncify() {
  try {
    emscripten_sleep(10);
    throw std::runtime_error("Intentional Asyncify/EH test exception");
  } catch (const std::runtime_error &) {
    return 1;
  }
}

EMSCRIPTEN_KEEPALIVE const char *KotonohaWebSelfTestSavePath() {
  webSelfTestResult = webSelfTestSavePath().string();
  return webSelfTestResult.c_str();
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestWriteSave() {
  try {
    Kotonoha::VNSave save;
    save.nodeId = 4242;
    save.mode = Kotonoha::ScriptMode::Timed;
    save.timelinePosition = 12.5;
    save.resolvedChoices = {1, 3, 5};
    save.pendingBranch = -1;
    save.gameData = "web-selftest";
    save.write(webSelfTestSavePath().string());
    return 1;
  } catch (const std::exception &error) {
    logFailure("write save", error);
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE const char *KotonohaWebSelfTestReadSave() {
  try {
    const auto save = Kotonoha::VNSave::read(webSelfTestSavePath().string());
    std::ostringstream text;
    text << "node=" << save.nodeId << ";mode="
         << (save.mode == Kotonoha::ScriptMode::Timed ? "timed" : "standard")
         << ";position=" << save.timelinePosition
         << ";choices=" << save.resolvedChoices.size()
         << ";data=" << save.gameData;
    webSelfTestResult = text.str();
  } catch (const std::exception &error) {
    logFailure("read save", error);
    webSelfTestResult = std::string("ERR: ") + error.what();
  }
  return webSelfTestResult.c_str();
}

// The page writes a zero-byte placeholder to /game-assets/selftest.webm to
// exercise URL playback separately from the locally buffered fallback tests.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenVideoId(const char *id) {
  try {
    const Kotonoha::FileSystemAssetProvider assets("/game-assets");
    webSelfTestPlayer = std::make_unique<Kotonoha::MediaPlayer>();
    webSelfTestPlayer->open(assets, id, false, "selftest");
    return 1;
  } catch (const std::exception &error) {
    logFailure("open video", error);
    webSelfTestPlayer.reset();
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenVideo() {
  return KotonohaWebSelfTestOpenVideoId("selftest.webm");
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenAudioId(const char *id) {
  try {
    const Kotonoha::FileSystemAssetProvider assets("/game-assets");
    webSelfTestPlayer = std::make_unique<Kotonoha::MediaPlayer>();
    webSelfTestPlayer->open(assets, id, true, "selftest-audio");
    return 1;
  } catch (const std::exception &error) {
    logFailure("open audio", error);
    webSelfTestPlayer.reset();
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenSecondAudioId(const char *id) {
  try {
    const Kotonoha::FileSystemAssetProvider assets("/game-assets");
    webSelfTestSecondAudio = std::make_unique<Kotonoha::MediaPlayer>();
    webSelfTestSecondAudio->open(assets, id, true, "selftest-audio-two");
    return 1;
  } catch (const std::exception &error) {
    logFailure("open second audio", error);
    webSelfTestSecondAudio.reset();
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestPumpSecondAudio() {
  if (!webSelfTestSecondAudio || !KotonohaWebSelfTestRenderer())
    return 0;
  try {
    webSelfTestSecondAudio->update(*KotonohaWebSelfTestRenderer());
    return 1;
  } catch (const std::exception &error) {
    logFailure("pump second audio", error);
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE double KotonohaWebSelfTestSecondAudioDuration() {
  return webSelfTestSecondAudio ? webSelfTestSecondAudio->duration() : -1;
}

EMSCRIPTEN_KEEPALIVE double KotonohaWebSelfTestSecondAudioElapsed() {
  return webSelfTestSecondAudio ? webSelfTestSecondAudio->elapsed() : -1;
}

EMSCRIPTEN_KEEPALIVE void KotonohaWebSelfTestSetAudioGain(float gain) {
  if (webSelfTestPlayer && webSelfTestPlayer->getChannel())
    webSelfTestPlayer->getChannel()->setVolume(gain);
}

EMSCRIPTEN_KEEPALIVE double KotonohaWebSelfTestAudioGain() {
  return webSelfTestPlayer ? webSelfTestPlayer->audioGain() : -1;
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenVideoPath(const char *path) {
  try {
    webSelfTestPlayer = std::make_unique<Kotonoha::MediaPlayer>();
    webSelfTestPlayer->open(path, false, "selftest");
    return 1;
  } catch (const std::exception &error) {
    logFailure("open video path", error);
    webSelfTestPlayer.reset();
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestPumpVideo() {
  if (!webSelfTestPlayer || !KotonohaWebSelfTestRenderer())
    return 0;
  try {
    webSelfTestPlayer->update(*KotonohaWebSelfTestRenderer());
    return 1;
  } catch (const std::exception &error) {
    logFailure("pump video", error);
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestRenderVideo() {
  if (!webSelfTestPlayer || !KotonohaWebSelfTestRenderer())
    return 0;
  try {
    webSelfTestPlayer->render(*KotonohaWebSelfTestRenderer());
    return 1;
  } catch (const std::exception &error) {
    logFailure("render video", error);
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE double KotonohaWebSelfTestVideoDuration() {
  return webSelfTestPlayer ? webSelfTestPlayer->duration() : -1.0;
}

EMSCRIPTEN_KEEPALIVE double KotonohaWebSelfTestVideoElapsed() {
  return webSelfTestPlayer ? webSelfTestPlayer->elapsed() : -1.0;
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestVideoHasFrame() {
  return webSelfTestPlayer && webSelfTestPlayer->hasVideoFrame() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestVideoFinished() {
  return webSelfTestPlayer && webSelfTestPlayer->finished() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void KotonohaWebSelfTestCloseVideo() {
  webSelfTestPlayer.reset();
  webSelfTestSecondAudio.reset();
}
}

#endif
#endif
