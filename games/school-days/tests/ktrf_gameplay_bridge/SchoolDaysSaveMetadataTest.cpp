#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Metadata = Kotonoha::SchoolDaysSaveMetadata;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SCHOOL DAYS SAVE METADATA GATE FAIL: " << message << "\n";
    std::exit(1);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

} // namespace

int main() {
    Check(Metadata::StoryFor(0, 0) == 1, "route0 story mismatch");
    Check(Metadata::StoryFor(1, 0) == 2 && Metadata::StoryFor(3, 0) == 2,
          "route1..3 story mismatch");
    Check(Metadata::StoryFor(4, 0) == 3 && Metadata::StoryFor(5, 0) == 3,
          "route4..5 story mismatch");
    Check(Metadata::StoryFor(6, 0) == 4 && Metadata::StoryFor(18, 0) == 4,
          "route6..18 story mismatch");
    Check(Metadata::StoryFor(19, 0) == 5 && Metadata::StoryFor(31, 0) == 5,
          "route19..31 story mismatch");
    Check(Metadata::StoryFor(32, 0) == 6 && Metadata::StoryFor(54, 0) == 6,
          "route32..54 story mismatch");
    Check(Metadata::StoryFor(-1, 0) == 0 && Metadata::StoryFor(55, 0) == 0 &&
              Metadata::StoryFor(-1, 1) == 6 && Metadata::StoryFor(55, 1) == 6,
          "out-of-range EndClear fallback mismatch");

    Metadata::Result out;
    std::string error;
    Metadata::LocalTime english{2026, 10, 20, 2, 0, 40}; // Tuesday
    Check(Metadata::Format(english, true, 1, u"", &out, &error), error);
    Check(out.date == u"10/20/2026(Tue.)00:40",
          "English SaveLoad date format mismatch");
    Check(out.storyText == u"01" &&
              out.primary == u"10/20/2026(Tue.)00:4001" && out.sub.empty(),
          "English SaveConfig concatenation mismatch");
    std::u16string storedDate;
    std::u16string storedStory;
    Check(Metadata::SplitStoredPrimary(out.primary, true, &storedDate,
                                       &storedStory, &error) &&
              storedDate == out.date && storedStory == out.storyText,
          "English stored SaveConfig split mismatch");

    Metadata::LocalTime singleDigit{2026, 10, 4, 0, 20, 45}; // Sunday
    Check(Metadata::Format(singleDigit, true, 2, u"note", &out, &error), error);
    Check(out.date == u"10/ 4/2026(Sun.)20:45" &&
              out.primary == u"10/ 4/2026(Sun.)20:4502" && out.sub == u"note",
          "English width/comment witness mismatch");

    Metadata::LocalTime japanese{2012, 5, 28, 1, 19, 18}; // Monday
    Check(Metadata::Format(japanese, false, 5, u"", &out, &error), error);
    Check(out.date == u"2012\u5e74 5\u670828\u65e5(\u6708)19:18" &&
              out.storyText == u"\u7b2c5\u8a71" &&
              out.primary ==
                  u"2012\u5e74 5\u670828\u65e5(\u6708)19:18\u7b2c5\u8a71",
          "Japanese SaveConfig witness mismatch");
    Check(Metadata::SplitStoredPrimary(out.primary, false, &storedDate,
                                       &storedStory, &error) &&
              storedDate == out.date && storedStory == out.storyText,
          "Japanese stored SaveConfig split mismatch");
    Check(!Metadata::SplitStoredPrimary(u"x", true, &storedDate, &storedStory,
                                        &error) &&
              !Metadata::SplitStoredPrimary(u"ab", false, &storedDate,
                                             &storedStory, &error),
          "short stored SaveConfig was accepted");

    bool useEnglish = false;
    Check(Metadata::ParseUseEnglish("[UseEnglish]=\"1\";\r\n", &useEnglish,
                                    &error) && useEnglish,
          "UseEnglish=1 parse mismatch");
    Check(Metadata::ParseUseEnglish("x\n[UseEnglish]=\"0\"\n", &useEnglish,
                                    &error) && !useEnglish,
          "UseEnglish=0 parse mismatch");
    Check(!Metadata::ParseUseEnglish("[UseEnglish]=\"2\"", &useEnglish, &error),
          "invalid UseEnglish value was accepted");

    std::cout << "SCHOOL DAYS SAVE METADATA GATE PASS\n"
              << "route_story_mapping=PASS\n"
              << "endclear_fallback=PASS\n"
              << "english_exact_format=PASS\n"
              << "japanese_exact_format=PASS\n"
              << "primary_date_plus_story=PASS\n"
              << "stored_primary_split=PASS\n"
              << "sub_comment=PASS\n"
              << "useenglish_parse=PASS\n";
    return 0;
}
