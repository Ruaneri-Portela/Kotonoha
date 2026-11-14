// Optional WebAssembly integration harness. It is only added to the RealLive
// target when KOTONOHA_WEB_SELFTEST is enabled, so production web builds do not
// expose these entry points. The browser test drives the real VNSave and
// MediaPlayer implementations; nothing here reimplements their behaviour.
#ifdef KOTONOHA_WEB_SELFTEST
#ifdef __EMSCRIPTEN__

#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

namespace {
std::unique_ptr<Kotonoha::MediaPlayer> webSelfTestPlayer;
std::string webSelfTestResult;

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

extern "C" {
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

// The page writes a zero-byte placeholder to /game-assets/selftest.webm, the
// same shape produced for a deferred manifest video. MediaPlayer must resolve
// it through the provider and play the real bytes from the HTTP URL.
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestOpenVideo() {
  try {
    const Kotonoha::FileSystemAssetProvider assets("/game-assets");
    webSelfTestPlayer = std::make_unique<Kotonoha::MediaPlayer>();
    webSelfTestPlayer->open(assets, "selftest.webm", false, "selftest");
    return 1;
  } catch (const std::exception &error) {
    logFailure("open video", error);
    webSelfTestPlayer.reset();
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
}
}

#endif
#endif
