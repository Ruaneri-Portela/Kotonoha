#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int Run() {
    using namespace Kotonoha;
    const fs::path root = fs::temp_directory_path() / "kotonoha_sd_asset_resolver";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "System" / "MixedCase");
    const fs::path file = root / "System" / "MixedCase" / "ASSET.DAT";
    { std::ofstream out(file, std::ios::binary); out << "fixture"; }

    const auto dedicatedRoot = SelectSchoolDaysAssetRoot("school-days", "generic");
    const auto genericRoot = SelectSchoolDaysAssetRoot(nullptr, "generic");
    const auto noRoot = SelectSchoolDaysAssetRoot(nullptr, nullptr);
    Check(dedicatedRoot.available && dedicatedRoot.dedicated &&
              std::string(dedicatedRoot.root) == "school-days" &&
              genericRoot.available && !genericRoot.dedicated &&
              std::string(genericRoot.root) == "generic" &&
              !noRoot.available && std::string(noRoot.root).empty(),
          "School Days dedicated asset-root precedence drifted");

    const auto exact = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "System/MixedCase/ASSET.DAT");
    Check(exact.status == SchoolDaysAssetResolutionStatus::Present &&
              exact.present && !exact.collision && fs::path(exact.physical) == file,
          "exact lookup failed");

    const auto folded = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "system/mixedcase/asset.dat");
    Check(folded.status == SchoolDaysAssetResolutionStatus::Present &&
              fs::path(folded.physical) == file,
          "case-fold lookup failed");

    const auto emptyRoot = ResolveSchoolDaysAssetPath(
        "", "System/MixedCase/ASSET.DAT");
    const auto finalDirectory = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "System/MixedCase");
    const auto intermediateFile = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "System/MixedCase/ASSET.DAT/child.dat");
    Check(emptyRoot.status == SchoolDaysAssetResolutionStatus::Invalid &&
              finalDirectory.status == SchoolDaysAssetResolutionStatus::Invalid &&
              intermediateFile.status == SchoolDaysAssetResolutionStatus::Invalid &&
              emptyRoot.physical.empty() && finalDirectory.physical.empty() &&
              intermediateFile.physical.empty(),
          "invalid root/file-type lookup did not fail closed");

    const auto missing = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "system/mixedcase/Missing/NESTED.OGG");
    Check(missing.status == SchoolDaysAssetResolutionStatus::Missing &&
              !missing.present && !missing.collision &&
              missing.physical.find("Missing/NESTED.OGG") != std::string::npos,
          "missing lookup lost full candidate path");
    Check(ResolveSchoolDaysBgmAsset(
              root.string().c_str(), "system/mixedcase/Missing/NESTED.OGG") ==
              missing.physical,
          "optional media missing path no longer defers decoder failure");

    const auto traversal = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "System/../secret.dat");
    const auto missingTraversal = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "Missing/../outside.ogg");
    const auto missingDrive = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "Missing/C:/outside.ogg");
    const auto absolute = ResolveSchoolDaysAssetPath(
        root.string().c_str(), "C:/outside.dat");
    Check(traversal.status == SchoolDaysAssetResolutionStatus::Invalid &&
              missingTraversal.status == SchoolDaysAssetResolutionStatus::Invalid &&
              missingDrive.status == SchoolDaysAssetResolutionStatus::Invalid &&
              absolute.status == SchoolDaysAssetResolutionStatus::Invalid &&
              traversal.physical.empty() && missingTraversal.physical.empty() &&
              missingDrive.physical.empty() && absolute.physical.empty(),
          "invalid path escaped asset root before or after a missing prefix");

    std::string selected;
    Check(SchoolDaysSelectUniqueFoldedMatch({}, &selected) ==
              SchoolDaysAssetResolutionStatus::Missing && selected.empty(),
          "pure missing selector failed");
    Check(SchoolDaysSelectUniqueFoldedMatch({"Asset.dat"}, &selected) ==
              SchoolDaysAssetResolutionStatus::Present && selected == "Asset.dat",
          "pure unique selector failed");
    Check(SchoolDaysSelectUniqueFoldedMatch(
              {"Asset.dat", "ASSET.DAT"}, &selected) ==
              SchoolDaysAssetResolutionStatus::Collision && selected.empty(),
          "case-fold collision did not fail closed");

    fs::remove_all(root, ec);
    std::cout << "SCHOOL DAYS ASSET RESOLVER GATE PASS\n"
              << "exact_present=PASS\n"
              << "mixed_case_unique=PASS\n"
              << "root_and_file_type_invalid=PASS\n"
              << "missing_full_candidate=PASS\n"
              << "optional_media_missing_deferred=PASS\n"
              << "absolute_traversal_invalid=PASS\n"
              << "post_missing_traversal_invalid=PASS\n"
              << "casefold_collision_fail_closed=PASS\n"
              << "dedicated_root_precedence=PASS\n";
    return 0;
}
} // namespace

int main() {
    try { return Run(); }
    catch (const std::exception& e) {
        std::cerr << "SchoolDaysAssetResolverTest: " << e.what() << '\n';
        return 2;
    }
}
