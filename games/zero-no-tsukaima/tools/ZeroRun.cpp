// Headless runner: plays the scene scripts straight from the disc image,
// auto-clicking and always picking the first choice.
//   ZeroRun <disc.bin> [maxSteps] [START.KS]
//   ZeroRun <disc.bin> --tags <SCRIPT.KS>   (prints the raw tags of one scene, ALL = every scene)
#include <ZeroNoTsukaima/Game.hpp>
#include <ZeroNoTsukaima/Scene.hpp>
#include <ZeroNoTsukaima/Source.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
  if (argc > 3 && std::strcmp(argv[2], "--tags") == 0) {
    const bool all = std::strcmp(argv[3], "ALL") == 0;
    for (size_t i = 0; i < src.Names().size(); ++i) {
      if (!all && src.Names()[i] != argv[3]) continue;
      if (all) std::printf("## %s\n", src.Names()[i].c_str());
      std::vector<uint8_t> data;
      std::vector<SceneTag> tags;
      if (!src.LoadScene(i, &data) || !ParseSceneTags(data, &tags)) return 1;
      for (const SceneTag &t : tags) {
        std::printf("%s", t.name.c_str());
        for (const auto &[k, v] : t.attrs)
          if (k != "tagname") std::printf(" %s=%s", k.c_str(), v.c_str());
        std::printf("\n");
      }
      if (!all) return 0;
    }
    if (all) return 0;
    std::fprintf(stderr, "no scene %s\n", argv[3]);
    return 1;
  }
  const int maxSteps = argc > 2 ? std::atoi(argv[2]) : 100;
  Game game(src.Names(), [&](size_t i, std::vector<uint8_t> *o) { return src.LoadScene(i, o); });
  game.SetCharacterTable(&src.Catalog().Characters());
  game.SetStageNames(src.Catalog().StageNames());
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
    } else {
      game.FinishWait();
    }
  }
  return 0;
}
