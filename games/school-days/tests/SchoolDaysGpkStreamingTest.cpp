#include "SchoolDaysAssets.hpp"

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <zlib.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr std::array<uint8_t, 16> kXorKey = {
    0x82, 0xEE, 0x1D, 0xB3, 0x57, 0xE9, 0x2C, 0xC2,
    0x2F, 0x54, 0x7B, 0x10, 0x4C, 0x9A, 0x75, 0x49,
};

void Require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

void U16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

void U32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
  out->push_back(static_cast<uint8_t>(value >> 16u));
  out->push_back(static_cast<uint8_t>(value >> 24u));
}

std::vector<uint8_t> Deflate(const std::vector<uint8_t>& input) {
  uLongf size = compressBound(static_cast<uLong>(input.size()));
  std::vector<uint8_t> out(static_cast<size_t>(size));
  Require(compress2(out.data(), &size, input.data(), static_cast<uLong>(input.size()),
                    Z_BEST_COMPRESSION) == Z_OK,
          "Synthetic compression failed");
  out.resize(static_cast<size_t>(size));
  return out;
}

std::vector<uint8_t>
BuildPack(const std::u16string &name, const std::vector<uint8_t> &logical,
          bool compressed, size_t prefixLength, size_t bodyPadding = 0u,
          size_t indexPadding = 0u, uint8_t indexPaddingValue = 0u) {
  constexpr uint32_t firstOffset = 5120u;
  std::vector<uint8_t> pack(firstOffset, 0u);
  std::vector<uint8_t> stored = compressed ? Deflate(logical) : logical;
  Require(prefixLength <= stored.size() && prefixLength <= 255u,
          "Synthetic prefix is invalid");

  std::vector<uint8_t> index;
  U16(&index, static_cast<uint16_t>(name.size()));
  for (char16_t unit : name) U16(&index, static_cast<uint16_t>(unit));
  U16(&index, 0u);
  U16(&index, 0u);
  U16(&index, 0u);
  U32(&index, firstOffset);
  U32(&index, static_cast<uint32_t>(stored.size()));
  const char* magic = compressed ? "DFLT" : "    ";
  index.insert(index.end(), magic, magic + 4);
  U32(&index, compressed ? static_cast<uint32_t>(logical.size()) : 0u);
  index.push_back(static_cast<uint8_t>(prefixLength));
  index.insert(index.end(), stored.begin(), stored.begin() + prefixLength);
  U16(&index, 0u);
  U16(&index, 0u);
  index.insert(index.end(), indexPadding, indexPaddingValue);

  pack.insert(pack.end(), stored.begin() + prefixLength, stored.end());
  pack.insert(pack.end(), bodyPadding, 0u);
  auto compressedIndex = Deflate(index);
  std::vector<uint8_t> encoded;
  U32(&encoded, static_cast<uint32_t>(index.size()));
  encoded.insert(encoded.end(), compressedIndex.begin(), compressedIndex.end());
  for (size_t i = 0; i < encoded.size(); ++i) encoded[i] ^= kXorKey[i & 15u];
  pack.insert(pack.end(), encoded.begin(), encoded.end());
  pack.insert(pack.end(), {'S','T','K','F','i','l','e','0','P','I','D','X'});
  U32(&pack, static_cast<uint32_t>(encoded.size()));
  pack.insert(pack.end(), {'S','T','K','F','i','l','e','0','P','A','C','K','F','I','L','E'});
  return pack;
}

struct RawEntry {
  std::string logical;
  std::vector<uint8_t> bytes;
  bool compressed = false;
  bool validUncompressedLength = true;
};

std::vector<uint8_t> BuildMultiPack(const std::vector<RawEntry>& entries) {
  Require(!entries.empty(), "Cannot build an empty synthetic GPK");
  constexpr uint32_t firstOffset = 5120u;
  std::vector<uint8_t> pack(firstOffset, 0u);
  std::vector<uint8_t> index;
  uint32_t bodyOffset = firstOffset;
  for (const auto& entry : entries) {
    Require(!entry.logical.empty() &&
                entry.logical.size() <= std::numeric_limits<uint16_t>::max(),
            "Invalid integrated synthetic GPK entry name");
    const auto stored = entry.compressed ? Deflate(entry.bytes) : entry.bytes;
    Require(!stored.empty(), "Integrated synthetic GPK entries must be nonempty");
    const size_t prefixLength = std::min<size_t>(6u, stored.size());
    U16(&index, static_cast<uint16_t>(entry.logical.size()));
    for (unsigned char letter : entry.logical) {
      Require(letter <= 0x7fu, "Synthetic GPK path must be ASCII");
      U16(&index, static_cast<uint16_t>(letter));
    }
    U16(&index, 0u); // subVersion
    U16(&index, 0u); // version
    U16(&index, 0u); // reserved
    U32(&index, bodyOffset);
    U32(&index, static_cast<uint32_t>(stored.size()));
    const char* magic = entry.compressed ? "DFLT" : "    ";
    index.insert(index.end(), magic, magic + 4);
    U32(&index, entry.compressed
                   ? static_cast<uint32_t>(entry.bytes.size())
                   : entry.validUncompressedLength
                         ? static_cast<uint32_t>(stored.size()) : 0u);
    index.push_back(static_cast<uint8_t>(prefixLength));
    index.insert(index.end(), stored.begin(), stored.begin() + prefixLength);
    pack.insert(pack.end(), stored.begin() + prefixLength, stored.end());
    bodyOffset += static_cast<uint32_t>(stored.size() - prefixLength);
  }
  U16(&index, 0u); // PIDX terminator
  U16(&index, 0u);
  auto compressedIndex = Deflate(index);
  std::vector<uint8_t> encoded;
  U32(&encoded, static_cast<uint32_t>(index.size()));
  encoded.insert(encoded.end(), compressedIndex.begin(), compressedIndex.end());
  for (size_t i = 0; i < encoded.size(); ++i)
    encoded[i] ^= kXorKey[i & 15u];
  pack.insert(pack.end(), encoded.begin(), encoded.end());
  pack.insert(pack.end(), {'S','T','K','F','i','l','e','0','P','I','D','X'});
  U32(&pack, static_cast<uint32_t>(encoded.size()));
  pack.insert(pack.end(), {'S','T','K','F','i','l','e','0','P','A','C','K','F','I','L','E'});
  return pack;
}

// Upstream PIDX offset-range regression: a physical gap is legal, but entries
// must never overlap even when their prefixes are stored in the index.
std::vector<uint8_t> TwoEntryIndex(uint32_t secondOffset) {
  std::vector<uint8_t> index;
  const auto add = [&index](const std::u16string &name, uint32_t offset) {
    U16(&index, static_cast<uint16_t>(name.size()));
    for (char16_t c : name)
      U16(&index, static_cast<uint16_t>(c));
    U16(&index, 0u);
    U16(&index, 0u);
    U16(&index, 0u);
    U32(&index, offset);
    U32(&index, 5u); // one inline byte plus four physical bytes
    index.insert(index.end(), {' ', ' ', ' ', ' '});
    U32(&index, 0u);
    index.push_back(1u);
    index.push_back('P');
  };
  add(u"FIRST.BIN", 5120u);
  add(u"SECOND.BIN", secondOffset);
  U16(&index, 0u);
  U16(&index, 0u);
  return index;
}

std::vector<uint8_t> IntegratedPayload(std::string_view path) {
  const std::string synthetic =
      "INTEGRATED-LXH-GPK::" + std::string(path) + "::" +
      std::string(80u, '#');
  return std::vector<uint8_t>(synthetic.begin(), synthetic.end());
}

std::vector<RawEntry> IntegratedEntries() {
  std::vector<RawEntry> entries;
  for (const auto& path : Kotonoha::SchoolDaysLxhContent::PackEntries())
    entries.push_back({path, IntegratedPayload(path)});
  Require(entries.size() == 28u, "Expected exactly 28 integrated pack entries");
  return entries;
}

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(out), "Cannot create synthetic pack");
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(out), "Cannot write synthetic pack");
}

void BuildRetailSet(const fs::path& packs) {
  fs::create_directories(packs);
  const std::vector<uint8_t> raw = {'P','R','E','F','I','X','-','R','A','W','-','B','O','D','Y'};
  const std::vector<uint8_t> deflated = {'D','F','L','T','-','F','A','L','L','B','A','C','K'};
  for (const auto& spec : Kotonoha::SchoolDaysRetailGpkSpecs()) {
    const std::string prefix(spec.logicalPrefix);
    if (prefix == "BGM")
      Write(packs / spec.filename, BuildPack(u"RAW.BIN", raw, false, 6u));
    else if (prefix == "Movie00")
      Write(packs / spec.filename, BuildPack(u"COMPRESSED.BIN", deflated, true, 5u));
    else if (prefix == "Script")
      Write(packs / spec.filename,
            BuildPack(u"English/05/05-A2-F00.ENG.ORS",
                      {'R','E','T','A','I','L','-','E','N','D','I','N','G'}, false, 4u));
    else if (prefix == "Event00")
      Write(packs / spec.filename,
            BuildPack(u"PADDED.BIN", raw, false, 3u, 9u, 6u));
    else
      Write(packs / spec.filename, BuildPack(u"DUMMY.BIN", {'x'}, false, 0u));
  }
}

void ExpectMountRejected(const fs::path& root, const std::string& why) {
  std::string reason;
  bool rejected = false;
  try {
    SchoolDays::Assets assets(root, "", true, nullptr,
                              Kotonoha::SchoolDaysLxhMountMode::Required);
  } catch (const std::exception& error) {
    rejected = true;
    reason = error.what();
  }
  Require(rejected, "Invalid integrated pack unexpectedly mounted: " + why +
                        " (last error: " + reason + ")");
  Require(!Kotonoha::SchoolDaysRetailGpksMounted(),
          "Failed integrated pack left a process-wide GPK mount active");
}

void CheckIntegratedReadAndSeek(SchoolDays::Assets& assets,
                                const std::string& logical) {
  const bool image = logical.size() >= 4u &&
                     logical.compare(logical.size() - 4u, 4u, ".png") == 0;
  const std::string reference = assets.resolve(logical, image);
  Require(Kotonoha::IsSchoolDaysGpkUri(reference),
          "Integrated LxH resolved to a loose overlay, not GPK URI: " + logical);
  const auto expected = IntegratedPayload(logical);
  const auto decoded = assets.read(reference);
  Require(decoded == expected, "Integrated asset bytes drifted: " + logical);
  auto stream = assets.openStream(reference);
  Require(stream && stream->size() == expected.size(),
          "Integrated LxH raw stream unavailable: " + logical);
  std::array<Uint8, 16> chunk{};
  Require(stream->read(chunk.data(), chunk.size()) == chunk.size() &&
              std::equal(chunk.begin(), chunk.end(), expected.begin()),
          "Integrated LxH stream prefix mismatch: " + logical);
  const uint64_t middle = 6u + 12u;
  Require(stream->seek(middle) &&
              stream->read(chunk.data(), chunk.size()) == chunk.size() &&
              std::equal(chunk.begin(), chunk.end(), expected.begin() + middle),
          "Integrated LxH range seek mismatch: " + logical);
  Require(!stream->seek(stream->size() + 1u),
          "Integrated raw stream accepted out-of-range seek: " + logical);
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    if (argc != 3 || std::string_view(argv[1]) != "--write-synthetic-packs")
      return 2;
    try {
      const fs::path output = argv[2];
      Require(fs::create_directory(output),
              "Synthetic pack destination must be new");
      BuildRetailSet(output);
      Write(output / "LxH.GPK", BuildMultiPack(IntegratedEntries()));
      return 0;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  const fs::path root = fs::temp_directory_path() /
                        ("kotonoha-sd-gpk-stream-" +
                         std::to_string(SDL_GetPerformanceCounter()));
  std::error_code ec;
  fs::remove_all(root, ec);
  try {
    // Upstream parser coverage independent of the integrated 30-pack mount.
    Kotonoha::SchoolDaysGpkIndex index;
    std::string indexError;
    const auto gapped = TwoEntryIndex(5128u);
    Require(index.ParseDecoded(gapped.data(), gapped.size(), &indexError),
            "Nonoverlapping PIDX offset gap was rejected: " + indexError);
    const auto overlapping = TwoEntryIndex(5123u);
    Require(!index.ParseDecoded(overlapping.data(), overlapping.size(),
                                &indexError),
            "Overlapping PIDX entry ranges were accepted");
    Write(root / "malformed-index.GPK",
          BuildPack(u"INVALID.BIN", {'x'}, false, 0u, 0u, 3u, 0x7fu));
    Kotonoha::SchoolDaysGpkArchive malformedArchive;
    Require(!malformedArchive.Open(root / "malformed-index.GPK"),
            "Nonzero PIDX terminator padding was accepted");

    const auto packs = root / "Packs";
    BuildRetailSet(packs);
    ExpectMountRejected(root, "mandatory Packs/LxH.GPK is missing");

    auto integratedEntries = IntegratedEntries();
    integratedEntries.pop_back();
    Write(packs / "LxH.GPK", BuildMultiPack(integratedEntries));
    ExpectMountRejected(root, "27/28 integrated entries");

    integratedEntries = IntegratedEntries();
    integratedEntries.back().logical = "LxH/Media/UNKNOWN-LXH.ogg";
    Write(packs / "LxH.GPK", BuildMultiPack(integratedEntries));
    ExpectMountRejected(root, "28 entries but one canonical LxH path missing");

    integratedEntries = IntegratedEntries();
    integratedEntries[1].compressed = true;
    Write(packs / "LxH.GPK", BuildMultiPack(integratedEntries));
    ExpectMountRejected(root, "compressed LxH entry is not seekable raw");

    integratedEntries = IntegratedEntries();
    integratedEntries[1].validUncompressedLength = false;
    Write(packs / "LxH.GPK", BuildMultiPack(integratedEntries));
    ExpectMountRejected(root, "raw LxH entry length does not match stored");

    Write(packs / "LxH.GPK", BuildMultiPack(IntegratedEntries()));
    {
      // The physical root has no archives. Every open and reopen must use the
      // mount, and repeated ReadEntry calls on an IMount-backed archive must
      // reuse a cached handle rather than reopening on every entry.
      class CountingMount final : public Kotonoha::Filesystem::IMount {
      public:
        explicit CountingMount(
            std::shared_ptr<const Kotonoha::Filesystem::IMount> inner)
            : inner_(std::move(inner)) {}
        bool exists(std::string_view path) const override {
          return inner_->exists(path);
        }
        Kotonoha::Filesystem::FileInfo stat(std::string_view path) const override {
          return inner_->stat(path);
        }
        std::vector<Kotonoha::Filesystem::FileInfo>
        list(std::string_view path) const override {
          return inner_->list(path);
        }
        std::unique_ptr<Kotonoha::Filesystem::FileHandle>
        open(std::string_view path) const override {
          ++openCount;
          if (failOpens)
            throw Kotonoha::Filesystem::FilesystemError(
                Kotonoha::Filesystem::ErrorCode::IOError, "Simulated revoke");
          return inner_->open(path);
        }
        mutable size_t openCount = 0u;
        mutable bool failOpens = false;
      private:
        std::shared_ptr<const Kotonoha::Filesystem::IMount> inner_;
      };

      const auto rootWithoutPacks = root / "virtual-game";
      fs::create_directory(rootWithoutPacks);
      auto inner =
          std::make_shared<Kotonoha::Filesystem::DirectoryMount>(packs);
      auto counting = std::make_shared<CountingMount>(inner);
      {
        Kotonoha::SchoolDaysGpkArchive cachedArchive;
        std::string cachedError;
        Require(cachedArchive.Open("LxH.GPK", &cachedError, counting),
                "CountingMount failed to open LxH.GPK: " + cachedError);
        Require(counting->openCount == 1u,
                "Expected exactly 1 open during GPK Open()");
        for (const auto &logical : Kotonoha::SchoolDaysLxhContent::PackEntries()) {
          std::vector<uint8_t> bytes;
          Require(cachedArchive.Read(logical, &bytes, &cachedError) &&
                      bytes == IntegratedPayload(logical),
                  "Cached virtual GPK read mismatch: " + logical);
        }
        Require(counting->openCount == 2u,
                "Expected 28 virtual ReadEntry calls to reuse 1 cached stream");
        counting->failOpens = true;
        Require(!cachedArchive.OpenStream(),
                "OpenStream must still fail closed when mount revokes opens");
        counting->failOpens = false;
      }

      SchoolDays::Assets virtualAssets(rootWithoutPacks, {}, false, inner);
      for (const auto &logical : Kotonoha::SchoolDaysLxhContent::PackEntries())
        CheckIntegratedReadAndSeek(virtualAssets, logical);
#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_DISPLAY)
      // The synthetic retail fixture contains no options UI entries and has
      // no external overlay. All approved resources must resolve from
      // the executable and retain their exact decompressed sizes.
      const std::pair<const char *, size_t> displayResources[] = {
          {"Option_Display.png", 489489u},
          {"Option_Display_Chip.png", 198630u},
          {"Option_Display.cmap", 480008u},
          {"Option_Display_Wide.cmap", 360008u},
          {"Option_Display_Wide_Full.cmap", 921608u},
          {"Option_Display_Wide_Note.cmap", 589832u},
#if defined(KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART)
          {"Option_Def.png", 105855u},
          {"Option_Def_Chip.png", 74642u},
          {"Option_Sound.png", 103585u},
          {"Option_Sound_Chip.png", 87223u},
#endif
      };
      for (const auto &resource : displayResources) {
        const std::string logical = std::string("System/Option/") + resource.first;
        const auto reference = virtualAssets.resolve(logical);
        Require(Kotonoha::IsSchoolDaysGpkUri(reference) &&
                    virtualAssets.containsAsset(reference) &&
                    Kotonoha::HasSchoolDaysAssetOverride(logical),
                "Embedded Display resource failed to resolve: " + logical);
        const auto bytes = virtualAssets.read(reference);
        Require(bytes.size() == resource.second,
                "Embedded Display resource size mismatch: " + logical);
        Require(!virtualAssets.openStream(reference),
                "Compressed embedded Display asset exposed a retail stream");
        Require(Kotonoha::LookupMountedSchoolDaysAsset(logical, true) ==
                    Kotonoha::SchoolDaysGpkLookupStatus::Missing,
                "Original lookup must bypass the embedded Display overlay");
      }
      Require(Kotonoha::LookupMountedSchoolDaysAsset(
                  "system\\option\\option_display.png") ==
                  Kotonoha::SchoolDaysGpkLookupStatus::Present,
              "Embedded Display lookup lost case/separator normalization");
#if !defined(KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART)
      Require(Kotonoha::LookupMountedSchoolDaysAsset(
                  "System/Option/Option_Def.png") ==
                  Kotonoha::SchoolDaysGpkLookupStatus::Missing,
              "Retail-derived artwork must not be embedded by default");
#endif
#endif
    }
    Kotonoha::SchoolDaysGpkArchive integratedArchive;
    std::string archiveError;
    Require(integratedArchive.Open(packs / "LxH.GPK", &archiveError) &&
                integratedArchive.EntryCount() == 28u,
            "Integrated GPK archive has wrong 28-entry index: " + archiveError);

    const auto overlay = root / "private-display-overlay";
    Write(overlay / "Script/English/05/05-A2-F00.ENG.ORS",
          {'P','O','I','S','O','N','-','O','R','S'});
    Write(overlay / "LxH/Media/05-A2-F00-LXH.wmv",
          {'P','O','I','S','O','N','-','M','O','V','I','E'});
    Write(overlay / "System/Option/Option_Display.png",
          {'D','I','S','P','L','A','Y','-','O','V','E','R','R','I','D','E'});
    {
      SchoolDays::Assets assets(root, overlay.string());
      Require(Kotonoha::SchoolDaysRetailGpksMounted(),
              "30-pack GPK mount did not publish atomically");
      Require(Kotonoha::MountedSchoolDaysGpkEntryCount() == 29u + 28u,
              "Mounted entry census omitted mandatory 28-entry LxH pack");

      const std::string rawId = assets.resolve("BGM/RAW.BIN");
      Require(Kotonoha::IsSchoolDaysGpkUri(rawId),
              "Retail raw fixture did not resolve to GPK URI");
      auto stream = assets.openStream(rawId);
      Require(stream && stream->size() == 15u,
              "Retail raw entry did not expose seekable stream");
      std::array<Uint8, 8> buffer{};
      Require(stream->read(buffer.data(), 4u) == 4u &&
                  std::string(buffer.begin(), buffer.begin() + 4) == "PREF",
              "Retail raw stream did not reconstruct inline prefix");
      Require(stream->seek(7u) && stream->read(buffer.data(), 8u) == 8u &&
                  std::string(buffer.begin(), buffer.end()) == "RAW-BODY",
              "Retail raw stream seek/read did not address GPK body range");
      Require(!stream->seek(stream->size() + 1u),
              "Retail raw stream accepted out-of-range seek");

      // Upstream physical body gap and legal zero PIDX padding must not
      // change the reconstructed payload of an ordinary GPK resource.
      const auto padded = assets.read(assets.resolve("Event00/PADDED.BIN"));
      Require(std::string(padded.begin(), padded.end()) == "PREFIX-RAW-BODY",
              "GPK entry padding leaked into asset bytes or blocked loading");

      const std::string compressedId = assets.resolve("Movie00/COMPRESSED.BIN");
      Require(!assets.openStream(compressedId),
              "Compressed retail entry incorrectly exposed raw stream");
      const auto inflated = assets.read(compressedId);
      Require(std::string(inflated.begin(), inflated.end()) == "DFLT-FALLBACK",
              "Compressed GPK fallback did not decode bytes");

      for (const auto& logical : Kotonoha::SchoolDaysLxhContent::PackEntries())
        CheckIntegratedReadAndSeek(assets, logical);
      Require(Kotonoha::LookupMountedSchoolDaysAsset(
                  "Script/English/05/05-A2-F00.ENG.ORS") ==
                  Kotonoha::SchoolDaysGpkLookupStatus::Present,
              "Grafted ORS did not take precedence over retail Script.GPK");
      const auto display = assets.read(assets.resolve(
          "System/Option/Option_Display.png", true));
      Require(std::string(display.begin(), display.end()) == "DISPLAY-OVERRIDE",
              "Ordinary display-options overlay lost independent precedence");
    }
    Require(!Kotonoha::SchoolDaysRetailGpksMounted(),
            "Finished synthetic GPK test left global mount installed");
  } catch (const std::exception& error) {
    fs::remove_all(root, ec);
    std::cerr << "SchoolDaysGpkStreamingTest: " << error.what() << '\n';
    return 1;
  }
  fs::remove_all(root, ec);
  std::cout << "SchoolDays GPK streaming + integrated LxH passed\n";
  return 0;
}
