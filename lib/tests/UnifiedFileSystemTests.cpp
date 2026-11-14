#include <Kotonoha/core/UnifiedFileSystem.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

void put(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  require(static_cast<bool>(output), "Cannot write test fixture");
}

std::string text(const std::optional<std::vector<std::uint8_t>> &bytes) {
  if (!bytes)
    throw std::runtime_error("Expected existing file");
  return std::string(bytes->begin(), bytes->end());
}

class TestProvider final : public Kotonoha::AssetProvider {
public:
  std::string name() const override { return "memory"; }

protected:
  bool containsAsset(const std::string &id) const override {
    return id == "story.ks";
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    if (id != "story.ks")
      throw std::runtime_error("Unknown test asset");
    return {'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r'};
  }
};

template <class Action>
void expectRejected(Action action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Unsafe path must be rejected");
}

struct TempRoot {
  fs::path root;
  TempRoot() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::temp_directory_path() /
           ("kotonoha-filesystem-" + std::to_string(stamp));
    fs::create_directories(root);
  }
  ~TempRoot() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};

void testNativeLocations() {
  TempRoot tmp;
  Kotonoha::StorageLocations locations;
  locations.platform = Kotonoha::StoragePlatform::Desktop;
  locations.resources = tmp.root / "resources";
  locations.userData = tmp.root / "user";
  locations.workingDirectory = tmp.root / "work";
  locations.packageIdentifier = "org.kotonoha.test";

  put(locations.resources / "assets/fonts/demo.ttf", "bundled");
  put(locations.workingDirectory / "assets/fonts/demo.ttf", "working");
  put(locations.resources / "game-assets/story.ks", "disk");
  put(locations.resources / "game-assets/fallback.bin", "");
  put(locations.resources / "config/settings.json", "{}");

  Kotonoha::UnifiedFileSystem service(locations, "example");
  using Kotonoha::FileDomain;
  require(text(service.read(FileDomain::Engine, "fonts/demo.ttf")) == "bundled",
          "Engine asset precedence was changed");
  require(text(service.read(FileDomain::Game, "story.ks")) == "disk",
          "Loose game file not resolved");
  require(service.read(FileDomain::Game, "fallback.bin")->empty(),
          "Empty file must not be confused with a missing file");
  require(!service.read(FileDomain::Game, "absent.bin"),
          "Missing game file must return nullopt");
  require(text(service.read(FileDomain::Reference, "config/settings.json")) == "{}",
          "References should resolve from known resource roots");

  const std::vector<std::uint8_t> data = {0, 255, 10};
  service.writeSave("chapter/slot.dat", data);
  require(service.read(FileDomain::Saves, "chapter/slot.dat") == data,
          "Binary save round-trip failed");
  service.writeSave("chapter/slot.dat", {1, 2});
  require(service.read(FileDomain::Saves, "chapter/slot.dat") ==
              std::optional<std::vector<std::uint8_t>>({1, 2}),
          "Atomic save replacement failed");
  require(!fs::exists(service.layout().saves / "chapter/slot.dat.tmp"),
          "Temporary save file must not persist");

  expectRejected([&] { service.read(FileDomain::Game, "../secret.bin"); });
  expectRejected([&] { service.writeSave("../escape.bin", data); });
  expectRejected([&] { service.writeSave("/absolute.bin", data); });
  expectRejected([&] { service.writeSave("chapter\\..\\escape.bin", data); });

  fs::create_directories(tmp.root / "outside");
  std::error_code ec;
  fs::create_directories(service.layout().saves);
  fs::create_directory_symlink(tmp.root / "outside",
                               service.layout().saves / "outside-link", ec);
  if (!ec) {
    expectRejected([&] { service.writeSave("outside-link/leak.dat", data); });
    require(!fs::exists(tmp.root / "outside/leak.dat"),
            "Save must not follow links outside writable storage");
  }

  auto provider = std::make_shared<TestProvider>();
  service.setGameProvider(provider);
  require(text(service.read(FileDomain::Game, "story.ks")) == "provider",
          "Native provider must take precedence over loose game files");
  require(text(service.read(FileDomain::Game, "fallback.bin")) == "",
          "Provider misses must fall back to loose files");

  put(tmp.root / "overlay/story.ks", "override");
  provider->setOverlay(tmp.root / "overlay");
  require(text(service.read(FileDomain::Game, "story.ks")) == "override",
          "Provider overlay precedence was lost");
}

void testMobileLayout() {
  TempRoot tmp;
  Kotonoha::StorageLocations locations;
  locations.packageIdentifier = "org.kotonoha.mobile";
  locations.resources = tmp.root / "bundle";
  locations.userData = tmp.root / "documents";
  locations.obb = tmp.root / "obb";
  locations.privateGameAssets = tmp.root / "private-game-assets";

  locations.platform = Kotonoha::StoragePlatform::Android;
  put(locations.obb / "game_assets/data.bin", "obb");
  put(locations.privateGameAssets / "data.bin", "private");
  Kotonoha::UnifiedFileSystem android(locations, "example");
  require(text(android.read(Kotonoha::FileDomain::Game, "data.bin")) == "obb",
          "Android OBB priority changed");

  locations.platform = Kotonoha::StoragePlatform::IOS;
  put(locations.resources / "game-assets/data.bin", "bundle");
  put(locations.userData / "game-assets/data.bin", "documents");
  Kotonoha::UnifiedFileSystem ios(locations, "example");
  require(text(ios.read(Kotonoha::FileDomain::Game, "data.bin")) == "bundle",
          "iOS bundle must precede Documents");
}
} // namespace

int main() {
  try {
    testNativeLocations();
    testMobileLayout();
    std::cout << "Unified filesystem tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Unified filesystem tests failed: " << error.what() << '\n';
    return 1;
  }
}
