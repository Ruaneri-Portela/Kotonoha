#include <Kotonoha/routing/SchoolDaysReplayScriptOracle.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>

namespace {

using Oracle = Kotonoha::SchoolDaysReplayScriptOracle;
using Sequence = Oracle::SequenceDescriptor;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SCHOOL DAYS REPLAY SCRIPT ORACLE GATE FAIL: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

void CheckSequence(const Sequence* actual,
                   std::initializer_list<const char*> expected,
                   const std::string& label) {
    Check(actual != nullptr, label + " returned null");
    Check(actual->count == expected.size(), label + " count drifted");
    std::size_t i = 0;
    for (const char* value : expected) {
        Check(std::string(actual->sceneKeys[i]) == value,
              label + " SceneKey drifted at " + std::to_string(i));
        ++i;
    }
}

template <std::size_t N>
void CheckH11Matrix(int choice, const int (&expected)[N][3]) {
    const int selectResults[] = {-2, 0, 1};
    for (std::size_t cursor = 0; cursor < N; ++cursor) {
        for (int column = 0; column < 3; ++column) {
            int next = -1;
            bool completed = false;
            std::string error;
            Check(Oracle::ResolveSpecialNextCursor(
                      11, choice, static_cast<int>(cursor), selectResults[column],
                      &next, &completed, &error),
                  "HScene11 authenticated matrix lookup failed");
            const int nativeNext = expected[cursor][column];
            const bool nativeTerminal = nativeNext == static_cast<int>(N);
            Check(completed == nativeTerminal &&
                      next == (nativeTerminal ? -1 : nativeNext),
                  "HScene11 authenticated matrix value drifted");
        }
    }
}

void CheckOrdinaryCorpus() {
    CheckSequence(Oracle::OrdinarySequence(0), {"02/02-2S-W03"}, "index0");
    CheckSequence(Oracle::OrdinarySequence(1),
        {"02/02-2K-J03","02/02-2K-J04","02/02-2K-J05","02/02-2K-J06",
         "02/02-2K-J07","02/02-2K-J08","02/02-2K-J09","02/02-2K-J10"}, "index1");
    CheckSequence(Oracle::OrdinarySequence(2), {"02/02-2K-L01"}, "index2");
    CheckSequence(Oracle::OrdinarySequence(3), {"02/02-2K-O01","02/02-2K-O04"}, "index3");
    CheckSequence(Oracle::OrdinarySequence(4), {"02/02-2K-Q03","02/02-2K-Q04","02/02-2K-Q05"}, "index4");
    CheckSequence(Oracle::OrdinarySequence(5), {"03/03-SA-B07"}, "index5");
    CheckSequence(Oracle::OrdinarySequence(6),
        {"03/03-A0-A00","03/03-A0-A04","03/03-A0-A05","03/03-A0-A06",
         "03/03-A0-A07","03/03-A0-A08","03/03-A0-A09","03/03-A0-A10"}, "index6");
    CheckSequence(Oracle::OrdinarySequence(7), {"03/03-A1-E08","03/03-A1-Z00","03/03-A1-Z01"}, "index7");
    CheckSequence(Oracle::OrdinarySequence(8), {"03/03-AK-E00"}, "index8");
    CheckSequence(Oracle::OrdinarySequence(9), {"03/03-SB-A00","03/03-SB-A01","03/03-SB-A03"}, "index9");
    CheckSequence(Oracle::OrdinarySequence(10),
        {"03/03-B2-C00","03/03-B2-N02","03/03-B2-N03","03/03-B2-N04",
         "03/03-B2-N06","03/03-B2-N07","03/03-B2-O03"}, "index10");
    CheckSequence(Oracle::OrdinarySequence(12),
        {"04/04-KA-D01","04/04-KA-D03","04/04-KA-D04","04/04-KA-D05",
         "04/04-KA-D06","04/04-KA-D07","04/04-KA-D08"}, "index12");
    CheckSequence(Oracle::OrdinarySequence(13), {"04/04-KA-B03"}, "index13");
    CheckSequence(Oracle::OrdinarySequence(14), {"04/04-KA-L00","04/04-KA-L01"}, "index14");
    CheckSequence(Oracle::OrdinarySequence(15), {"04/04-KA-B04"}, "index15");
    CheckSequence(Oracle::OrdinarySequence(16),
        {"04/04-SB-G05","04/04-SB-G06","04/04-SB-G07","04/04-SB-G08",
         "04/04-SB-I00","04/04-SB-I01","04/04-SB-I02","04/04-SB-I03","04/04-SB-I04"}, "index16");
    CheckSequence(Oracle::OrdinarySequence(17), {"04/04-KB-C03"}, "index17");
    CheckSequence(Oracle::OrdinarySequence(18), {"04/04-KB-E01"}, "index18");
    CheckSequence(Oracle::OrdinarySequence(19), {"04/04-KC-C03"}, "index19");
    CheckSequence(Oracle::OrdinarySequence(20), {"04/04-KC-E01"}, "index20");
    CheckSequence(Oracle::OrdinarySequence(21), {"04/04-SC-B00"}, "index21");
    CheckSequence(Oracle::OrdinarySequence(23), {"04/04-SC-D02"}, "index23");
    CheckSequence(Oracle::OrdinarySequence(24), {"04/04-SC-F00"}, "index24");
    CheckSequence(Oracle::OrdinarySequence(25),
        {"04/04-C0-I00","04/04-C0-I01","04/04-C0-I02","04/04-C0-I03","04/04-C0-I04"}, "index25");
    CheckSequence(Oracle::OrdinarySequence(26), {"04/04-KE-C01"}, "index26");
    CheckSequence(Oracle::OrdinarySequence(27), {"04/04-KE-B07"}, "index27");
    CheckSequence(Oracle::OrdinarySequence(28), {"05/05-KA-A05","05/05-KA-A06","05/05-KA-A07"}, "index28");
    CheckSequence(Oracle::OrdinarySequence(29), {"05/05-5H-C03","05/05-5H-C06","05/05-5H-C07"}, "index29");
    CheckSequence(Oracle::OrdinarySequence(31), {"05/05-SD-D05"}, "index31");
    CheckSequence(Oracle::OrdinarySequence(32), {"05/05-SD-A02","05/05-SD-B03","05/05-SD-B05","05/05-SD-A04"}, "index32");
    CheckSequence(Oracle::OrdinarySequence(33), {"05/05-SA-I00"}, "index33");
    CheckSequence(Oracle::OrdinarySequence(34), {"05/05-SB-I01","05/05-SB-I02"}, "index34");
    CheckSequence(Oracle::OrdinarySequence(35), {"05/05-KC-A00","05/05-KC-A01"}, "index35");
    CheckSequence(Oracle::OrdinarySequence(36), {"05/05-KC-D00","05/05-KC-D02","05/05-KC-D03","05/05-KC-D04"}, "index36");
    CheckSequence(Oracle::OrdinarySequence(37), {"05/05-KC-E00"}, "index37");
    CheckSequence(Oracle::OrdinarySequence(38), {"05/05-KC-A02"}, "index38");
    CheckSequence(Oracle::OrdinarySequence(39), {"05/05-5O-D10"}, "index39");
    CheckSequence(Oracle::OrdinarySequence(40), {"05/05-SE-N00"}, "index40");
}

void CheckSpecialCorpus() {
    const auto* s11 = Oracle::HScene(11);
    Check(s11 != nullptr && s11->kind == Oracle::Kind::SpecialChoiceTable &&
              s11->sceneCheckKey == 0 && s11->choiceCount == 4 &&
              s11->ordinary.count == 0,
          "index11 special descriptor drifted");
    CheckSequence(Oracle::SpecialChoiceSequence(11,0),
        {"03/03-KB-N00","03/03-KB-N02","03/03-KB-N03","03/03-KB-N04",
         "03/03-KB-N05","03/03-KB-N06","03/03-KB-N07","03/03-KB-N08",
         "03/03-KB-N10","03/03-KB-O02","03/03-KB-O03","03/03-KB-O04",
         "03/03-KB-O05","03/03-KB-O07","03/03-KB-O08"}, "index11 choice0");
    CheckSequence(Oracle::SpecialChoiceSequence(11,1),
        {"03/03-KB-N00","03/03-KB-N02","03/03-KB-N03","03/03-KB-N04",
         "03/03-KB-N05","03/03-KB-N06","03/03-KB-N07","03/03-KB-N09",
         "03/03-KB-O01","03/03-KB-O03","03/03-KB-O05","03/03-KB-O07",
         "03/03-KB-O08","03/03-KB-O09"}, "index11 choice1");
    CheckSequence(Oracle::SpecialChoiceSequence(11,2),
        {"03/03-KB-N00","03/03-KB-N02","03/03-KB-N03","03/03-KB-N04",
         "03/03-KB-N05","03/03-KB-N06","03/03-KB-N07","03/03-KB-N08",
         "03/03-KB-N10","03/03-KB-O02","03/03-KB-O03","03/03-KB-O04",
         "03/03-KB-O05","03/03-KB-O07","03/03-KB-O08","03/03-KB-O09"}, "index11 choice2");
    CheckSequence(Oracle::SpecialChoiceSequence(11,3),
        {"03/03-KB-N00","03/03-KB-N02","03/03-KB-N03","03/03-KB-N04",
         "03/03-KB-N05","03/03-KB-N06","03/03-KB-N07","03/03-KB-N09",
         "03/03-KB-O01","03/03-KB-O03","03/03-KB-O05","03/03-KB-O07",
         "03/03-KB-O08"}, "index11 choice3");

    const auto* s22 = Oracle::HScene(22);
    Check(s22 != nullptr && s22->sceneCheckKey == 2 && s22->choiceCount == 2,
          "index22 special descriptor drifted");
    CheckSequence(Oracle::SpecialChoiceSequence(22,0),
        {"04/04-C1-A00","04/04-C1-A01","04/04-C1-A03"}, "index22 choice0");
    CheckSequence(Oracle::SpecialChoiceSequence(22,1),
        {"04/04-C1-A00","04/04-C1-A02","04/04-C1-A03"}, "index22 choice1");

    const auto* s30 = Oracle::HScene(30);
    Check(s30 != nullptr && s30->sceneCheckKey == 3 && s30->choiceCount == 2,
          "index30 special descriptor drifted");
    CheckSequence(Oracle::SpecialChoiceSequence(30,0),
        {"05/05-5H-D00","05/05-5H-D01","05/05-5H-D03"}, "index30 choice0");
    CheckSequence(Oracle::SpecialChoiceSequence(30,1),
        {"05/05-5H-D00","05/05-5H-D02","05/05-5H-D03"}, "index30 choice1");

    Check(Oracle::CursorResolutionFor(11) ==
              Oracle::TransitionCursorResolution::NativeSelectorRequired &&
          Oracle::CursorResolutionFor(22) ==
              Oracle::TransitionCursorResolution::NativeSelectorRequired &&
          Oracle::CursorResolutionFor(30) ==
              Oracle::TransitionCursorResolution::NativeSelectorRequired,
          "special cursor resolution was flattened");
    Check(Oracle::CursorResolutionFor(10) ==
              Oracle::TransitionCursorResolution::StaticTableOrder,
          "ordinary cursor resolution drifted");

    constexpr int h11Choice0[][3] = {
        {3,1,2},{6,4,5},{10,0,0},{10,0,0},{7,0,0},{8,0,0},{9,0,0},
        {10,0,0},{11,0,0},{11,0,0},{13,12,0},{10,0,0},{14,0,0},{15,0,0},
        {13,0,0}};
    constexpr int h11Choice1[][3] = {
        {3,1,2},{6,4,5},{9,0,0},{9,0,0},{7,0,0},{8,0,0},{9,0,0},
        {9,0,0},{9,0,0},{11,10,0},{12,0,0},{14,0,0},{13,0,0},{14,0,0}};
    constexpr int h11Choice2[][3] = {
        {3,1,2},{6,4,5},{10,0,0},{10,0,0},{7,0,0},{8,0,0},{9,0,0},
        {10,0,0},{11,0,0},{11,0,0},{13,12,0},{10,0,0},{14,0,0},{16,0,0},
        {15,0,0},{16,0,0}};
    constexpr int h11Choice3[][3] = {
        {3,1,2},{6,4,5},{9,0,0},{9,0,0},{7,0,0},{8,0,0},{9,0,0},
        {9,0,0},{9,0,0},{11,10,0},{12,0,0},{13,0,0},{11,0,0}};
    CheckH11Matrix(0, h11Choice0);
    CheckH11Matrix(1, h11Choice1);
    CheckH11Matrix(2, h11Choice2);
    CheckH11Matrix(3, h11Choice3);

    int next = -1;
    bool completed = false;
    std::string cursorError;
    Check(Oracle::ResolveSpecialNextCursor(11,0,0,-2,&next,&completed,&cursorError) &&
              next == 3 && !completed,
          "HScene11 no-select cursor0 transition drifted");
    Check(Oracle::ResolveSpecialNextCursor(11,0,0,-1,&next,&completed,&cursorError) &&
              next == 3 && !completed,
          "HScene11 timed/no-choice cursor0 transition drifted");
    Check(Oracle::ResolveSpecialNextCursor(11,0,0,0,&next,&completed,&cursorError) &&
              next == 1 && !completed,
          "HScene11 select0 cursor0 transition drifted");
    Check(Oracle::ResolveSpecialNextCursor(11,0,0,1,&next,&completed,&cursorError) &&
              next == 2 && !completed,
          "HScene11 select1 cursor0 transition drifted");
    Check(Oracle::ResolveSpecialNextCursor(11,0,2,0,&next,&completed,&cursorError) &&
              next == 0 && !completed,
          "HScene11 cursor0 restart was treated as terminal");
    Check(Oracle::ResolveSpecialNextCursor(11,0,13,-2,&next,&completed,&cursorError) &&
              next == -1 && completed,
          "HScene11 bank0 terminal sentinel drifted");
    Check(Oracle::ResolveSpecialNextCursor(11,2,13,-2,&next,&completed,&cursorError) &&
              next == -1 && completed,
          "HScene11 bank2 terminal sentinel drifted");
    Check(Oracle::ResolveSpecialNextCursor(22,0,0,1,&next,&completed,&cursorError) &&
              next == 1 && !completed &&
          Oracle::ResolveSpecialNextCursor(22,0,2,-2,&next,&completed,&cursorError) &&
              next == -1 && completed,
          "HScene22 sequential cursor drifted");
    Check(Oracle::ResolveSpecialNextCursor(30,1,2,0,&next,&completed,&cursorError) &&
              next == -1 && completed,
          "HScene30 sequential terminal drifted");
    Check(!Oracle::ResolveSpecialNextCursor(11,0,0,2,&next,&completed,&cursorError),
          "unsupported replay select result did not fail closed");
    Check(!Oracle::ResolveSpecialNextCursor(22,0,0,2,&next,&completed,&cursorError) &&
              !Oracle::ResolveSpecialNextCursor(30,0,0,-3,&next,&completed,&cursorError),
          "sequential special selector accepted unsupported replay select result");
}

void CheckBoundsAndShape() {
    Check(Oracle::HSceneCount() == 41, "HScene count drifted");
    Check(Oracle::HScene(-1) == nullptr && Oracle::HScene(41) == nullptr,
          "HScene bounds accepted invalid index");
    Check(Oracle::OrdinarySequence(11) == nullptr &&
              Oracle::OrdinarySequence(22) == nullptr &&
              Oracle::OrdinarySequence(30) == nullptr,
          "special table was exposed as ordinary sequence");
    Check(Oracle::SpecialChoiceSequence(10,0) == nullptr &&
              Oracle::SpecialChoiceSequence(11,4) == nullptr,
          "special-choice bounds failed closed");
    Check(Oracle::CursorResolutionFor(41) ==
              Oracle::TransitionCursorResolution::Invalid,
          "invalid cursor resolution did not fail closed");
}

void AddKeys(const Sequence& sequence, std::set<std::string>* keys,
             std::size_t* records) {
    for (std::size_t i = 0; i < sequence.count; ++i) {
        keys->insert(sequence.sceneKeys[i]);
        ++*records;
    }
}

void CheckOptionalCorpusValidation() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "kotonoha-replay-script-oracle-gate";
    std::error_code ec;
    fs::remove_all(root, ec);

    std::set<std::string> keys;
    std::size_t records = 0;
    for (int index = 0; index < Oracle::HSceneCount(); ++index) {
        const auto* descriptor = Oracle::HScene(index);
        Check(descriptor != nullptr, "unvalidated descriptor query failed");
        if (descriptor->kind == Oracle::Kind::Ordinary) {
            AddKeys(descriptor->ordinary, &keys, &records);
        } else {
            for (std::size_t choice = 0; choice < descriptor->choiceCount; ++choice)
                AddKeys(descriptor->choices[choice].sequence, &keys, &records);
        }
    }
    Check(records == 166, "authenticated replay descriptor record count drifted");

    bool mixedCasePhysicalWitness = false;
    for (const std::string& key : keys) {
        std::string relative = Oracle::OrsRelativePath(key.c_str());
        if (key == "02/02-2S-W03") {
            for (char& ch : relative) {
                if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch - 'A' + 'a');
            }
            mixedCasePhysicalWitness = true;
        }
        const fs::path path = root / fs::path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary).put('\0');
    }
    Check(mixedCasePhysicalWitness, "mixed-case ORS witness was not created");

    std::string error;
    const std::string rootString = root.string();
    Check(Oracle::ValidateCorpus(rootString.c_str(), &error), error);
    for (int index = 0; index < Oracle::HSceneCount(); ++index)
        Check(Oracle::HScene(index, rootString.c_str(), &error) != nullptr, error);

    const fs::path specialMissing = root / Oracle::OrsRelativePath("03/03-KB-N08");
    fs::remove(specialMissing, ec);
    Check(Oracle::SpecialChoiceSequence(11,0,rootString.c_str(),&error) == nullptr &&
              error.find("03/03-KB-N08.ENG.ORS") != std::string::npos,
          "special optional ORS validation did not report missing SceneKey");
    Check(Oracle::SpecialChoiceSequence(11,1,rootString.c_str(),&error) != nullptr,
          "special ORS validation crossed into a different choice table");
    fs::create_directories(specialMissing.parent_path());
    std::ofstream(specialMissing, std::ios::binary).put('\0');

    const fs::path ordinaryMissing = root / Oracle::OrsRelativePath("05/05-SE-N00");
    fs::remove(ordinaryMissing, ec);
    Check(Oracle::HScene(40,rootString.c_str(),&error) == nullptr &&
              error.find("05/05-SE-N00.ENG.ORS") != std::string::npos,
          "ordinary optional ORS validation did not report missing SceneKey");
    Check(!Oracle::ValidateCorpus(rootString.c_str(), &error),
          "complete corpus validation accepted a missing ORS");

    fs::remove_all(root, ec);
}

} // namespace

int main() {
    CheckOrdinaryCorpus();
    CheckSpecialCorpus();
    CheckBoundsAndShape();
    CheckOptionalCorpusValidation();

    std::cout << "SCHOOL DAYS REPLAY SCRIPT ORACLE GATE PASS\n"
              << "ordinary_indices_38=PASS\n"
              << "special_choice_tables_11_22_30=PASS\n"
              << "special_cursor_native_required=PASS\n"
              << "descriptor_records_166=PASS\n"
              << "special_native_cursor_matrices=PASS\n"
              << "optional_ors_corpus_validation=PASS\n"
              << "ors_casefold_filesystem_lookup=PASS\n";
    return 0;
}
