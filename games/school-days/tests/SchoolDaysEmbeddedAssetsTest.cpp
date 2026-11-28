#include "SchoolDaysEmbeddedAssets.hpp"
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using Kotonoha::FileResolver;

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::vector<unsigned char> read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

void write(const fs::path &path, const char *text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(bool(output), "Cannot write test fixture");
}

struct Fixture {
  fs::path previous = fs::current_path();
  fs::path root = fs::temp_directory_path() /
                  ("kotonoha-embedded-test-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  Fixture() {
    require(fs::create_directory(root), "Cannot allocate test directory");
    root = fs::canonical(root);
    fs::current_path(root);
  }
  ~Fixture() {
    std::error_code ignored;
    fs::current_path(previous, ignored);
    fs::remove_all(root, ignored);
  }
};

void checkNeutralScope(const Fixture &fixture) {
  const auto before = Kotonoha::applicationDirectories("test-port");
  const auto fallback = fixture.root / "private-assets";
  const auto name = "embedded-test-only.glsl";
  write(fallback / "ArtCNN" / name, "fallback");
  write(fallback / "embedded-test-only.txt", "embedded");
  write(fallback / "game-assets/embedded-test-leak.txt", "must not resolve");
  write(fixture.root / "game-assets/embedded-test-only.txt", "game");
  {
    Kotonoha::ScopedEngineAssetRoot scope(fallback);
    const auto during = Kotonoha::applicationDirectories("test-port");
    require(during.gameAssets == before.gameAssets &&
                during.documents == before.documents &&
                during.saves == before.saves,
            "Fallback changed game or save directories");
    require(during.engineAssets.back() == fallback,
            "Embedded root must have lowest engine asset priority");
    require(FileResolver::findEngineAsset("embedded-test-only.txt") ==
                fallback / "embedded-test-only.txt" &&
                FileResolver::findAsset(name) == fallback / "ArtCNN" / name,
            "Embedded resource or shader basename did not resolve");
    require(FileResolver::findGameAsset("test-port",
                                        "embedded-test-only.txt") ==
                fixture.root / "game-assets/embedded-test-only.txt" &&
                !FileResolver::findGameAsset("test-port",
                                             "embedded-test-leak.txt") &&
                !FileResolver::findGameAsset("test-port", name) &&
                !FileResolver::findAsset("game-assets/embedded-test-leak.txt",
                                         "test-port"),
            "Engine fallback contaminated game-asset lookup");
    write(fixture.root / "assets/embedded-test-only.txt", "external");
    write(fixture.root / "assets/ArtCNN" / name, "external shader");
    require(FileResolver::findEngineAsset("embedded-test-only.txt") ==
                fixture.root / "assets/embedded-test-only.txt" &&
                FileResolver::findAsset(name) ==
                    fixture.root / "assets/ArtCNN" / name,
            "External override lost precedence");
  }
  const auto after = Kotonoha::applicationDirectories("test-port");
  require(after.engineAssets == before.engineAssets &&
              after.references == before.references &&
              fs::is_directory(fallback),
          "Root scope did not unregister or deleted caller-owned data");
}

void checkPack(const fs::path &source) {
#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS)
  require(fs::is_directory(source), "Approved source directory missing");
  const auto before = Kotonoha::applicationDirectories("school-days");
  fs::path previousRoot;
  for (int pass = 0; pass < 2; ++pass) {
    fs::path sessionRoot;
    {
      SchoolDays::EmbeddedAssets pack;
      const auto during = Kotonoha::applicationDirectories("school-days");
      require(during.engineAssets.size() == before.engineAssets.size() + 1 &&
                  during.gameAssets == before.gameAssets &&
                  during.documents == before.documents &&
                  during.saves == before.saves,
              "Private native pack changed directory policy");
      sessionRoot = during.engineAssets.back();
      require(sessionRoot != previousRoot && sessionRoot.filename() == "assets" &&
                  sessionRoot.parent_path().filename().string().find("session-") == 0,
              "Pack did not allocate a fresh private session");
      size_t count = 0;
      for (const auto &entry : fs::recursive_directory_iterator(source)) {
        if (!entry.is_regular_file())
          continue;
        const auto logical = entry.path().lexically_relative(source);
        const auto expected = read(entry.path());
        require(!expected.empty() &&
                    SchoolDays::ReadEmbeddedAsset(logical.generic_string()) ==
                        expected &&
                    read(sessionRoot / logical) == expected,
                "Embedded bytes mismatch: " + logical.generic_string());
        ++count;
      }
      require(count == 50, "Expected the exact approved 50-file pack");
      size_t materialized = 0;
      for (const auto &entry : fs::recursive_directory_iterator(sessionRoot))
        if (entry.is_regular_file())
          ++materialized;
      require(materialized == count, "Unexpected materialized resource");
      require(SchoolDays::ReadEmbeddedAsset("../SPLASH.PNG").empty() &&
                  SchoolDays::ReadEmbeddedAsset("game-assets/styles.ass").empty() &&
                  SchoolDays::ReadEmbeddedAsset("school-days-hq.ktnroute").empty(),
              "Embedded pack exposed an out-of-scope resource");
      require(FileResolver::isFontFile(sessionRoot / "fonts/NotoSansJP-Regular.ttf"),
              "Embedded default font is invalid");
      // Path-based consumers can use these assets without an installation copy.
      require(FileResolver::findEngineAsset("SPLASH.PNG").has_value() &&
                  FileResolver::findEngineAsset("SchoolDaysHQ-Launcher.png").has_value() &&
                  FileResolver::findEngineAsset("ArtCNN/ArtCNN_C4F16.glsl").has_value(),
              "Splash, launcher, or shader resolution failed");
      require(!FileResolver::getFontMap().empty(),
              "Font catalog could not load the scoped assets");
    }
    require(!fs::exists(sessionRoot.parent_path()) &&
                Kotonoha::applicationDirectories("school-days").engineAssets ==
                    before.engineAssets,
            "Private session was not removed after scope exit");
    previousRoot = sessionRoot;
  }
#else
  (void)source;
  const auto before = Kotonoha::applicationDirectories();
  SchoolDays::EmbeddedAssets pack;
  require(SchoolDays::ReadEmbeddedAsset("SPLASH.PNG").empty() &&
              Kotonoha::applicationDirectories().engineAssets == before.engineAssets,
          "Disabled private pack must be a no-op");
#endif
}
} // namespace

int main(int argc, char **argv) {
  try {
    SDL_SetAppMetadata("Embedded assets test", "1",
                       "org.kotonoha.embedded-assets-test");
    const auto source =
        argc > 1 && argv[1][0] ? fs::absolute(argv[1]) : fs::path{};
    Fixture fixture;
    checkNeutralScope(fixture);
    checkPack(source);
    std::cout << "Embedded asset scope, precedence, bytes and cleanup passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
