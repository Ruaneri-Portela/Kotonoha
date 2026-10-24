#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
[[noreturn]] void Fail(const std::string& text) { std::cerr << text << '\n'; std::exit(2); }
void Check(bool value, const char* text) { if (!value) Fail(text); }
}

int main(int argc, char** argv) {
    if (argc != 2) Fail("usage: SchoolDaysTitleProfileTest <assets-root>");
    Kotonoha::SchoolDaysEndBackgroundResolver::Profile profile;
    std::string error;
    Check(Kotonoha::SchoolDaysTitleProfile::Load(argv[1], &profile, &error), error.c_str());
    Check(profile.endBgView, "EndBGView drifted from authenticated profile");
    Check(profile.baseAsset == "System/Title/TitleBase.png",
          "BaseFile drifted from authenticated profile");
    Check(profile.titleBgmStem == "BGM/SD_BGM/sdbgm28",
          "TitleBGM drifted from authenticated profile");
    Check(profile.systemBgmStem == "BGM/SD_BGM/sdbgm29",
          "SystemBGM drifted from authenticated profile");
    Check(profile.endingMax == 22 && profile.endingAssets.size() == 22,
          "EndingMax/list size drifted");
    Check(profile.endingAssets.front() == "System/EndTitle/END-05-5H-E00.png" &&
              profile.endingAssets[15] == "System/EndTitle/END-05-SB-E01.wmv" &&
              profile.endingAssets[20] == "System/EndTitle/END-05-SE-S00.wmv" &&
              profile.endingAssets.back() == "System/EndTitle/END-SETSUNA.png",
          "EndingList ordering/path drifted");
    Check(profile.allClearAsset == "System/EndTitle/END-ALL-complete.png",
          "AllClear path drifted");
    std::cout << "SCHOOL DAYS TITLE PROFILE GATE PASS\n"
              << "startscript_base_endbg=PASS\n"
              << "titlebgm_loop_stem=PASS\n"
              << "systembgm_loop_stem=PASS\n"
              << "endingmax22=PASS\n"
              << "ending_order=PASS\n"
              << "wmv_entries=PASS\n"
              << "allclear=PASS\n";
    return 0;
}
