#include "PmailAudio.hpp"
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 2;
    SDL_Init(SDL_INIT_AUDIO);
    auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
    Kotonoha::AudioMixer mixer;
    Kotonoha::PmailPort::PmailAudio audio(mixer, assets);
    const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    Kotonoha::PmailPort::PmailGame game(bytes, path.string());
    game.animationFrameCount = [&](const std::string &name) {
      return assets->AnimationOf(name).frames.size();
    };
    game.trace = [](const std::string &line) {
      std::cout << line << std::endl;
    };
    game.onNative = [&](int id, const std::vector<Pmail::Arg> &args) {
      std::vector<int> numbers;
      std::vector<std::string> strings;
      for (auto a : args) {
        if (a.tag() == 0)
          numbers.push_back(game.vm().value(a));
        else if (a.tag() == 2)
          strings.push_back(game.vm().text(a));
      }
      audio.Handle(id, numbers, strings);
    };
    if (!game.init())
      return 1;
    game.returnToTitle();
    game.titleChoose(2);
    for (int i = 0; i < 100; i++) {
      game.pump();
      if (game.waitingForClick())
        game.click();
      if (game.choosing())
        game.choose(0);
      SDL_Delay(20);
    }
    std::cout << "RESULT " << game.currentScript() << ":" << game.vm().pc()
              << " title=" << game.atTitle() << " ended=" << game.ended()
              << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
