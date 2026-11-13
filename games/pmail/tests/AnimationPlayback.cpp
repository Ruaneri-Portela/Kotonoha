#include <Pmail/Port.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Kotonoha::PmailPort;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: PmailAnimationPlaybackCheck PMAILDVD");
    uint8_t phase = 0;
    require(MouthFrame(64, phase) == 0 && phase == 0,
            "Silence must not advance mouth phase");
    require(MouthFrame(-64, phase) == 0 && phase == 0,
            "Negative silence threshold changed");
    require(MouthFrame(65, phase) == 1 && phase == 1,
            "Quiet mouth pattern changed");
    require(MouthFrame(16378, phase) == 2 && phase == 2,
            "Loud mouth pattern changed");
    phase = 0;
    require(MouthFrame(-16378, phase) == 1,
            "Original asymmetric quiet boundary changed");
    phase = 0;
    require(MouthFrame(-16379, phase) == 0,
            "Original negative loud boundary changed");
    Kotonoha::Providers::SnowAssets assets(argv[1]);
    const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    PmailGame game(bytes, path.string());
    game.animationFrameCount = [&](const std::string &name) {
      return assets.AnimationOf(name).frames.size();
    };
    require(game.init(), "Could not initialize game");
    game.returnToTitle();
    game.titleChoose(0);
    game.pump();
    game.click();
    game.native(game.vm(), 51, {{0}, {245}, {152}});
    require(game.animating(), "SYS 51 did not start OSBOOT");
    require(game.animations().back().count == 80,
            "OSBOOT should have 80 frames");
    const auto pc = game.vm().pc();
    require(!game.click(), "Dialogue input skipped animation");
    for (uint32_t frame = 0; frame < 80; ++frame) {
      require(game.animations().back().frame == frame, "Frame was skipped");
      game.pump();
      require(game.vm().pc() == pc, "VM advanced during animation");
      game.advanceAnimation(53);
      require(game.animations().back().frame == frame,
              "OSBOOT advanced before its 54ms interval");
      if (frame == 37) {
        const auto saved = game.serialize();
        PmailGame restored(bytes, path.string());
        restored.animationFrameCount = game.animationFrameCount;
        restored.deserialize(saved);
        require(restored.serialize() == saved,
                "Animation save did not preserve frame/phase/VM");
        restored.advanceAnimation(1);
        require(restored.animations().back().frame == 38,
                "Restored animation restarted or lost phase");
      }
      game.advanceAnimation(1);
    }
    require(!game.animating() && !game.vm().blocked(),
            "Animation did not release the VM");
    require(game.animations().back().frame == 79,
            "Last frame was not retained");
    for (uint32_t preset = 1; preset < AnimationPresets.size(); ++preset) {
      game.native(game.vm(), 51, {{preset}, {200}, {100}});
      const auto count = game.animations().back().count;
      for (uint32_t frame = 0; frame < count; ++frame) {
        require(game.animations().back().frame == frame,
                "Effect skipped a frame after a slow update");
        game.advanceAnimation(10000);
      }
      require(!game.animating(), "Effect did not finish");
    }
    game.native(game.vm(), 51, {{0}, {245}, {152}});
    for (int tick = 0; tick < 270; ++tick)
      game.advanceAnimation(16);
    require(!game.animating(),
            "60Hz updates drifted beyond the original 4320ms OSBOOT duration");
    game.native(game.vm(), 69, {{0}, {0}, {0}});
    require(game.animations().empty(),
            "Scene clear left stale animation overlays");
    game.returnToTitle();
    require(!game.animating(), "Returning to title retained blocked animation");
    std::cout << "All 19 SYS 51 presets played; OSBOOT timing, VM barrier, no "
                 "skipped frames, save/restore and scene cleanup passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
