#include <Kotonoha/routing/SchoolDaysEpisodeOverlay.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysEpisodeOverlayTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

} // namespace

int main() {
    using Overlay = Kotonoha::SchoolDaysEpisodeOverlay;

    Overlay overlay;
    Check(!overlay.Armed(), "fresh overlay is unexpectedly armed");
    Check(!overlay.ArmForNaturalHandoff("bad", "01/01-00-A00"),
          "malformed source SceneKey armed overlay");
    Check(!overlay.ArmForNaturalHandoff("00/00-00-A00", "00/00-00-A01"),
          "same-episode scene swap armed overlay");

    Check(overlay.ArmForNaturalHandoff("00/00-00-L00", "01/01-00-A00"),
          "Episode 1 -> Episode 2 handoff did not arm");
    Check(overlay.EpisodeNumber() == 2,
          "Episode 2 handoff produced wrong caption number");
    Check(overlay.AppliesTo("01/01-00-A00"),
          "overlay does not bind to destination scene");
    Check(!overlay.AppliesTo("01/01-00-A01"),
          "overlay leaked into following physical scene");
    Check(Overlay::TelopLogicalPath(2) ==
              "System/SCREEN/TELOP/TELOP02.PNG",
          "Episode 2 did not map to the retail TELOP02 asset");
    Check(Overlay::TelopLogicalPath(6) ==
              "System/SCREEN/TELOP/TELOP06.PNG",
          "Episode 6 did not map to the retail TELOP06 asset");
    Check(Overlay::TelopLogicalPath(0).empty() &&
              Overlay::TelopLogicalPath(7).empty(),
          "unsupported episode number fabricated a TELOP asset");

    Check(overlay.ArmForNaturalHandoff("01/01-1K-O00", "02/02-2K-A00"),
          "Episode 2 -> Episode 3 route did not arm");
    Check(overlay.EpisodeNumber() == 3, "Episode 3 caption number mismatch");
    Check(overlay.ArmForNaturalHandoff("02/02-2K-I02", "03/03-KB-A00"),
          "Episode 3 -> Episode 4 route did not arm");
    Check(overlay.EpisodeNumber() == 4, "Episode 4 caption number mismatch");
    Check(overlay.ArmForNaturalHandoff("03/03-KB-L03", "04/04-SA-A00"),
          "Episode 4 -> Episode 5 route did not arm");
    Check(overlay.EpisodeNumber() == 5, "Episode 5 caption number mismatch");
    Check(overlay.ArmForNaturalHandoff("04/04-SA-L03", "05/05-5H-A00"),
          "Episode 5 -> Episode 6 route did not arm");
    Check(overlay.EpisodeNumber() == 6, "Episode 6 caption number mismatch");

    Check(!overlay.ArmForNaturalHandoff("05/05-5H-L03", "06/06-XX-A00"),
          "unsupported Episode 7 handoff armed overlay");
    Check(!overlay.Armed(), "failed handoff left stale overlay armed");

    // Measured from the retail Episode 1 movie: ~0.5 s fade-in, opaque until
    // ~4 s, then a ~1 s fade-out.  Later captions use the same host profile.
    Check(Overlay::Alpha(0u) == 0u, "alpha at t=0 is not transparent");
    Check(Overlay::Alpha(250u) == 127u, "fade-in midpoint drifted");
    Check(Overlay::Alpha(500u) == 255u, "fade-in end is not opaque");
    Check(Overlay::Alpha(4000u) == 255u, "hold end is not opaque");
    Check(Overlay::Alpha(4500u) == 127u, "fade-out midpoint drifted");
    Check(Overlay::Alpha(5000u) == 0u, "fade-out end is not transparent");

    std::cout << "SCHOOL DAYS EPISODE OVERLAY GATE PASS\n"
              << "natural_handoff_episode_2_to_6=PASS\n"
              << "same_scene_and_load_default_unarmed=PASS\n"
              << "destination_scene_binding=PASS\n"
              << "retail_telop_asset_mapping=PASS\n"
              << "retail_timing_0_5_4_5_seconds=PASS\n";
    return 0;
}
