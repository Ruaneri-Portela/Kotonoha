#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  // Defaults keep the CI threshold; the overrides allow longer parity sweeps
  // without changing what normal runs assert.
  int maxMessages = 500;
  int budgetSeconds = 45;
  int titleChoice = 0;
  if (const char *value = SDL_getenv("PMAIL_FLOW_MESSAGES"))
    maxMessages = std::max(1, std::atoi(value));
  if (const char *value = SDL_getenv("PMAIL_FLOW_SECONDS"))
    budgetSeconds = std::max(1, std::atoi(value));
  if (const char *value = SDL_getenv("PMAIL_FLOW_TITLE_CHOICE"))
    titleChoice = std::max(0, std::atoi(value));
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
  game.onNative = [&](int id, const auto &) { ++counts[id]; };
  if (!game.init())
    return 1;
  const auto began = SDL_GetTicks();
  int messages = 0;
  bool started = false;
  while (SDL_GetTicks() - began < static_cast<uint64_t>(budgetSeconds) * 1000 &&
         messages < maxMessages) {
    game.advanceAnimation(16);
    game.pump(10000);
    if (game.extrasMenuActive()) {
      // The native omake overlay is modal. Close it through EXIT so the
      // trace can continue into the title script's extra.OBJ dispatch.
      game.extrasChoose(4);
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
