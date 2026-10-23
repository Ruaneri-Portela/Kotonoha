#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysSaveStorageTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) Fail("could not read test output");
    const auto end = file.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), end);
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) Fail("usage: SchoolDaysSaveStorageTest <assets-root>");

    Kotonoha::SchoolDaysSaveProfile profile;
    std::string error;
    Check(Kotonoha::SchoolDaysSaveProfile::Load(argv[1], &profile, &error),
          error.c_str());
    Check(profile.useEnglish && profile.textInput &&
              profile.fontData == "System/System/FONTDATA.DAT" &&
              profile.saveFolder == "Save" &&
              profile.saveFileName == "Save/SaveFile00%d.DAT" &&
              profile.flagFileName == "Save/GlobalFlag.DAT" &&
              profile.saveConfig == "FILMEngine/SaveFile00%d",
          "authenticated FILMENGINE persistence profile drifted");

    std::string formatted;
    Check(Kotonoha::SchoolDaysSaveProfile::FormatIndexed(
              profile.saveFileName, 10, &formatted, &error) &&
              formatted == "Save/SaveFile0010.DAT",
          "slot10 filename formatting mismatch");
    Check(Kotonoha::SchoolDaysSaveProfile::FormatIndexed(
              profile.saveConfig, 99, &formatted, &error) &&
              formatted == "FILMEngine/SaveFile0099",
          "slot99 SaveConfig formatting mismatch");

    const auto unique = static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto base = std::filesystem::temp_directory_path() /
                      ("kotonoha-sdhq-storage-" + std::to_string(unique));
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) Fail("could not create temporary base directory");

    Kotonoha::SchoolDaysSaveStorage storage;
    Check(storage.Initialize(base, profile, &error), error.c_str());
    Check(std::filesystem::is_directory(base / "Save"),
          "init did not create configured SaveFolder");

    std::vector<uint8_t> loaded;
    bool present = true;
    Check(storage.LoadGlobalFlag(&loaded, &present, &error) && !present &&
              loaded.empty(),
          "missing GlobalFlag was not treated as normal OPEN_EXISTING miss");

    bool openable = true;
    Check(storage.ProbeSaveFile(10, &openable, &error) && !openable,
          "missing SaveFile was not treated as an empty slot");

    Kotonoha::SchoolDaysSaveService::Plan plan;
    plan.slot = 10;
    plan.saveFilePath = "Save/SaveFile0010.DAT";
    plan.globalFlagPath = "Save/GlobalFlag.DAT";
    plan.saveFileBytes = {1, 2, 3, 4, 5};
    plan.globalFlagBytes = {9, 8, 7};
    Check(storage.WriteSaveFile(plan, &error), error.c_str());
    Check(Read(base / "Save" / "SaveFile0010.DAT") == plan.saveFileBytes,
          "slot CREATE_ALWAYS write mismatch");
    Check(storage.ProbeSaveFile(10, &openable, &error) && openable,
          "written SaveFile did not become openable to SaveLoad");
    loaded.clear();
    Check(storage.ReadSaveFile(10, &loaded, &error) &&
              loaded == plan.saveFileBytes,
          "indexed SaveFile read did not return exact slot bytes");
    Check(!storage.ReadSaveFile(9, &loaded, &error) &&
              !storage.ReadSaveFile(-1, &loaded, &error) &&
              !storage.ReadSaveFile(100, &loaded, &error),
          "SaveFile read accepted missing/out-of-range slot");
    Check(storage.ProbeSaveFile(9, &openable, &error) && !openable,
          "neighboring missing SaveFile was reported openable");
    Check(!storage.ProbeSaveFile(-1, &openable, &error) &&
              !storage.ProbeSaveFile(100, &openable, &error),
          "out-of-range SaveLoad slot probe was accepted");

    plan.saveFileBytes = {6, 5};
    Check(storage.WriteSaveFile(plan, &error), error.c_str());
    Check(Read(base / "Save" / "SaveFile0010.DAT") == plan.saveFileBytes,
          "slot rewrite did not truncate existing file");

    Check(storage.WriteGlobalFlag(plan, &error), error.c_str());
    present = false;
    Check(storage.LoadGlobalFlag(&loaded, &present, &error) && present &&
              loaded == plan.globalFlagBytes,
          "GlobalFlag write/load mismatch");

    const std::vector<uint8_t> shutdownGlobalFlag = {0x53, 0x44, 0x48, 0x51};
    Check(storage.WriteGlobalFlagBytes(shutdownGlobalFlag, &error), error.c_str());
    present = false;
    loaded.clear();
    Check(storage.LoadGlobalFlag(&loaded, &present, &error) && present &&
              loaded == shutdownGlobalFlag,
          "standalone shutdown GlobalFlag flush mismatch");

    auto unsafe = plan;
    unsafe.saveFilePath = "../escape.dat";
    Check(!storage.WriteSaveFile(unsafe, &error),
          "storage accepted traversal outside executable directory");

    std::filesystem::remove_all(base, ec);
    std::cout << "SCHOOL DAYS SAVE STORAGE GATE PASS\n"
              << "filmengine_profile=PASS\n"
              << "slot_template=PASS\n"
              << "init_savefolder=PASS\n"
              << "missing_globalflag=PASS\n"
              << "slot_open_probe=PASS\n"
              << "slot_indexed_read=PASS\n"
              << "create_always_truncate=PASS\n"
              << "globalflag_io=PASS\n"
              << "shutdown_globalflag_flush=PASS\n"
              << "path_containment=PASS\n";
    return 0;
}
