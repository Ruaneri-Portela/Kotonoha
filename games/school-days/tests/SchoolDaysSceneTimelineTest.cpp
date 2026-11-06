#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/routing/SchoolDaysOrsFadeBoundary.hpp>
#include <Kotonoha/routing/SchoolDaysOrsFadeFrame.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using Kotonoha::SchoolDaysSceneTimeline;

void Require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

struct Fixture {
  fs::path path;
  Kotonoha_orsData events{};

  explicit Fixture(const char *lines) {
    static unsigned sequence = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::current_path() / ("kotonoha_g2_2_" + std::to_string(stamp) +
                                 "_" + std::to_string(++sequence) + ".ors");
    {
      std::ofstream out(path, std::ios::binary);
      Require(static_cast<bool>(out), "fixture open failed");
      out << lines;
    }
    events = Kotonoha_OrsParser(path.string().c_str());
  }

  ~Fixture() {
    Kotonoha_OrsClean(&events);
    std::error_code error;
    fs::remove(path, error);
  }

  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;
};

void TestTicks() {
  Fixture fixture("[SkipFRAME]=00:00:00;\n"
                  "[PrintText]=00:00:23\tA\tText\t00:01:00;\n"
                  "[PlayVoice]=01:02:09\tVoice/path\t1\tmak\t01:09:00;\n"
                  "[Next]=01:09:00;\n");
  auto *zero = fixture.events.data;
  auto *frame23 = zero->next;
  auto *a03 = frame23->next;
  auto *next = a03->next;
  Require(zero->startTick == 0, "00:00:00 is not tick 0");
  Require(frame23->startTick == 23 && frame23->endTick == 24,
          "00:00:23 or 00:01:00 conversion");
  Require(a03->startTick == 1497 && a03->endTick == 1656 &&
              next->startTick == 1656,
          "A03 tick conversion");

  Fixture unusual("[PrintText]=00:20:26\tA\tText\t00:21:02;\n");
  Require(unusual.events.size == 1 && unusual.events.data->startTick == 506 &&
              unusual.events.data->endTick == 506,
          "frame field above 23 was not arithmetically carried");

  Require(Kotonoha_SceneTickToMicroseconds(24) == 1000000 &&
              Kotonoha_SceneTickToMicroseconds(1497) == 62375000,
          "absolute microsecond conversion");
  Require(Kotonoha_SceneTickToMillisecondsCeil(1) == 42 &&
              Kotonoha_MillisecondsToSceneTick(41) == 0 &&
              Kotonoha_MillisecondsToSceneTick(42) == 1 &&
              Kotonoha_SceneTickToMillisecondsCeil(1497) == 62375 &&
              Kotonoha_SceneTickToMillisecondsCeil(1656) == 69000,
          "absolute millisecond conversion");
  Require(Kotonoha_SceneTickToMillisecondsCeil(24000) == 1000000,
          "long timeline drifted");
}

void TestA03MarkersAndSkipTarget() {
  Fixture fixture("[SkipFRAME]=01:02:09;\n"
                  "[SetSELECT]=01:02:09\t'First'\tnull\t01:08:00;\n"
                  "[PlayVoice]=01:05:07\tVoice/path\t1\tmak\t01:07:00;\n"
                  "[Next]=01:09:00;\n");
  Require(fixture.events.size == 4, "A03 fixture parse");
  SchoolDaysSceneTimeline timeline;
  timeline.Bind(&fixture.events);
  Require(timeline.SkipFrameTick() == 1497 && timeline.NextTick() == 1656,
          "markers were merged");
  Require(timeline.ComputeSkipToEndTarget(1240) == 1473,
          "known pre-SkipFRAME target");
  Require(timeline.ComputeSkipToEndTarget(1497) == 1473,
          "choice-boundary click must return to the native pre-choice target");
  Require(timeline.ComputeSkipToEndTarget(1550) == 1656,
          "post-choice forward action must target Next");

  /* Resource preparation must not count as timeline dispatch. */
  fixture.events.last->eventPrepared = true; /* Next, prepared ahead of time. */
  fixture.events.last->prev->eventPrepared = true; /* The later voice. */

  Require(timeline.AdvanceTo(1496).empty() && !timeline.ReachedNext(1496),
          "scene ended before SkipFRAME");
  auto atMarker = timeline.AdvanceTo(1497);
  Require(atMarker.size() == 2 && atMarker[0]->command == SkipFRAME &&
              atMarker[1]->command == SetSELECT && !timeline.ReachedNext(1497),
          "SkipFRAME ended scene or same-tick order changed");
  auto voice = timeline.AdvanceTo(1567);
  Require(voice.size() == 1 && voice[0]->command == PLAY_VOICE &&
              !timeline.ReachedNext(1567),
          "post-SkipFRAME PlayVoice was lost");
  auto atNext = timeline.AdvanceTo(1656);
  Require(atNext.size() == 1 && atNext[0]->command == Next &&
              timeline.ReachedNext(1656) && timeline.ReadyToEnd(1656),
          "Next did not end scene");
  Require(timeline.AdvanceTo(1700).empty(),
          "events were dispatched after Next");
}

void TestEqualMarkersAndSeek() {
  Fixture equal("[SkipFRAME]=00:08:00;\n[Next]=00:08:00;\n");
  SchoolDaysSceneTimeline timeline;
  timeline.Bind(&equal.events);
  Require(timeline.ComputeSkipToEndTarget(100) == 192 &&
              !timeline.ComputeSkipToEndTarget(192).has_value(),
          "equal marker skip target");
  auto same = timeline.AdvanceTo(192);
  Require(same.size() == 2 && same[0]->command == SkipFRAME &&
              same[1]->command == Next && timeline.ReadyToEnd(192),
          "equal markers lost source order or scene end");

  Fixture jumps("[PlaySe]=00:04:05\t1\tSe/a\t00:04:06;\n"    // tick 101
                "[PlaySe]=00:04:06\t1\tSe/b\t00:04:08;\n"    // tick 102
                "[PrintText]=00:04:09\tA\tText\t00:04:12;\n" // tick 105
                "[PlayVoice]=00:04:09\tVoice/a\t1\tmak\t00:04:12;\n"
                "[Next]=00:05:00;\n");
  timeline.Bind(&jumps.events);
  Require(timeline.AdvanceTo(100).empty(), "early event delivery");
  auto crossed = timeline.AdvanceTo(105);
  Require(crossed.size() == 4 && crossed[0]->startTick == 101 &&
              crossed[1]->startTick == 102 &&
              crossed[2]->command == PRINT_TEXT &&
              crossed[3]->command == PLAY_VOICE,
          "multi-tick jump lost events or reordered a tie");
  Require(timeline.AdvanceTo(105).empty(), "duplicate dispatch");
  timeline.ResetToTick(102);
  auto afterSeek = timeline.AdvanceTo(105);
  Require(afterSeek.size() == 2 && afterSeek[0]->command == PRINT_TEXT &&
              afterSeek[1]->command == PLAY_VOICE,
          "seek cursor duplicated prior events or lost future events");
  timeline.RebuildActiveAt(102);
  auto active = timeline.AdvanceTo(102);
  Require(active.size() == 1 && active[0]->startTick == 102,
          "active-at-target media rebuild cursor");
  timeline.RebuildActiveAt(105);
  auto rebuilt = timeline.AdvanceTo(105);
  Require(rebuilt.size() == 2 && rebuilt[0]->command == PRINT_TEXT &&
              rebuilt[1]->command == PLAY_VOICE,
          "rebuild dispatched ended transient events from skipped interval");
  timeline.Restart();
  Require(timeline.AdvanceTo(101).size() == 1,
          "scene restart did not reset cursor");
}

void TestRetailOrsFadePresentationBoundary() {
  // Original Script.GPK 01/01-00-C03 has an authored BlackFade OUT from tick72
  // through endTick96 and Next at exactly tick96. Retail expires the fade at
  // 96, but the v3 runtime owns one additional outgoing frame before Advance.
  Fixture fixture("[BlackFade]=00:03:00\tOUT\t00:04:00;\n"
                  "[Next]=00:04:00;\n");
  SchoolDaysSceneTimeline timeline;
  timeline.Bind(&fixture.events);
  const auto *fade = fixture.events.data;
  Require(fade->command == BLACK_FADE && fade->startTick == 72 &&
              fade->endTick == 96 && timeline.NextTick() == 96,
          "retail boundary fixture did not preserve authored 24 fps ticks");
  using Policy = Kotonoha::SchoolDaysOrsFadeBoundary;
  Require(!Policy::CoversDeferredNext(*fade, 95, 96, true) &&
              !Policy::CoversDeferredNext(*fade, 96, 96, false) &&
              Policy::CoversDeferredNext(*fade, 96, 96, true),
          "deferred one-frame black boundary cover is missing or premature");

  Fixture gap("[BlackFade]=00:03:00\tOUT\t00:03:23;\n"
              "[Next]=00:04:00;\n");
  Require(!Policy::CoversDeferredNext(*gap.events.data, 96, 96, true),
          "a genuine authored gap was lengthened beyond its retail fade end");
  Fixture fadeIn("[WhiteFade]=00:03:00\tIN\t00:04:00;\n"
                 "[Next]=00:04:00;\n");
  Require(!Policy::CoversDeferredNext(*fadeIn.events.data, 96, 96, true),
          "an IN fade was incorrectly converted into a terminal cover");
  Fixture whiteOut("[WhiteFade]=00:03:00\tOUT\t00:04:00;\n"
                   "[Next]=00:04:00;\n");
  Require(Policy::CoversDeferredNext(*whiteOut.events.data, 96, 96, true),
          "white terminal OUT frame lost the same native scene-boundary rule");
}

void TestRetailOrsFadeIntegerAlpha() {
  // Authenticated A00 commercial EXE sample (FADE_RE.md): 12-tick IN,
  // floor integer alpha at fixed 24fps; endTick is EXCLUSIVE.
  Fixture intro("[BlackFade]=00:04:05\tIN\t00:04:17;\n"
                "[Next]=00:05:00;\n");
  auto *fade = intro.events.data;
  using Model = Kotonoha::SchoolDaysOrsFadeFrame;
  Require(!Model::AtTick(intro.events, 101, 120, false).drawable,
          "a fade which has not been dispatched must remain hidden");
  fade->eventTouched = true;
  const int nativeAlpha[] = {255, 234, 213, 192, 170, 149,
                             128, 107, 85, 64, 43, 22};
  for (int i = 0; i < 12; ++i) {
    const auto sample = Model::AtTick(intro.events, 101 + i, 120, false);
    Require(sample.drawable && !sample.deferredNextCover &&
                sample.color == 0 && sample.alpha == nativeAlpha[i],
            "retail A00 integer-tick fade IN alpha drifted");
  }
  Require(!Model::AtTick(intro.events, 113, 120, false).drawable,
          "fade IN must vanish precisely at its exclusive endTick");

  Fixture outbound("[WhiteFade]=00:03:00\tOUT\t00:04:00;\n"
                   "[Next]=00:04:00;\n");
  outbound.events.data->eventTouched = true;
  const auto half = Model::AtTick(outbound.events, 84, 96, false);
  const auto last = Model::AtTick(outbound.events, 94, 96, false);
  Require(half.drawable && half.color == 255 && half.alpha == 127 &&
              last.alpha == 233,
          "24fps fade OUT must truncate integer coverage, not round/lerp fractional ticks");
  Require(!Model::AtTick(outbound.events, 96, 96, false).drawable,
          "native OUT fade may not continue at the exclusive terminal tick");
  const auto retired = Model::AtTick(outbound.events, 96, 96, true);
  Require(retired.drawable && retired.deferredNextCover &&
              retired.color == 255 && retired.alpha == 255,
          "synthetic deferred Next frame must retain authored white terminal color");

  // Retail 01/01-00-E06 OUT expires tick1005, while Next occurs tick1006;
  // the authored 1-tick gap MUST NOT be filled in by our scene-boundary fix.
  Fixture gap("[BlackFade]=00:40:21\tOUT\t00:41:21;\n"
              "[Next]=00:41:22;\n");
  gap.events.data->eventTouched = true;
  Require(!Model::AtTick(gap.events, 1005, 1006, false).drawable &&
              !Model::AtTick(gap.events, 1006, 1006, true).drawable,
          "scripted gaps must stay visible after an expired fade OUT");
}
} // namespace

int main() {
  try {
    TestTicks();
    TestA03MarkersAndSkipTarget();
    TestEqualMarkersAndSeek();
    TestRetailOrsFadePresentationBoundary();
    TestRetailOrsFadeIntegerAlpha();
    std::cout << "SchoolDaysSceneTimelineTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysSceneTimelineTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
