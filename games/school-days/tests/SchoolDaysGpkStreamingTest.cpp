#include "SchoolDaysAssets.hpp"

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

std::vector<uint8_t> BuildPack(const std::u16string& name,
                               const std::vector<uint8_t>& logical,
                               bool compressed, size_t prefixLength) {
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

  pack.insert(pack.end(), stored.begin() + prefixLength, stored.end());
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
    else
      Write(packs / spec.filename, BuildPack(u"DUMMY.BIN", {'x'}, false, 0u));
  }
}

void ExpectMountRejected(const fs::path& root, const std::string& why) {
  std::string reason;
  bool rejected = false;
  try {
    SchoolDays::Assets assets(root);
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

int main() {
  const fs::path root = fs::temp_directory_path() /
                        ("kotonoha-sd-gpk-stream-" +
                         std::to_string(SDL_GetPerformanceCounter()));
  std::error_code ec;
  fs::remove_all(root, ec);
  try {
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
