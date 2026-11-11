#include <ZeroNoTsukaima/Game.hpp>
#include <cstdio>

using namespace ZeroNoTsukaima;
static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static std::vector<uint8_t> Bytes(const std::string &s) {
  return {s.begin(), s.end()};
}

int main() {
  std::vector<SceneTag> tags;
  std::string error;
  // 0x5c is the trail byte of CP932 表, not a string escape.
  std::string cp932 = "talk(\"";
  cp932 += char(0x95);
  cp932 += char(0x5c);
  cp932 +=
      "\", null, \"line\\n\\\"//not a comment\\\"\\\\end\", (0x04000000+17));";
  CHECK(ParseConcertoScene(Bytes(cp932), &tags, &error));
  bool name = false, text = false;
  for (const auto &tag : tags) {
    if (tag.name == "dispname")
      name = tag.attrs.at("name") == "表";
    if (tag.name == "ch")
      text = tag.attrs.at("text") == "line\n\"//not a comment\"\\end";
  }
  CHECK(name && text);

  const std::vector<std::string> source = {
      R"( /* comma, brace } and fake talk("x") */
      eval("f.route = 1");
      if (isJump()) { talk("wrong", null, "debug", null); }
      if (f.route == 1) {
        talk("internal", "shown", "first", (0x04000000+17));
        eval("f.route += 1");
        if (f.route == 2) { talk("n", null, "nested", null); }
      } else { talk("wrong", null, "else", null); }
      talk("n", null, "second", null);
      selectInit();
      selectItem("hidden", (0x01000000+1), null, false);
      selectItem("continue", (0x01000000+1), "f.pick = 7", "f.route == 2");
      selectItem("stay", null, "f.pick = 9", true);
      select();
      talk("n", null, "stayed", null);
    )",
      R"(talk("n", null, "chosen", null);
      next((0x01000000+0), false);
      next((0x01000000+2), "f.pick == 7");)",
      R"(talk("n", null, "arrived", null); unknownGameplay({a=1,b=2});)"};
  auto loader = [&](size_t i, std::vector<uint8_t> *out) {
    if (i >= source.size())
      return false;
    *out = Bytes(source[i]);
    return true;
  };
  Game game({"0000.KS", "0001.KS", "0002.KS"}, loader);
  CHECK(game.Start("0000.KS"));
  game.Pump();
  CHECK(game.WaitingForClick() && game.Text() == "first");
  CHECK(game.Speaker() == "shown" && game.Voice() == 17);
  auto cues = game.TakeCues();
  CHECK(cues.size() == 1 && cues[0].kind == Game::Cue::Voice &&
        cues[0].number == 17);
  CHECK(game.Click());
  game.Pump();
  CHECK(game.Text() == "nested" && game.Voice() == 0);
  CHECK(game.Click());
  game.Pump();
  CHECK(game.Text() == "second");
  CHECK(game.Click());
  game.Pump();
  CHECK(game.Choosing() && game.Choices().size() == 2);
  CHECK(!game.Choose(2) && game.Choosing());
  CHECK(game.Choose(0));
  game.Pump();
  CHECK(game.ScriptName() == "0001.KS" && game.Text() == "chosen");
  CHECK(game.Flag("f.pick") == 7);
  CHECK(game.Click());
  game.Pump();
  CHECK(game.ScriptName() == "0002.KS" && game.Text() == "arrived");
  CHECK(game.Click());
  game.Pump();
  CHECK(game.Finished() && game.UnsupportedCommands().count("unknownGameplay"));

  Game stay({"0000.KS", "0001.KS", "0002.KS"}, loader);
  CHECK(stay.Start("0000.KS"));
  for (int i = 0; i < 4; ++i) {
    stay.Pump();
    stay.Click();
  }
  stay.Pump();
  CHECK(stay.Choose(1));
  stay.Pump();
  CHECK(stay.ScriptName() == "0000.KS" && stay.Text() == "stayed");
  CHECK(stay.Flag("f.pick") == 9);

  Game layers({"0000.KS"}, [&](size_t, std::vector<uint8_t> *out) {
    *out = Bytes(R"(reset(); back();
      set("stage", {image=(0x02000000+85),show=1,level=200,opacity=100});
      set("lay1", {image=(0x02000000+303),show=1,level=140,x=64,opacity=100});
      trans("crossfade",100);
      talk("n",null,"visible",null);
      set("lay1", {opacity=0,hide=1,opacityTime=200});
      waitAction(null,true);
      talk("n",null,"hidden",null);)");
    return true;
  });
  CHECK(layers.Start("0000.KS"));
  layers.Pump();
  CHECK(layers.Blocked() == Game::Block::Time);
  layers.FinishWait();
  layers.Pump();
  auto scene = layers.Scene();
  CHECK(scene.size() == 2 && scene[0].image == "scenedat/85" &&
        scene[1].image == "scenedat/303" && scene[1].x == 64);
  CHECK(layers.Click());
  layers.Pump();
  layers.Advance(100);
  scene = layers.Scene();
  CHECK(scene.size() == 2 && scene[1].alpha > 0.49f && scene[1].alpha < 0.51f);
  layers.Advance(100);
  layers.Pump();
  CHECK(layers.Scene().size() == 1 && layers.Text() == "hidden");

  for (const char *bad : {"talk(\"broken", "/* missing", "if (true) {",
                          "while(true) {talk(\"n\",null,\"x\",null);}",
                          "if (unknown()) { msgon(); }",
                          "if (f.x & 1) msgon();", "eval(\"f.x = unknown()\");",
                          "selectItem(\"x\",null,\"unknown()\",true);",
                          "next((0x02000000+1), true);"}) {
    const size_t before = tags.size();
    CHECK(!ParseConcertoScene(Bytes(bad), &tags, &error));
    CHECK(!error.empty() && tags.size() == before);
  }
  if (!failures)
    std::puts("Concerto: CP932, escapes, nested conditions, choices, eval, "
              "voice and cross-scene transitions verified");
  return failures ? 1 : 0;
}
