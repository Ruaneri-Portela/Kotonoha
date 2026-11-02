#include "DdlcAssets.hpp"
#include <Ddlc/Runtime.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TemporaryDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("kotonoha-ddlc-assets-" + std::to_string(SDL_GetPerformanceCounter()));

  TemporaryDirectory() {
    if (!std::filesystem::create_directory(path))
      throw std::runtime_error("Cannot create isolated test directory");
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

void number(std::vector<uint8_t> &out, uint32_t value) {
  for (int n = 0; n < 4; ++n)
    out.push_back(uint8_t(value >> (n * 8)));
}
void string(std::vector<uint8_t> &out, const std::string &text) {
  out.push_back('X');
  number(out, text.size());
  out.insert(out.end(), text.begin(), text.end());
}
void archive(const std::filesystem::path &path, const std::string &id,
             const std::string &body, bool rpe = false, bool invalid = false) {
  const uint32_t key = rpe ? 0x42424242u : 0x11111111u;
  std::vector<uint8_t> index{0x80, 2, '}'};
  string(index, id);
  index.push_back(']');
  index.push_back('J');
  number(index, (invalid ? 1000000 : 64) ^ key);
  index.push_back('J');
  number(index, uint32_t(body.size()) ^ key);
  index.insert(index.end(), {0x86, 'a', 's', '.'});
  uLongf size = compressBound(index.size());
  std::vector<uint8_t> packed(size);
  require(compress(packed.data(), &size, index.data(), index.size()) == Z_OK,
          "Compress fixture");
  packed.resize(size);
  char header[64]{};
  if (rpe)
    std::snprintf(header, sizeof header, "RPE-1.0 %08x %016llx\n",
                  key ^ 0xffffffffu,
                  static_cast<unsigned long long>(64 + body.size()));
  else
    std::snprintf(header, sizeof header, "RPA-3.0 %016llx %08x\n",
                  static_cast<unsigned long long>(64 + body.size()), key);
  std::ofstream file(path, std::ios::binary);
  file.write(header, 64);
  file.write(body.data(), body.size());
  file.write(reinterpret_cast<const char *>(packed.data()), packed.size());
  require(bool(file), "Write archive fixture");
}
std::string text(const std::vector<uint8_t> &bytes) {
  return std::string(bytes.begin(), bytes.end());
}
void archiveTests(const std::filesystem::path &root) {
  const auto dir = root / "archives";
  std::filesystem::create_directory(dir);
  archive(dir / "a.rpa", "ui/test.txt", "first");
  archive(dir / "z.rpa", "ui/test.txt", "last");
  Ddlc::Assets assets;
  require(assets.open(dir.string()), "Open synthetic RPA");
  require(text(assets.read("ui/test.txt")) == "last",
          "Archives follow reverse priority");
  std::filesystem::create_directory(dir / "UI");
  {
    std::ofstream f(dir / "UI" / "Test.txt");
    f << "loose";
  }
  require(assets.open(dir.string()), "Reopen with loose files");
  require(text(assets.read("ui/test.txt")) == "loose",
          "Loose files precede archives case-insensitively");
  auto names = assets.names();
  require(std::count(names.begin(), names.end(), "ui/test.txt") == 1,
          "Merged names are unique");
  archive(dir / "translated.rpx", "ui/test.txt", "translated", true);
  Ddlc::Archive patch;
  require(patch.open((dir / "translated.rpx").string()), "Open VNX RPE");
  require(text(patch.read("UI/TEST.TXT")) == "translated",
          "Complemented VNX key decodes bytes");
  assets.mountOverlay({(dir / "translated.rpx").string()}, {}, {});
  require(text(assets.read("ui/test.txt")) == "translated",
          "Overlay archive precedes base loose files");
  assets.mountOverlay(
      {(dir / "translated.rpx").string()},
      [](const std::string &id, std::vector<uint8_t> *out) {
        if (id != "ui/test.txt")
          return false;
        if (out)
          *out = {'m', 'o', 'd'};
        return true;
      },
      [] { return std::vector<std::string>{"extra.rpyc"}; });
  require(text(assets.read("ui/test.txt")) == "mod",
          "Overlay loose file precedes overlay archive");
  archive(dir / "invalid.rpx", "ui/test.txt", "bad", true, true);
  Ddlc::Archive invalid;
  require(!invalid.open((dir / "invalid.rpx").string()),
          "Archive bounds reject invalid payload");
}

struct HeadlessBackend : Ddlc::Backend {
  bool imageSize(const std::string &, int &w, int &h) override {
    w = 1280;
    h = 720;
    return true;
  }
  void textSize(const std::string &text, const Ddlc::TextStyle &style, float &w,
                float &h) override {
    w = float(text.size()) * style.size * .5f;
    h = style.size;
  }
  void log(const std::string &) override {}
};
void runtimeTests(const Ddlc::GameAssets &game,
                  const std::filesystem::path &user) {
  HeadlessBackend backend;
  Ddlc::Vm vm(game.source(), game.script(), backend);
  vm.userDirectory = user;
  std::filesystem::create_directories(user);
  Ddlc::seedCharacters(user, game.source().dir(), &game.source());
  double clock = 0;
  vm.clock = [&] { return clock; };
  vm.init("");
  if (!vm.errors.empty())
    throw std::runtime_error(vm.errors.front());
  require(vm.py.eval("len(full_wordlist)>100").truthy(),
          "Poem words must initialize, including UTF8 patch words");
  require(bool(vm.imageNamed({"bedroom"})),
          "RenPy automatic basename image registration");
  require(!std::filesystem::exists(user / "CAN YOU HEAR ME.txt"),
          "An archived note is not a file in basedir");
  vm.py.exec("try: renpy.file(config.basedir+'/CAN YOU HEAR ME.txt')\nexcept: "
             "open(config.basedir+'/CAN YOU HEAR "
             "ME.txt','wb').write(renpy.file('CAN YOU HEAR ME.txt').read())\n");
  require(std::filesystem::is_regular_file(user / "CAN YOU HEAR ME.txt"),
          "Act2 copies its archived note into the user basedir");
  vm.py.set("copy_source", Ddlc::Value::str((user / "copy-source").string()));
  vm.py.set("copy_dest", Ddlc::Value::str((user / "copy-dest").string()));
  vm.py.exec("import shutil\nwith open(copy_source,'wb') as f: f.write('bridge "
             "copy')\nshutil.copyfile(copy_source,copy_dest)\nassert "
             "open(copy_dest).read()=='bridge copy'\n");
  require(std::filesystem::is_regular_file(user / "copy-dest"),
          "Copy only writes in the isolated user directory");
  vm.py.setAttr(vm.persistent, "playername", Ddlc::Value::str("Tester"));
  vm.start("start");
  int poemWords = 0, says = 0;
  for (int n = 0; n < 1000 && vm.running() && !vm.finished(); ++n) {
    clock += 100;
    const auto &interaction = vm.interaction();
    switch (interaction.k) {
    case Ddlc::Interaction::K::Say:
      ++says;
      vm.resume();
      break;
    case Ddlc::Interaction::K::Menu:
      vm.resume(Ddlc::Value::integer(0));
      break;
    case Ddlc::Interaction::K::Screen:
      vm.resume(Ddlc::Value::boolean(true));
      break;
    case Ddlc::Interaction::K::Ui:
      require(interaction.items.size() == 10, "Poem game presents ten words");
      vm.resume(
          interaction.items[size_t(++poemWords) % interaction.items.size()]
              .value);
      break;
    default:
      vm.resume();
      break;
    }
    if (!vm.errors.empty())
      throw std::runtime_error(vm.errors.front());
  }
  require(poemWords >= 20 && says >= 300,
          "First poem game and story run without compatibility errors");
  vm.stop();
  std::cout << "runtime says=" << says << " poemWords=" << poemWords << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    TemporaryDirectory temporary;
    archiveTests(temporary.path);
    const auto game = temporary.path / "game";
    const auto characters = temporary.path / "characters";
    std::filesystem::create_directory(game);
    std::error_code error;
    require(!Ddlc::hasOriginalCharacters(game, error) && !error,
            "Missing character directory must be reported");

    std::filesystem::create_directory(characters);
    auto writeCharacter = [&](const char *name) {
      std::ofstream file(characters / name);
      if (!file.put('x'))
        throw std::runtime_error("Cannot write test character file");
    };
    for (const char *name : {"monika.chr", "natsuki.chr", "sayori.chr"})
      writeCharacter(name);
    require(!Ddlc::hasOriginalCharacters(game, error) && !error,
            "Missing character file must be reported");

    writeCharacter("yuri.chr");
    require(Ddlc::hasOriginalCharacters(game, error) && !error,
            "All four character files must be recognized");

    std::filesystem::remove(characters / "monika.chr");
    std::filesystem::create_directory(characters / "monika.chr");
    require(!Ddlc::hasOriginalCharacters(game, error) && !error,
            "Character files must be regular files");

    // The live characters folder in the user directory is seeded once
    // from the installation's sibling characters/ folder.
    const auto seed = std::filesystem::absolute("ddlc-assets-seed-test");
    std::filesystem::remove_all(seed);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
      }
    } cleanup{seed};
    const auto install = seed / "install";
    const auto user = seed / "user";
    std::filesystem::create_directories(install / "game");
    std::filesystem::create_directories(install / "characters");
    std::filesystem::create_directories(user);
    for (const char *name : Ddlc::kCharacterFiles) {
      std::ofstream file(install / "characters" / name, std::ios::binary);
      if (!(file << "original " << name))
        throw std::runtime_error("Cannot write test character file");
    }
    require(Ddlc::seedCharacters(user, install / "game"),
            "A missing characters folder must be seeded");
    const auto live = Ddlc::charactersDirectory(user);
    for (const char *name : Ddlc::kCharacterFiles) {
      std::ifstream file(live / name, std::ios::binary);
      const std::string content{std::istreambuf_iterator<char>(file), {}};
      require(content == std::string("original ") + name,
              "Seeded files must copy the originals");
    }
    require(!std::filesystem::exists(user / "characters.seeding"),
            "The staging folder must not remain");

    std::filesystem::remove(live / "sayori.chr");
    require(!Ddlc::seedCharacters(user, install / "game"),
            "An existing characters folder must be kept");
    require(!std::filesystem::exists(live / "sayori.chr"),
            "Deleted characters must stay deleted");

    const auto bare = seed / "bare-user";
    std::filesystem::create_directories(bare);
    std::filesystem::remove_all(install / "characters");
    require(Ddlc::seedCharacters(bare, install / "game"),
            "The folder is created without originals too");
    require(std::filesystem::is_directory(Ddlc::charactersDirectory(bare)) &&
                std::filesystem::is_empty(Ddlc::charactersDirectory(bare)),
            "Without originals the characters folder starts empty");

    if (argc > 1) {
      Ddlc::GameAssets vanilla(argv[1]);
      require(vanilla.script().hasLabel("start"), "Vanilla start label");
      require(vanilla.contains("gui/menu_bg.png"), "Vanilla images present");
      runtimeTests(vanilla, temporary.path / "vanilla-user");
      std::cout << "vanilla scripts=" << vanilla.script().fileCount() << '\n';
      if (argc > 2) {
        Ddlc::GameAssets translated(argv[1], argv[2]);
        require(translated.script().hasLabel("start"),
                "Translated start label");
        require(translated.script().fileCount() > vanilla.script().fileCount(),
                "Loose VNX survival script included");
        require(translated.contains("images/poem_special/poem_special1.png"),
                "VNX RPE image available");
        auto before = vanilla.read("images/poem_special/poem_special1.png"),
             after = translated.read("images/poem_special/poem_special1.png");
        require(before != after && !after.empty(),
                "Translated image overrides original");
        require(translated.script().translationSources() !=
                    vanilla.script().translationSources(),
                "Translated script texts override originals");
        runtimeTests(translated, temporary.path / "translated-user");
        Ddlc::Archive rpe;
        require(
            rpe.open(
                (std::filesystem::path(argv[2]) / "vnx/game/vnx.rpx").string()),
            "Open retail VNX RPE");
        for (const auto &id : rpe.names())
          require(!rpe.read(id).empty(), "Read every VNX RPE member");
        std::cout << "translated scripts=" << translated.script().fileCount()
                  << '\n';
      }
    }
    std::cout << "DdlcAssetsTest: OK\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DdlcAssetsTest: " << error.what() << '\n';
    return 1;
  }
}
