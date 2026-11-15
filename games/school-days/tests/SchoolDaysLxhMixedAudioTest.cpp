#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void CheckIntegratedAudioManifest() {
  const auto resources = Kotonoha::SchoolDaysLxhContent::PackEntries();
  // Each ending contributes an ORS, a final card, a silent WMV and ONE OGG.
  // There is no separately addressable per-character audio in this manifest.
  Require(resources.size() == 28, "LxH resource manifest size changed");
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    const std::string stem = "LxH/Media/" + std::string(ending) + "-LXH";
    Require(std::count(resources.begin(), resources.end(), stem + ".ogg") == 1,
            "LxH soundtrack missing or duplicated");
    Require(std::count(resources.begin(), resources.end(), stem + ".wmv") == 1,
            "LxH movie missing or duplicated");
  }
}

void CheckSyntheticVoiceAndMixedSe() {
  // This deliberately reproduces the *event types*, not any commercial text
  // or audio. The native male voice has its own numbered PLAY_VOICE event;
  // the LxH episode has a separate PLAY_SE containing the full soundtrack.
  const std::string orsText =
      "[PlayVoice]=00:00:00\tVoice05/mock-0001\t1\tmak\t00:00:10;\n"
      "[PrintText]=00:00:00\tMakoto\tNative test.\t00:00:10;\n"
      "[PlayMovie]=00:00:12\tLxH/Media/05-A2-F00-LXH\t0\t00:02:12;\n"
      "[PlaySe]=00:00:12\t1\tLxH/Media/05-A2-F00-LXH\t00:02:12;\n"
      "[PrintText]=00:00:18\tMakoto\tMale test.\t00:00:28;\n"
      "[PrintText]=00:00:26\tSekai\tFemale overlap.\t00:00:34;\n"
      "[Next]=00:02:18;\n";
  Kotonoha::OrsDocument script(
      Kotonoha_OrsParserBytes(orsText.data(), orsText.size()));
  Require(script.get().size == 7, "Synthetic ORS event count changed");

  const Kotonoha_orsEvent *nativeVoice = nullptr;
  const Kotonoha_orsEvent *epilogueMovie = nullptr;
  const Kotonoha_orsEvent *epilogueSe = nullptr;
  const Kotonoha_orsEvent *maleText = nullptr;
  const Kotonoha_orsEvent *femaleText = nullptr;
  for (const auto *event = script.get().data; event; event = event->next) {
    if (event->command == PLAY_VOICE) nativeVoice = event;
    if (event->command == PLAY_MOVIE) epilogueMovie = event;
    if (event->command == PLAY_SE) epilogueSe = event;
    if (event->command == PRINT_TEXT && event->data.print_text) {
      const std::string speaker = event->data.print_text->character;
      if (speaker == "Makoto" && event->startTick > 0) maleText = event;
      if (speaker == "Sekai") femaleText = event;
    }
  }

  Require(nativeVoice && nativeVoice->data.play_voice &&
              nativeVoice->data.play_voice->a == 1 &&
              !Kotonoha::SchoolDaysVoiceAllowed(
                  nativeVoice->data.play_voice->a, false) &&
              Kotonoha::SchoolDaysVoiceAllowed(
                  nativeVoice->data.play_voice->a, true),
          "Native numbered male PlayVoice must honor MenVoice");
  Require(epilogueMovie && epilogueMovie->data.play_movie && epilogueSe &&
              epilogueSe->data.play_se &&
              epilogueSe->command != PLAY_VOICE &&
              epilogueSe->data.play_se->a == 1 &&
              epilogueSe->startTick == epilogueMovie->startTick,
          "LxH soundtrack must remain a concurrent slot-one PlaySe");
  const std::string soundtrack =
      std::string(epilogueSe->data.play_se->path) + ".ogg";
  Require(Kotonoha::SchoolDaysLxhContent::IsPackResource(soundtrack),
          "LxH mixed soundtrack must resolve through its real OGG resource");

  Require(maleText && femaleText &&
              maleText->startTick < femaleText->endTick &&
              femaleText->startTick < maleText->endTick,
          "Synthetic overlapping speakers must remain simultaneous");
  // A per-dialogue mute applied to the single PlaySe would silence the
  // female speaker too, as well as any ambience mixed in the same stream.
  // The test intentionally does not pretend that PRINT_TEXT offers audio stems.
}

} // namespace

int main() {
  try {
    CheckIntegratedAudioManifest();
    CheckSyntheticVoiceAndMixedSe();
    std::cout << "SchoolDaysLxhMixedAudioTest PASS: separate native PlayVoice "
                 "gate, integrated LxH PlaySe soundtrack and overlapping cues\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLxhMixedAudioTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
