#include "PmailPack.hpp"
#include <Pmail/Port.hpp>
#include <Pmail/Vm.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  // Defaults keep the CI threshold; the overrides allow longer parity sweeps
  // without changing what normal runs assert.
  int maxMessages = 500;
  int budgetSeconds = 45;
  int titleChoice = 0;
  int extrasChoice = 4;
  if (const char *value = SDL_getenv("PMAIL_FLOW_MESSAGES"))
    maxMessages = std::max(1, std::atoi(value));
  if (const char *value = SDL_getenv("PMAIL_FLOW_SECONDS"))
    budgetSeconds = std::max(1, std::atoi(value));
  if (const char *value = SDL_getenv("PMAIL_FLOW_TITLE_CHOICE"))
    titleChoice = std::max(0, std::atoi(value));
  if (const char *value = SDL_getenv("PMAIL_FLOW_EXTRAS_CHOICE"))
    extrasChoice = std::clamp(std::atoi(value), 1, 4);
  const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
  Kotonoha::PmailPort::PmailGame game(std::move(bytes), path.string());
  // Diagnostic: dump a script object's word stream around a selected word PC.
  if (const char *dump = SDL_getenv("PMAIL_FLOW_DUMP")) {
    auto object = game.loadByName(dump);
    if (!object) {
      std::cerr << "dump: object not found: " << dump << '\n';
      return 3;
    }
    std::size_t center = object->codeWords;
    const bool full = !SDL_getenv("PMAIL_FLOW_DUMP_PC");
    if (!full)
      center = std::strtoul(SDL_getenv("PMAIL_FLOW_DUMP_PC"), nullptr, 0);
    const std::size_t begin = full ? 0 : (center > 48 ? center - 48 : 0);
    const std::size_t end =
        full ? object->codeWords : std::min(object->codeWords, center + 48);
    std::cerr << "== " << object->name << " id=0x" << std::hex << object->id
              << std::dec << " codeWords=" << object->codeWords << " ==\n";
    for (std::size_t i = begin; i < end; ++i) {
      const uint32_t w = object->word(i);
      std::cerr << i << ": 0x" << std::hex << std::setw(8) << std::setfill('0')
                << w << std::dec << "  op=0x" << std::hex << (w >> 24)
                << " mode=0x" << ((w >> 16) & 0xff) << " imm=0x" << (w & 0xffff)
                << std::dec;
      const uint32_t imm = w & 0xffff;
      const uint32_t mode = (w >> 16) & 0xff;
      if (mode == 2 || mode == 5)
        std::cerr << "  str=\"" << object->str(imm) << "\"";
      std::cerr << '\n';
    }
    return 0;
  }
  // Diagnostic: enumerate immediate SYS calls in every script object. The
  // retail pack is compressed, so load each entry through PmailPack rather
  // than scanning the raw DAT. This stays identifier-only (no dialogue).
  if (SDL_getenv("PMAIL_FLOW_NATIVE_SCAN")) {
    Kotonoha::PmailPort::PmailPack pack;
    if (!pack.open(path.string())) {
      std::cerr << "native-scan: cannot open " << path << '\n';
      return 3;
    }
    std::map<int, std::set<std::string>> byId;
    size_t objects = 0;
    for (const auto &entry : pack.entries()) {
      auto bytes = pack.read(entry);
      if (bytes.empty())
        continue;
      ::Pmail::ScriptObject object;
      if (!object.parse(entry.name, std::move(bytes)))
        continue;
      ++objects;
      for (std::size_t i = 0; i < object.codeWords; ++i) {
        const uint32_t word = object.word(i);
        if ((word >> 24) != 0x05)
          continue;
        const uint32_t mode = (word >> 16) & 0xff;
        if (mode == 3)
          byId[int(word & 0xffff)].insert(object.name);
        else if (mode == 4)
          byId[int((word >> 8) & 0xff)].insert(object.name);
      }
    }
    std::cout << "objects=" << objects << '\n';
    for (const auto &[id, sites] : byId) {
      std::cout << id;
      for (const auto &site : sites)
        std::cout << '\t' << site;
      std::cout << '\n';
    }
    std::cout << "ids=" << byId.size() << '\n';
    return 0;
  }
  Kotonoha::Providers::SnowAssets assets(argv[1]);
  if (const char *filter = SDL_getenv("PMAIL_FLOW_LIST")) {
    for (const auto &id : assets.listAsset())
      if (std::string(filter).empty() || id.find(filter) != std::string::npos)
        std::cout << id << '\n';
    return 0;
  }
  if (const char *asset = SDL_getenv("PMAIL_FLOW_ASSET")) {
    try {
      const auto bytes = assets.readAsset(asset);
      std::cerr << asset << " -> " << bytes.size() << " bytes\n";
      if (const char *path = SDL_getenv("PMAIL_FLOW_ASSET_SAVE")) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  std::streamsize(bytes.size()));
      }
    } catch (const std::exception &error) {
      std::cerr << asset << " -> " << error.what() << '\n';
    }
    return 0;
  }
  if (const char *sprite = SDL_getenv("PMAIL_FLOW_SPRITE")) {
    const auto placement = assets.Sprite(sprite);
    std::cout << sprite << " valid=" << placement.valid << " x=" << placement.x
              << " y=" << placement.y << " w=" << placement.w
              << " h=" << placement.h << '\n';
    return 0;
  }
  if (const char *graphic = SDL_getenv("PMAIL_FLOW_GRAPHIC")) {
    auto surface = assets.loadSurfaceAsset(graphic);
    std::cerr << graphic << " -> "
              << (surface ? std::to_string(surface->w) + "x" +
                                std::to_string(surface->h)
                          : std::string("missing"))
              << '\n';
    if (surface && SDL_getenv("PMAIL_FLOW_GRAPHIC_SAVE")) {
      SDL_SaveBMP(surface.get(), SDL_getenv("PMAIL_FLOW_GRAPHIC_SAVE"));
    }
    return 0;
  }
  game.animationFrameCount = [&](const std::string &name) {
    return assets.AnimationOf(name).frames.size();
  };
  game.trace = [](const std::string &line) { std::cout << line << '\n'; };
  std::map<int, size_t> counts;
  game.musicTracks = [&] {
    std::vector<std::string> ids;
    for (const auto &id : assets.listAsset("sound/mm")) {
      const auto slash = id.rfind('/');
      if (slash == std::string::npos || id.compare(slash + 1, 2, "mm") != 0)
        continue;
      bool digits = id.size() > slash + 3;
      for (std::size_t i = slash + 3; i < id.size(); ++i)
        if (id[i] < '0' || id[i] > '9') {
          digits = false;
          break;
        }
      if (digits)
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
  };
  game.onNative = [&](int id, const auto &) { ++counts[id]; };
  if (!game.init())
    return 1;
  const auto began = SDL_GetTicks();
  int messages = 0;
  bool started = false;
  bool extrasDone = false;
  auto viewerMode = Kotonoha::PmailPort::PmailGame::ExtrasViewerMode::None;
  bool viewerTouched = false;
  while (SDL_GetTicks() - began < static_cast<uint64_t>(budgetSeconds) * 1000 &&
         messages < maxMessages) {
    game.advanceAnimation(16);
    game.pump(10000);
    if (game.extrasViewerActive()) {
      if (game.extrasViewerMode() != viewerMode) {
        viewerMode = game.extrasViewerMode();
        viewerTouched = false;
      }
      if (game.extrasViewerFullView())
        game.extrasViewerBack();
      else if (!viewerTouched) {
        viewerTouched = true;
        game.extrasViewerActivate();
      } else
        game.extrasViewerBack();
    } else {
      viewerMode = Kotonoha::PmailPort::PmailGame::ExtrasViewerMode::None;
      viewerTouched = false;
      if (game.extrasMenuActive()) {
        // The native omake overlay is modal. PMAIL_FLOW_EXTRAS_CHOICE selects
        // a viewer (1 CG, 2 recollection, 3 music) or 4 to exit. Exercise the
        // requested viewer once, then leave the menu so the trace continues.
        const int choice = extrasDone ? 4 : extrasChoice;
        extrasDone = true;
        game.extrasChoose(choice);
      } else if (game.atTitle()) {
        if (started)
          break;
        game.titleChoose(titleChoice);
        started = true;
      } else if (game.choosing())
        game.choose(0);
      else if (game.nativeTimeWaitActive()) {
        // SYS 34 pauses are skippable by input; do the same so the trace does
        // not spend the whole budget inside retail dramatic beats.
        game.click();
      } else if (game.waitingForClick()) {
        game.click();
        ++messages;
      } else if (game.ended())
        break;
      else
        SDL_Delay(1);
    }
  }
  std::cerr << "Flow: " << messages << " dialogue advances; "
            << game.currentScript() << ":" << game.vm().pc() << '\n';
  for (auto [id, count] : counts)
    std::cerr << "SYS " << id << ": " << count << '\n';
  if (!started || messages < 400 || counts[51] < 3) {
    std::cerr
        << "Story did not progress through the computer animation sequence\n";
    return 1;
  }
}
