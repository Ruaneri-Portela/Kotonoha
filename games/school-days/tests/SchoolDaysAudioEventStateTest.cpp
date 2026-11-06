#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysMoveSomState.hpp>
#include <Kotonoha/routing/SchoolDaysAbcAnimation.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const char *message) {
  std::cerr << "SchoolDaysAudioEventStateTest: " << message << '\n';
  std::exit(2);
}

void Check(bool value, const char *message) {
  if (!value)
    Fail(message);
}

void TestAbcPcmGeometry() {
  using namespace Kotonoha;

  Check(SchoolDaysAbcActivityTime100ns(1) == 416667 &&
            SchoolDaysAbcActivityTime100ns(2) == 833333 &&
            SchoolDaysAbcActivityTime100ns(3) == 1250000,
        "ABC 100 ns activity timestamp rounding drifted");

  std::vector<int16_t> pcm(22050, 0);
  pcm[0] = 60;
  pcm[1917] = 61;
  pcm[3834] = -60;
  pcm[5752] = -59;
  auto records = SchoolDaysAnalyzeAbcPcmActivity(pcm);
  Check(records.size() == 12, "ABC full 44100-byte chunk record count drifted");
  Check(!records[0].active && records[1].active && records[2].active &&
            !records[3].active,
        "ABC signed s16 thresholds drifted");
  Check(!SchoolDaysAbcSamplePcmChunk(pcm, 0, 44100, 23),
        "ABC sample index >= byte block was accepted");
  Check(SchoolDaysAbcVoiceActiveAt(records, 1) &&
            !SchoolDaysAbcVoiceActiveAt(records, 12),
        "ABC activity query after media EOF drifted");
  Check(!SchoolDaysAbcVoiceActiveAt({}, 0),
        "empty ABC activity unexpectedly reported active");

  pcm.resize(22050 + 10000, 0);
  records = SchoolDaysAnalyzeAbcPcmActivity(pcm);
  Check(records.size() == 17, "ABC partial final chunk record count drifted");
  for (size_t n = 12; n < records.size(); ++n)
    Check(!records[n].active,
          "ABC partial final chunk negative index became active");
  Check(!SchoolDaysAbcSamplePcmChunk(pcm, 44100, 20000, 12),
        "ABC negative local sample index was accepted");

  std::vector<int16_t> fullTwoChunks(44100, 0);
  fullTwoChunks[22050 + 958] = 61;
  records = SchoolDaysAnalyzeAbcPcmActivity(fullTwoChunks);
  Check(records.size() == 24 && records[12].active,
        "ABC P/2 offset in second full chunk drifted");

  // Decoder AVFrame boundaries are irrelevant once the sample stream has
  // been normalized. Reassemble uneven fragments and require bit identity.
  std::vector<int16_t> split;
  for (size_t pos = 0; pos < fullTwoChunks.size(); pos += 1073) {
    split.insert(split.end(), fullTwoChunks.begin() + pos,
                 fullTwoChunks.begin() +
                     std::min(pos + size_t(1073), fullTwoChunks.size()));
  }
  const auto repeated = SchoolDaysAnalyzeAbcPcmActivity(split);
  Check(repeated.size() == records.size(),
        "ABC AVFrame-size independence changed record count");
  for (size_t n = 0; n < records.size(); ++n)
    Check(repeated[n].active == records[n].active,
          "ABC AVFrame-size independence changed activity value");
}

void TestAbcSchedulerAndKeys() {
  using namespace Kotonoha;

  Check(SchoolDaysAbcAnimationUpdatesAtRate(1.0) &&
            SchoolDaysAbcAnimationUpdatesAtRate(2.0) &&
            SchoolDaysAbcAnimationUpdatesAtRate(4.0) &&
            SchoolDaysAbcAnimationUpdatesAtRate(11.999) &&
            !SchoolDaysAbcAnimationUpdatesAtRate(12.0) &&
            !SchoolDaysAbcAnimationUpdatesAtRate(24.0) &&
            !SchoolDaysAbcAnimationUpdatesAtRate(
                std::numeric_limits<double>::infinity()) &&
            !SchoolDaysAbcAnimationUpdatesAtRate(
                std::numeric_limits<double>::quiet_NaN()),
        "ABC ImageChar playback-rate gate drifted");

  std::vector<SchoolDaysAbcActivityRecord> active;
  for (unsigned n = 0; n < 16; ++n)
    active.push_back({n, true});
  const auto at2 = SchoolDaysComputeAbcAnimationAt(active, 1, 2);
  const auto at3 = SchoolDaysComputeAbcAnimationAt(active, 1, 3);
  const auto at6 = SchoolDaysComputeAbcAnimationAt(active, 1, 6);
  const auto at9 = SchoolDaysComputeAbcAnimationAt(active, 1, 9);
  Check(at2.activeIndex == 0 && at3.activeIndex == 1 && at6.activeIndex == 2 &&
            at9.activeIndex == 0,
        "ABC 24 Hz graphic tick / 3 A-B-C-A sequence drifted");

  active[3].active = false; // scene tick 4 for voiceStart=1
  const auto idle = SchoolDaysComputeAbcAnimationAt(active, 1, 4);
  const auto resumed = SchoolDaysComputeAbcAnimationAt(active, 1, 6);
  Check(idle.activeIndex == 0 && idle.counter == 1 && resumed.activeIndex == 2,
        "ABC idle convergence or retained counter drifted");
  Check(SchoolDaysComputeAbcAnimationAt(active, 1, 6).counter ==
            resumed.counter,
        "ABC seek reconstruction became nondeterministic");

  SchoolDaysAbcAnimationState mak, tai;
  SchoolDaysAdvanceAbcAnimation(mak, active, 1, 3);
  SchoolDaysAdvanceAbcAnimation(tai, active, 10, 10);
  Check(mak.counter == 1 && tai.counter == 0,
        "independent ABC keys stopped keeping independent counters");

  SchoolDaysAbcAnimationState beforeVoice;
  beforeVoice.counter = 7;
  beforeVoice.activeIndex = 2;
  SchoolDaysAdvanceAbcAnimation(beforeVoice, active, 10, 9);
  Check(beforeVoice.counter == 7 && beforeVoice.activeIndex == 2,
        "ABC pre-voice tick mutated state");

  SchoolDaysAbcAnimationCursor cursor;
  SchoolDaysUpdateAbcAnimation(cursor, active, 1, 3, 1.0);
  Check(cursor.initialized && cursor.lastTick == 3 &&
            cursor.state.counter == 1 && cursor.state.activeIndex == 1,
        "ABC forward cursor did not reach the first B state");
  SchoolDaysUpdateAbcAnimation(cursor, active, 1, 9, 12.0);
  Check(
      cursor.lastTick == 9 && cursor.state.counter == 1 &&
          cursor.state.activeIndex == 1,
      "ABC 12x freeze changed the selected frame or retained old cursor time");
  SchoolDaysUpdateAbcAnimation(cursor, active, 1, 12, 1.0);
  Check(cursor.lastTick == 12 && cursor.state.counter == 2 &&
            cursor.state.activeIndex == 2,
        "ABC post-freeze resume replayed skipped >=12x updates");

  Check(SchoolDaysNormalizeAbcAnimationKey("MAK") == "mak" &&
            SchoolDaysNormalizeAbcAnimationKey("x01") == "x01" &&
            SchoolDaysNormalizeAbcAnimationKey("X01") == "x01" &&
            SchoolDaysNormalizeAbcAnimationKey("xxx").empty() &&
            SchoolDaysNormalizeAbcAnimationKey("XXX").empty() &&
            SchoolDaysNormalizeAbcAnimationKey("").empty(),
        "ABC animation-key normalization drifted");
}

void TestAbcResourceGrouping() {
  using namespace Kotonoha;

  SchoolDaysAbcGroupResources group;
  group.baseStem = "BASE";
  group.key = "mak";
  Check(SchoolDaysAssignAbcResource(group, 0, "BASEMAK.A.PNG"),
        "first ABC resource assignment failed");
  Check(SchoolDaysAssignAbcResource(group, 0, "BASEMAK.A.PNG") &&
            !group.collision,
        "idempotent ABC resource assignment became a collision");
  Check(!SchoolDaysAssignAbcResource(group, 0, "basemak.a.png") &&
            group.collision && !group.Complete(),
        "fold-equivalent ABC sibling collision did not fail closed");
  Check(!SchoolDaysAssignAbcResource(group, 3, "invalid.PNG"),
        "out-of-range ABC state was accepted");

  SchoolDaysAbcSibling parsed;
  Check(SchoolDaysParseAbcSibling("Event/04-KA-F07-002B.PNG",
                                  "Event/04-KA-F07-002BX01.A.PNG", &parsed) &&
            parsed.baseStem == "04-KA-F07-002B" && parsed.key == "x01" &&
            parsed.state == 0,
        "generic x01 ABC sibling parsing drifted");
  Check(!SchoolDaysParseAbcSibling("Event/04-KA-F07-002B.PNG",
                                   "Event/04-KA-F07-002B.A.PNG", &parsed),
        "empty ABC key fallback was fabricated");
  Check(!SchoolDaysParseAbcSibling("Event/base.PNG", "Event/baseMAK.D.PNG",
                                   &parsed) &&
            !SchoolDaysParseAbcSibling("Event/base.JPG", "Event/baseMAK.A.PNG",
                                       &parsed),
        "invalid ABC state/extension was accepted");

  const std::vector<std::string> siblings{
      "Event/04-KA-F07-002BX01.A.PNG", "Event/04-KA-F07-002BX01.B.PNG",
      "Event/04-KA-F07-002BMAK.A.PNG", "Event/04-KA-F07-002BMAK.B.PNG",
      "Event/04-KA-F07-002BMAK.C.PNG", "Event/unrelated.A.PNG",
  };
  const auto groups =
      SchoolDaysGroupAbcSiblings("Event/04-KA-F07-002B.PNG", siblings);
  Check(groups.size() == 2 && groups[0].key == "mak" && groups[0].Complete() &&
            groups[1].key == "x01" && !groups[1].Complete() &&
            groups[1].paths[0].find("X01.A.PNG") != std::string::npos &&
            groups[1].paths[2].empty(),
        "ABC sibling grouping lost complete/partial group semantics");

  // 05-KC-A02-009 has an authored KOT key. Bare 009.A/B/C must never be
  // treated as a fallback group when the voice association is "kot".
  const auto kc = SchoolDaysGroupAbcSiblings(
      "Event05/05-KC/05-KC-A02/05-KC-A02-009.PNG",
      {"Event05/05-KC/05-KC-A02/05-KC-A02-009KOT.A.PNG",
       "Event05/05-KC/05-KC-A02/05-KC-A02-009KOT.B.PNG",
       "Event05/05-KC/05-KC-A02/05-KC-A02-009KOT.C.PNG",
       "Event05/05-KC/05-KC-A02/05-KC-A02-009.A.PNG"});
  Check(kc.size() == 1 && kc[0].key == "kot" && kc[0].Complete(),
        "009 ABC grouping fabricated an uncoded fallback instead of KOT");

  const auto colliding = SchoolDaysGroupAbcSiblings(
      "Event/base.PNG", {"Event/baseMAK.A.PNG", "Event/BASEmak.a.png"});
  Check(colliding.size() == 1 && colliding[0].key == "mak" &&
            colliding[0].collision && !colliding[0].Complete(),
        "ABC casefold-equivalent sibling collision was not preserved");
}

} // namespace

int main() {
  using Kotonoha::SchoolDaysSeSlots;

  Check(Kotonoha::SchoolDaysVoiceAllowed(0, false),
        "numeric zero voice must ignore MenVoice");
  Check(Kotonoha::SchoolDaysVoiceAllowed(1, true) &&
            !Kotonoha::SchoolDaysVoiceAllowed(1, false) &&
            !Kotonoha::SchoolDaysVoiceAllowed(7, false),
        "nonzero voice MenVoice gate drifted");

  Kotonoha_orsTypePlayVoice voiceData{};
  char animationKey[] = "mak";
  voiceData.character_short = animationKey;
  Kotonoha_orsEvent voice{};
  voice.command = PLAY_VOICE;
  voice.data.play_voice = &voiceData;
  Check(std::string(Kotonoha::SchoolDaysVoiceAnimationKey(&voice)) == "mak",
        "voice animation association key drifted");
  Check(std::string(Kotonoha::SchoolDaysVoiceAnimationKey(nullptr)).empty(),
        "null voice event did not fail closed");

  TestAbcPcmGeometry();
  TestAbcSchedulerAndKeys();
  TestAbcResourceGrouping();

  Check(SchoolDaysSeSlots::Count == 9u && SchoolDaysSeSlots::Valid(0) &&
            SchoolDaysSeSlots::Valid(8) && !SchoolDaysSeSlots::Valid(9),
        "retail SE slot domain must be exactly 0..8");

  Kotonoha_orsTypePlaySe seAData{}, seBData{}, seCData{};
  seAData.a = 2;
  seBData.a = 2;
  seCData.a = 3;

  Kotonoha_orsEvent a{}, b{}, c{};
  a.command = PLAY_SE;
  a.startTick = 10;
  a.endTick = 20;
  a.data.play_se = &seAData;
  a.next = &c;

  c.command = PLAY_SE;
  c.startTick = 12;
  c.endTick = 21;
  c.data.play_se = &seCData;
  c.next = &b;

  b.command = PLAY_SE;
  b.startTick = 14;
  b.endTick = 22;
  b.data.play_se = &seBData;

  Kotonoha_orsData source{};
  source.data = &a;
  source.last = &b;
  source.size = 3;

  int fakeA = 1;
  int fakeB = 2;
  int fakeC = 3;
  auto *mediaA = reinterpret_cast<Kotonoha_audioDecode *>(&fakeA);
  auto *mediaB = reinterpret_cast<Kotonoha_audioDecode *>(&fakeB);
  auto *mediaC = reinterpret_cast<Kotonoha_audioDecode *>(&fakeC);

  SchoolDaysSeSlots slots;
  SchoolDaysSeSlots::Slot previous;
  Check(slots.Replace(2, &a, mediaA, &previous) && previous.event == nullptr,
        "initial SE slot replacement failed");
  Check(slots.Replace(3, &c, mediaC, &previous) && slots.At(2).event == &a &&
            slots.At(3).event == &c,
        "independent SE slots stopped coexisting");
  Check(slots.Replace(2, &b, mediaB, &previous) && previous.event == &a &&
            previous.media == mediaA && slots.At(2).event == &b &&
            slots.At(3).media == mediaC,
        "latest same-slot SE did not replace the previous occupant");
  Check(!slots.Replace(9, &a, nullptr) && slots.At(9).event == nullptr,
        "out-of-range SE slot was accepted");

  const auto at13 = SchoolDaysSeSlots::ActiveEventsAt(
      &source, Kotonoha_SceneTickToMillisecondsCeil(13));
  Check(at13[2] == &a && at13[3] == &c,
        "seek reconstruction lost pre-replacement active slots");
  const auto at14 = SchoolDaysSeSlots::ActiveEventsAt(
      &source, Kotonoha_SceneTickToMillisecondsCeil(14));
  Check(at14[2] == &b && at14[3] == &c,
        "seek reconstruction revived replaced slot occupant");
  const auto at23 = SchoolDaysSeSlots::ActiveEventsAt(
      &source, Kotonoha_SceneTickToMillisecondsCeil(23));
  Check(at23[2] == nullptr && at23[3] == nullptr,
        "expired SE slot occupants revived after seek");

  slots.Clear();
  Check(slots.At(2).event == nullptr && slots.At(3).event == nullptr,
        "SE slot reset retained occupants");

  Kotonoha_orsTypeMoveSom somData{};
  somData.numeric = 5;
  Kotonoha_orsEvent som{};
  som.command = MOVE_SOM;
  som.startTick = 30;
  som.endTick = 40;
  som.data.move_som = &somData;
  Kotonoha::SchoolDaysMoveSomNoOp moveSom;
  moveSom.Start(&som);
  Check(moveSom.Active(&som), "MOVE_SOM no-op window did not start");
  moveSom.AdvanceTo(39);
  Check(moveSom.Active(&som),
        "MOVE_SOM no-op window ended before authored END");
  moveSom.AdvanceTo(40);
  Check(!moveSom.Active(&som), "MOVE_SOM no-op window survived authored END");
  moveSom.Start(&som);
  moveSom.Reset();
  Check(!moveSom.Active(&som), "MOVE_SOM no-op reset retained state");

  std::cout << "SCHOOL DAYS AUDIO EVENT STATE GATE PASS\n"
            << "voice_menvoice_gate=PASS\n"
            << "voice_animation_key=PASS\n"
            << "abc_pcm_geometry=PASS\n"
            << "abc_scheduler_keys=PASS\n"
            << "abc_resource_grouping=PASS\n"
            << "se_slot_domain_0_8=PASS\n"
            << "same_slot_replacement=PASS\n"
            << "seek_reconstruction=PASS\n"
            << "move_som_noop_window=PASS\n";
  return 0;
}
