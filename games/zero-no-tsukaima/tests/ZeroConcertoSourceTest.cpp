#include <ZeroNoTsukaima/Source.hpp>
#include <ZeroNoTsukaima/Game.hpp>

#include <cstdio>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  ZeroNoTsukaima::SceneSource source;
  std::string error;
  const auto check = [](bool ok, const char *message) {
    if (!ok)
      std::fprintf(stderr, "FAIL: %s\n", message);
    return ok;
  };
  // Repeat on the same object to catch stale names/indices after reopening.
  for (int pass = 0; pass < 2; ++pass) {
    if (!check(source.Open(argv[1], &error), error.c_str()) ||
        !check(source.Names().size() == 1921, "scene count") ||
        !check(source.Names().front() == "0000.KS" &&
                   source.Names().back() == "1920.KS", "numeric scene names") ||
        !check(source.PictureCount() == 1184, "picture count") ||
        !check(source.NormalCount() == 360, "normal count") ||
        !check(source.SoundCount() == 106, "sound count") ||
        !check(source.VoiceCount() == 16192, "voice count") ||
        !check(source.NormalIndex("11") == 11 &&
                   source.ScenedatIndex("0") == 0 &&
                   source.SoundIndex("1") == 1, "numeric asset lookup"))
      return 1;
    std::vector<uint8_t> data;
    for (size_t i = 0; i < source.Names().size(); ++i)
      if (!check(source.LoadScene(i, &data), "scene read"))
        return 1;
    Kotonoha::Ps2Image image;
    if (!check(source.LoadNormalPicture(11, &image), "decode UI atlas") ||
        !check(source.LoadPicture(0, &image), "decode scene image") ||
        !check(source.LoadSound(1, &data) && !data.empty(), "sound read") ||
        !check(source.LoadVoice(1, &data) && !data.empty(), "voice read") ||
        !check(!source.LoadScene(1921, &data), "reject invalid scene index"))
      return 1;
  }
  std::puts("Concerto: all 1921 scene slots, images, audio and reopen verified");
  for (size_t pick = 0; pick < 3; ++pick) {
    ZeroNoTsukaima::Game game(source.Names(), [&](size_t i, std::vector<uint8_t> *out) {
      return source.LoadScene(i, out);
    });
    if (!check(game.Start("0006.KS"), "start real source script"))
      return 1;
    int pages = 0;
    for (int step = 0; step < 300 && !game.Choosing() && !game.Finished(); ++step) {
      game.Pump();
      if (game.WaitingForClick()) {
        if (pages == 0 && !check(game.Speaker() == "ルイズ" &&
                                   game.Text() == "……。" && game.Voice() == 5542,
                               "first real dialogue and voice ID"))
          return 1;
        ++pages;
        game.Click();
      } else if (!game.Choosing()) {
        game.FinishWait();
      }
    }
    if (!check(pages == 30 && game.Choices().size() == 3, "real scene choice point") ||
        !check(game.Choose(pick), "choose real branch"))
      return 1;
    const std::string branch = "000" + std::to_string(7 + pick) + ".KS";
    if (!check(game.ScriptName() == branch, "selected destination scene"))
      return 1;
    for (int step = 0; step < 100 && game.ScriptName() != "0010.KS" &&
                       !game.Finished(); ++step) {
      game.Pump();
      if (game.WaitingForClick())
        game.Click();
      else
        game.FinishWait();
    }
    if (!check(game.ScriptName() == "0010.KS", "branch rejoins through next()"))
      return 1;
    std::printf("Concerto narrative: 30 pages -> choice %zu -> %s -> 0010.KS\n",
                pick, branch.c_str());
  }
  return 0;
}
