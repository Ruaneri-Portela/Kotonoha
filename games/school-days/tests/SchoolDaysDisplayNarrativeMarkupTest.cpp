#include "SchoolDaysScenes.hpp"

#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/routing/SchoolDaysDisplayNarrativeMarkup.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Markup = Kotonoha::SchoolDaysDisplayNarrativeMarkup;

void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void Expect(const std::string &source, const std::string &displayed) {
  Require(Markup::Project(source) == displayed,
          "Narrative display projection changed a literal or retained a marker");
}

void TestMarkers() {
  Expect("Makoto?^", "Makoto?");
  Expect("¿Makoto?^", "¿Makoto?");
  Expect("Juan Ochoa (JNC)...2021^ \t\r\n", "Juan Ochoa (JNC)...2021 \t\r\n");
  Expect("Hola^\\nOtra línea^", "Hola\\nOtra línea");
  Expect("Hola^\\NOtra línea^", "Hola\\NOtra línea");
  Expect("Hola^\nOtra línea^", "Hola\nOtra línea");
  Expect("Hola^\r\nOtra línea^", "Hola\r\nOtra línea");
  Expect("{\\i1}¿Makoto?^\\N{\\i0}2^3", "{\\i1}¿Makoto?\\N{\\i0}2^3");
  Expect("2^3", "2^3");
  Expect("^prefix and mid^dle", "^prefix and mid^dle");
  Expect("line^ \ncontinued", "line^ \ncontinued");
  Expect("before^\\hafter", "before^\\hafter");
  Expect("^", "");
  Expect("double^^", "double^");
  Expect("A^   ", "A   ");
  Expect("\xE2\x80\x8B", "");
  Expect("A\xE2\x80\x8B" "B", "A\xE2\x80\x8B" "B");
}

void TestLxhCredit() {
  const std::string credit = "Juan Ochoa (JNC)- LxH .2021^";
  Require(Markup::SuppressLxhCredit(credit, credit, "en"),
          "English LxH credit must be hidden");
  Require(Markup::SuppressLxhCredit(credit, "...", "zh-cn"),
          "Translated LxH credit must be identified by the source line");
  Require(Markup::SuppressLxhCredit("Other source", " " + credit + "\r\n", "en"),
          "A localized credit must also be recognized");
  Require(!Markup::SuppressLxhCredit(credit, credit, "es") &&
              !Markup::SuppressLxhCredit(credit, credit, "es-419") &&
              Markup::Project(credit) == "Juan Ochoa (JNC)- LxH .2021",
          "Spanish must retain the LxH credit without the display marker");
  Require(!Markup::SuppressLxhCredit("Other source", "Other dialogue", "zh-cn"),
          "Unrelated LxH dialogue must be shown");
  // The live subtitle lookup must apply the same locale rule, not merely
  // expose an unused helper. Keep the original ORS and JSON values intact.
  Kotonoha::Translation translated, epilogue;
  epilogue.mutableSections()["5"]["@ors:12:0"] = credit;
  const std::vector<Kotonoha::SubtitleCue> source{
      {0, 1, credit, "Makoto", true}};
  for (const auto locale : {"en", "zh-cn"}) {
    const auto cues = SchoolDays::Subtitles::TranslateCues(
        source, {12}, 5, translated, &epilogue, 10, locale);
    Require(cues.size() == 1 && cues[0].text == "\xE2\x80\x8B" &&
                cues[0].speaker == "Makoto" && source[0].text == credit,
            "Runtime cue lookup leaked a Spanish-only LxH attribution");
  }
  const auto spanishCues = SchoolDays::Subtitles::TranslateCues(
      source, {12}, 5, translated, &epilogue, 10, "es");
  Require(spanishCues.size() == 1 && spanishCues[0].text == credit,
          "Spanish LxH attribution was suppressed in the actual cue lookup");
}

void TestSubtitleAndBackLogProjection() {
  Kotonoha::Translation english, spanish;
  const std::string source = "¿Makoto?^";
  english.mutableSections()["5"][source] = "Makoto?^";
  spanish.mutableSections()["5"][source] = source;
  english.mutableSections()["5"]["2^3"] = "2^3";
  spanish.mutableSections()["5"]["2^3"] = "2^3";
  const std::vector<Kotonoha::SubtitleCue> original{
      {0, 1, source, "Sekai", true},
      {1, 2, "2^3", "Makoto", true}};
  const auto en = SchoolDays::Subtitles::TranslateCues(
      original, {4, 5}, 5, english);
  const auto es = SchoolDays::Subtitles::TranslateCues(
      original, {4, 5}, 5, spanish);

  // Translation identity and speaker/ASS styling are deliberately untouched.
  Require(original[0].text == source && en[0].text == "Makoto?^" &&
              es[0].text == source &&
              en[0].speaker == "Sekai" && es[0].speaker == "Sekai" &&
              en[0].assText && es[0].assText &&
              english.text(5, source) == "Makoto?^" &&
              spanish.text(5, source) == source,
          "Subtitle source, JSON dictionary or canonical style ID mutated");
  Require(Markup::Project(en[0].text) == "Makoto?" &&
              Markup::Project(es[0].text) == "¿Makoto?" &&
              Markup::Project(en[1].text) == "2^3",
          "Post-lookup subtitle display projection failed");

  Kotonoha::SchoolDaysBackLogStore narrative, playData;
  const std::string displayed = Markup::Project(en[0].text);
  const std::u16string projectedUtf16(displayed.begin(), displayed.end());
  narrative.AppendLogMessage(0, u"Sekai", projectedUtf16);
  playData.AppendLogMessage(1, u"Sekai", projectedUtf16);
  Require(displayed == "Makoto?" &&
              narrative.At(0) && narrative.At(0)->text == u"Makoto?" &&
              narrative.At(0)->character == u"Sekai" &&
              playData.At(0) && playData.At(0)->text == u"Makoto?" &&
              playData.At(0)->character == u"Sekai",
          "BackLog or PlayData projected speaker/text differs from subtitles");
}

} // namespace

int main() {
  try {
    TestMarkers();
    TestLxhCredit();
    TestSubtitleAndBackLogProjection();
    std::cout << "School Days display narrative markup PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysDisplayNarrativeMarkupTest: " << error.what()
              << '\n';
    return 1;
  }
}
