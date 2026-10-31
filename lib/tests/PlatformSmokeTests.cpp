#if defined(KOTONOHA_IOS_SMOKE)
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#endif

#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

void runTests() {
  const auto resources = Kotonoha::resourceDirectory();
  require(Kotonoha::engineAssetDirectory() == resources / "assets",
          "Engine asset directory");
  require(Kotonoha::gameAssetDirectory("platform-smoke") == resources / "game-assets",
          "Game asset directory");
#if defined(KOTONOHA_IOS_SMOKE)
  require(Kotonoha::preferredDataDirectory("platform-smoke") == resources,
          "iOS preferred data directory");
  const auto documents = Kotonoha::preferredDocumentsDirectory("platform-smoke");
  require(documents.filename() == "Documents", "iOS sandbox Documents directory");
  const auto probe = documents /
      ("kotonoha-path-smoke-" + std::to_string(SDL_GetPerformanceCounter()) + ".txt");
  Kotonoha::writeTextFileAtomically(probe.string(), "documents");
  std::ifstream input(probe);
  std::string contents;
  input >> contents;
  input.close();
  require(std::filesystem::remove(probe), "Remove documents probe");
  require(contents == "documents", "Preferred documents is writable");
#endif
  const std::string sjis =
      "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x87\x40\x81\x60\xb1";
  require(Kotonoha::ShiftJisToUtf8(sjis) ==
              "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"
              "\xe2\x91\xa0\xef\xbd\x9e\xef\xbd\xb1",
          "CP932 conversion");
  require(Kotonoha::ShiftJisToUtf8("a\x85\x40" "z") == "a?z",
          "Invalid CP932 sequence");

#if defined(KOTONOHA_IOS_SMOKE)
  const auto fontPath = Kotonoha::resourceDirectory() / "SvgTestFont.ttf";
#else
  const std::filesystem::path fontPath = KOTONOHA_SVG_TEST_FONT;
#endif
  require(TTF_Init(), "Initialize SDL_ttf");
  struct TtfCleanup {
    ~TtfCleanup() { TTF_Quit(); }
  } cleanup;
  std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(
      TTF_OpenFont(fontPath.string().c_str(), 64), TTF_CloseFont);
  require(font != nullptr, "Open SVG glyph font");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
      TTF_RenderText_Blended(font.get(), "\xee\x80\x80", 3,
                             SDL_Color{255, 255, 255, 255}),
      SDL_DestroySurface);
  require(surface != nullptr, "Render SVG glyph");
  bool redPixel = false;
  for (int y = 0; y < surface->h && !redPixel; ++y) {
    for (int x = 0; x < surface->w; ++x) {
      Uint8 red, green, blue, alpha;
      require(SDL_ReadSurfacePixel(surface.get(), x, y, &red, &green, &blue,
                                   &alpha),
              "Read SVG glyph pixel");
      if (red > 200 && green < 50 && blue < 50 && alpha > 100) {
        redPixel = true;
        break;
      }
    }
  }
  require(redPixel, "SVG glyph contains its red vector artwork");
}

bool execute() {
  try {
    runTests();
    SDL_Log("KOTONOHA_PLATFORM_SMOKE_OK");
    return true;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Platform smoke test: %s",
                 error.what());
    return false;
  }
}
} // namespace

#if defined(KOTONOHA_IOS_SMOKE)
SDL_AppResult SDL_AppInit(void **, int, char **) {
  return execute() ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}
SDL_AppResult SDL_AppEvent(void *, SDL_Event *) { return SDL_APP_CONTINUE; }
SDL_AppResult SDL_AppIterate(void *) { return SDL_APP_CONTINUE; }
void SDL_AppQuit(void *, SDL_AppResult) {}
#else
int main() { return execute() ? 0 : 1; }
#endif
