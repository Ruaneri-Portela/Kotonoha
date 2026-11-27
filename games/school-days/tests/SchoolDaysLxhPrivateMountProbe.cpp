// Explicit read-only PRIVATE audit of a licensed School Days HQ installation
// with 29 unmodified retail GPKs and mandatory Packs/LxH.GPK.
// Never automatically run by CTest and never launches SchoolDays.exe.
#include "SchoolDaysAssets.hpp"

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr auto kEndings = Kotonoha::SchoolDaysLxhContent::Endings;
constexpr std::array<std::size_t, 7> kAddedText = {30, 53, 13, 23, 64, 68, 22};

// Expected 24-tick scene times based on the audited retail English EndRoll
// plus each Spanish LxH epilogue's time deltas. The worker's safe graft must
// respect the source's own EndRoll ending, not the Spanish Next (which is five
// seconds early in 05-B0-F00).
struct ExpectedTiming {
  const char *skip;
  const char *movie;
  const char *next;
};
constexpr std::array<ExpectedTiming, 7> kExpected{{
    {"04:04:11", "04:06:11", "06:13:11"},
    {"04:10:03", "04:13:03", "09:07:03"},
    {"03:57:19", "03:58:19", "05:37:19"},
    {"02:51:06", "02:51:15", "04:56:15"},
    {"13:52:12", "13:53:13", "17:43:13"},
    {"04:37:20", "04:37:20", "10:00:20"},
    {"03:34:23", "03:35:23", "05:34:23"},
}};

void Require(bool value, const std::string &message);

Kotonoha_SceneTick Time(const char *stamp) {
  unsigned minute = 0, second = 0, frame = 0;
  Require(std::sscanf(stamp, "%u:%u:%u", &minute, &second, &frame) == 3,
          "Invalid private audit time");
  return (minute * 60u + second) * 24u + frame;
}

void Require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}

std::string EndingLogical(std::string_view key) {
  return "Script/English/05/" + std::string(key) + ".ENG.ORS";
}

std::vector<unsigned char> Read(SchoolDays::Assets &assets,
                                const std::string &logical,
                                bool image = false) {
  return assets.read(assets.resolve(logical, image));
}

fs::path PackFolder(const fs::path &root) {
  return fs::is_regular_file(root / "Script.GPK") ? root : root / "Packs";
}

void VerifyMedia(SchoolDays::Assets &assets, const std::string &logical,
                 std::size_t kind) {
  const std::string reference = assets.resolve(logical, kind == 0u);
  Require(Kotonoha::IsSchoolDaysGpkUri(reference),
          "Integrated resource did not resolve to mandatory GPK: " + logical);
  const auto bytes = assets.read(reference);
  Require(bytes.size() > (kind == 1 ? 16u : kind == 0 ? 8u : 4u),
          "Integrated epilogue media is truncated: " + logical);
  switch (kind) {
  case 0:
    Require(bytes[0] == 0x89 && bytes[1] == 'P' &&
                bytes[2] == 'N' && bytes[3] == 'G',
            "Integrated PNG magic invalid");
    break;
  case 1:
    Require(bytes[0] == 0x30 && bytes[1] == 0x26 &&
                bytes[2] == 0xb2 && bytes[3] == 0x75,
            "Integrated WMV/ASF magic invalid");
    break;
  case 2:
    Require(bytes[0] == 'O' && bytes[1] == 'g' &&
                bytes[2] == 'g' && bytes[3] == 'S',
            "Integrated OGG magic invalid");
    break;
  default: throw std::runtime_error("Invalid media type");
  }
  auto stream = assets.openStream(reference);
  Require(stream && stream->size() == bytes.size(),
          "Integrated media is not seekable raw GPK: " + logical);
  std::array<Uint8, 4> probe{};
  Require(stream->read(probe.data(), probe.size()) == probe.size() &&
              std::equal(probe.begin(), probe.end(), bytes.begin()),
          "Integrated media raw stream begins with wrong bytes");
  Require(stream->seek(bytes.size() - probe.size()) &&
              stream->read(probe.data(), probe.size()) == probe.size() &&
              std::equal(probe.begin(), probe.end(),
                         bytes.end() - probe.size()),
          "Integrated media end seek returned wrong data");
  Require(!stream->seek(bytes.size() + 1u),
          "Integrated media stream allowed out-of-bounds seek");
}

void CheckParsedEpilogue(const std::vector<unsigned char> &base,
                         const std::vector<unsigned char> &withLxh,
                         std::size_t ending) {
  auto parse = [](const std::vector<unsigned char> &bytes) {
    const auto script = Kotonoha_OrsParserBytes(bytes.data(), bytes.size());
    struct Result {
      std::array<std::size_t, 7> counts{};
      Kotonoha_SceneTick endRollEnd = 0;
      Kotonoha_SceneTick skip = 0;
      Kotonoha_SceneTick movieStart = 0;
      Kotonoha_SceneTick next = 0;
    } counts;
    for (const auto *event = script.data; event != nullptr;
         event = event->next) {
      switch (event->command) {
      case PRINT_TEXT:
        ++counts.counts[0];
        break;
      case END_ROLL:
        ++counts.counts[1];
        counts.endRollEnd = event->endTick;
        break;
      case Next:
        ++counts.counts[2];
        counts.next = event->startTick;
        break;
      case PLAY_MOVIE:
        ++counts.counts[3];
        counts.movieStart = event->startTick;
        break;
      case PLAY_SE:
        ++counts.counts[4];
        break;
      case CREATE_BG:
        ++counts.counts[5];
        break;
      case SkipFRAME:
        ++counts.counts[6];
        counts.skip = event->startTick;
        break;
      default:
        break;
      }
    }
    auto mutableScript = script;
    Kotonoha_OrsClean(&mutableScript);
    return counts;
  };
  const auto original = parse(base);
  const auto added = parse(withLxh);
  Require(added.counts[0] == original.counts[0] + kAddedText[ending],
          "LxH has an incorrect number of additional PrintText events");
  Require(added.counts[1] == original.counts[1] && added.counts[1] == 1u,
          "LxH inserted another ending code/EndRoll event");
  Require(added.counts[2] == original.counts[2] && added.counts[2] == 1u,
          "LxH introduced a second terminal Next event");
  Require(added.counts[3] == original.counts[3] + 1u,
          "LxH did not append exactly one PlayMovie event");
  Require(added.counts[4] == original.counts[4] + 1u,
          "LxH did not append exactly one PlaySe event");
  Require(added.counts[5] == original.counts[5] + 1u,
          "LxH did not append exactly one CreateBG event");
  Require(original.counts[6] == 1u && added.counts[6] == 1u,
          "LxH lost or duplicated authored SkipFRAME");
  Require(added.endRollEnd == original.endRollEnd,
          "LxH modified retail English EndRoll duration");
  Require(added.skip == Time(kExpected[ending].skip),
          "LxH SkipFRAME does not match audited scene policy");
  Require(added.movieStart == Time(kExpected[ending].movie),
          "LxH movie start is mistimed relative to English credits");
  Require(added.next == Time(kExpected[ending].next),
          "LxH Next boundary does not match audited English extension");
}
} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2 || argc == 3,
            "Usage: SchoolDaysLxhPrivateMountProbe RETAIL_DIR "
            "[DISPLAY_OPTIONS_OVERLAY]");
    const fs::path retail = argv[1];
    const fs::path overlay = argc == 3 ? fs::path(argv[2]) : fs::path();
    Require(fs::is_directory(retail), "Missing private retail game directory");
    if (!overlay.empty())
      Require(fs::is_directory(overlay), "Display-options overlay not found");

    const fs::path packs = PackFolder(retail);
    Kotonoha::SchoolDaysGpkArchive originalScripts, integrated;
    std::string error;
    Require(originalScripts.Open(packs / "Script.GPK", &error),
            "Cannot independently audit original Script.GPK: " + error);
    Require(integrated.Open(packs / "LxH.GPK", &error),
            "Missing/corrupt mandatory LxH.GPK: " + error);
    const auto expected = Kotonoha::SchoolDaysLxhContent::PackEntries();
    Require(integrated.EntryCount() == expected.size() &&
                expected.size() == 28u,
            "LxH.GPK must contain exactly the 28 authored resources");
    for (const auto &logical : expected) {
      std::size_t index = 0;
      Require(integrated.Lookup(logical, &index) ==
                  Kotonoha::SchoolDaysGpkLookupStatus::Present &&
                  index < integrated.Index().Entries().size() &&
                  integrated.Index().Entries()[index].IsRaw(),
              "LxH.GPK missing raw entry: " + logical);
    }

    SchoolDays::Assets assets(retail, overlay.string());
    Require(Kotonoha::SchoolDaysRetailGpksMounted(),
            "Integrated 30-pack mount did not become active");
    for (std::size_t index = 0; index < kEndings.size(); ++index) {
      const std::string logical = EndingLogical(kEndings[index]);
      const std::string baseRelative =
          "English/05/" + std::string(kEndings[index]) + ".ENG.ORS";
      std::vector<unsigned char> base;
      Require(originalScripts.Read(baseRelative, &base, &error),
              "Cannot inspect original v1.02 ORS independently: " + error);
      const std::string reference = assets.resolve(logical);
      Require(Kotonoha::IsSchoolDaysGpkUri(reference),
              "Integrated ORS did not win over loose overlay / original Script.GPK");
      const auto withLxh = assets.read(reference);
      Require(base != withLxh, "Mandatory LxH ORS did not override original");
      CheckParsedEpilogue(base, withLxh, index);

      const std::string stem = std::string(kEndings[index]) + "-LXH";
      VerifyMedia(assets, "LxH/" + stem + ".png", 0);
      VerifyMedia(assets, "LxH/Media/" + stem + ".wmv", 1);
      VerifyMedia(assets, "LxH/Media/" + stem + ".ogg", 2);
    }
    if (!overlay.empty()) {
      const auto display = Read(assets, "System/Option/Option_Display", true);
      Require(!display.empty(),
              "Separate remaster Display Options overlay was lost");
    }
    std::cout << "PRIVATE SCHOOL DAYS LXH MOUNT PASS: seven integrated ORS "
                 "override original Script.GPK, 21 seekable media entries, "
                 "independent Display Options overlay; no retail writes\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PRIVATE SCHOOL DAYS LXH MOUNT FAIL: " << error.what() << '\n';
    return 1;
  }
}
