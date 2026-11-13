#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
  Kotonoha::PmailPort::PmailGame game(std::move(bytes), path.string());
  Kotonoha::Providers::SnowAssets assets(argv[1]);
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
  while (SDL_GetTicks() - began < 45000 && messages < 500) {
    game.advanceAnimation(16);
    game.pump(10000);
    if (game.atTitle()) {
      if (started)
        break;
      game.titleChoose(0);
      started = true;
    } else if (game.choosing())
      game.choose(0);
    else if (game.waitingForClick()) {
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
