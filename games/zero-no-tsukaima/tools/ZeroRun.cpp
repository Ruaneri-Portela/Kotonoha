// Headless runner: plays the scene scripts straight from the disc image,
// auto-clicking and always picking the first choice.
//   ZeroRun <disc.bin> [maxSteps] [START.KS]
#include <ZeroNoTsukaima/Game.hpp>
#include <ZeroNoTsukaima/Source.hpp>

#include <cstdio>
#include <cstdlib>

using namespace ZeroNoTsukaima;

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <disc.bin> [maxSteps] [script]\n", argv[0]);
    return 2;
  }
  SceneSource src;
  std::string error;
  if (!src.Open(argv[1], &error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  const int maxSteps = argc > 2 ? std::atoi(argv[2]) : 100;
  Game game(src.Names(), [&](size_t i, std::vector<uint8_t> *o) { return src.LoadScene(i, o); });
  if (!game.Start(argc > 3 ? argv[3] : "START.KS")) {
    std::fprintf(stderr, "cannot start\n");
    return 1;
  }
  for (int i = 0; i < maxSteps; ++i) {
    game.Pump();
    if (game.Finished()) { std::printf("[finished in %s]\n", game.ScriptName().c_str()); break; }
    if (game.Choosing()) {
      std::printf("[%s] CHOICE:", game.ScriptName().c_str());
      for (const auto &c : game.Choices()) std::printf(" | %s", c.c_str());
      std::printf("\n");
      game.Choose(0);
    } else if (game.WaitingForClick()) {
      std::printf("[%s] [%s|%s] %s%s%s\n", game.ScriptName().c_str(), game.Stage().c_str(), game.Event().c_str(),
                  game.Speaker().empty() ? "" : (game.Speaker() + ": ").c_str(), game.Text().c_str(),
                  game.Voice() ? (" <v" + std::to_string(game.Voice()) + ">").c_str() : "");
      game.Click();
    }
  }
  return 0;
}
