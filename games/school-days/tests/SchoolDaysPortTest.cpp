#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfProfile.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void Require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void Put(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << data;
  Require(bool(out), "Cannot create fixture");
}
}

int main(int argc, char **argv) {
  try {
    Require(argc == 3, "Usage: SchoolDaysPortTest route fixture-directory");
    const fs::path root = argv[2];
    Kotonoha_KtrfDocument document{};
    Kotonoha_KtrfError error{};
    Require(Kotonoha_KtrfLoadFile(argv[1], &document, &error), error.message);
    uint32_t entry = 0;
    Require(Kotonoha::SchoolDaysKtrfProfile().validate_document(&document, &entry, &error), error.message);
    const auto *resources = Kotonoha_KtrfFindSection(&document, "RSRC");
    Require(resources && resources->item_count == 1855, "Original resource count changed");
    for (uint32_t i = 0; i < resources->item_count; ++i) {
      Kotonoha_KtrfResource resource{};
      Require(Kotonoha_KtrfGetResource(&document, i, &resource, &error), error.message);
      const char *key = nullptr;
      size_t size = 0;
      Require(Kotonoha_KtrfGetString(&document, resource.value_str, &key, &size, &error), error.message);
      Put(root / "Script/English" / (std::string(key, size) + ".ENG.ORS"),
          "[PrintText]=00:00:00\tFixture\tSynthetic data, not game assets.\t00:01:00;\n"
          "[SkipFRAME]=00:01:00;\n[Next]=00:02:00;\n");
    }
    Kotonoha_KtrfClean(&document);
    Put(root / "Movie/CaseTest.WMV", "synthetic");
    std::string info;
    Require(SchoolDays::Assets::checkDirectory(root, info), info);
    auto assets = std::make_shared<SchoolDays::Assets>(root);
    Require(assets->resolve("movie/casetest.wmv") == (root / "Movie/CaseTest.WMV").string(),
            "Case-insensitive media discovery failed");
    Require(assets->read(assets->resolve("Movie/CaseTest.WMV")).size() == 9, "Provider byte read failed");
    Require(assets->localPath(assets->resolve("Movie/CaseTest.WMV")).size() > 0,
            "Loose media must stream directly");
    bool rejected = false;
    try { assets->resolve("../escape"); } catch (const std::exception &) { rejected = true; }
    Require(rejected, "Traversal accepted");
    SchoolDays::Runtime runtime(assets, argv[1]);
    Require(runtime.debugNode() == "00/00-00-A00", "New-game route is not original entry");
    Require(runtime.debugNodes().size() == 1857, "Debug nodes missing");
    const auto dumped = root / "roundtrip.ktnroute";
    runtime.dumpKtrf(dumped.string());
    Require(assets->read(dumped.string()) == assets->read(argv[1]), "Original KTRF dump is not byte-exact");
    runtime.debugJumpToNode("00/00-00-A01");
    Require(runtime.debugNode() == "00/00-00-A01", "Scene-key debug jump failed");
    runtime.saveGame((root / "QuickSave").string());
    runtime.debugJumpToNode("00/00-00-A00");
    runtime.loadGame((root / "QuickSave").string());
    Require(runtime.debugNode() == "00/00-00-A01", "Native save/load did not restore scene");
    Put(root / "invalid.ktnroute", "invalid");
    rejected = false;
    try { runtime.loadKtrf((root / "invalid.ktnroute").string()); }
    catch (const std::exception &) { rejected = true; }
    Require(rejected && runtime.debugNode() == "00/00-00-A01",
            "Invalid KTRF replaced the live route");
    runtime.loadKtrf(dumped.string());
    Require(runtime.debugNode() == "00/00-00-A00", "KTRF load did not reset entry");
    Require(Kotonoha_KtrfLoadFile(dumped.string().c_str(), &document, &error), error.message);
    Kotonoha_KtrfNode second{};
    Require(Kotonoha_KtrfGetNode(&document, 1, &second, &error), error.message);
    const char *editedLabel = nullptr;
    size_t editedLabelSize = 0;
    Require(Kotonoha_KtrfGetString(&document, second.id_str, &editedLabel, &editedLabelSize, &error),
            error.message);
    const std::string expectedLabel(editedLabel, editedLabelSize);
    auto sections = std::vector<Kotonoha_KtrfSection>(document.sections,
                                                    document.sections + document.section_count);
    std::vector<uint8_t> nodePayload;
    for (auto &section : sections) {
      if (std::string(section.type) != "NODE") continue;
      nodePayload.assign(section.payload, section.payload + section.stored_size);
      // NODE uses a 24-byte pool header and label_str at record byte 8.
      for (unsigned byte = 0; byte < 4; ++byte)
        nodePayload[24 + 8 + byte] = uint8_t(second.id_str >> (byte * 8));
      section.payload = nodePayload.data();
    }
    Kotonoha_KtrfDocument edited{};
    Require(Kotonoha_KtrfBuild(sections.data(), uint32_t(sections.size()), &edited, &error),
            error.message);
    const auto editedPath = root / "edited.ktnroute";
    Put(editedPath, std::string(reinterpret_cast<const char *>(edited.bytes), edited.size));
    Kotonoha_KtrfClean(&edited);
    Kotonoha_KtrfClean(&document);
    runtime.loadKtrf(editedPath.string());
    Require(runtime.debugNodes().front().label == expectedLabel,
            "Runtime did not expose edited KTRF node label");
    runtime.debugJumpToNode("1");
    Require(runtime.debugNode() != "00/00-00-A00", "Edited KTRF numeric jump failed");
    runtime.dumpKtrf(dumped.string());
    Require(assets->read(dumped.string()) == assets->read(editedPath.string()),
            "Edited original-profile KTRF did not round-trip through Runtime");
    auto movie = Kotonoha::SchoolDaysMovieEventState::Create(
        Kotonoha::SchoolDaysMovieKind::PlayMovie, "Movie/test", 24, 48, 0);
    Require(movie.resource == "Movie/test.wmv" && movie.Supported(), "Movie resource rule changed");
    Require(!SchoolDays::Assets::checkDirectory(root / "missing", info), "Missing root accepted");
    std::cout << "SchoolDaysPortTest: discovery, binary compatibility, debug nodes, save/load passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysPortTest: " << error.what() << '\n';
    return 1;
  }
}
