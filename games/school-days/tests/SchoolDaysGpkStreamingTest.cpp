#include "SchoolDaysAssets.hpp"

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <zlib.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr std::array<uint8_t, 16> kXorKey = {
    0x82, 0xEE, 0x1D, 0xB3, 0x57, 0xE9, 0x2C, 0xC2,
    0x2F, 0x54, 0x7B, 0x10, 0x4C, 0x9A, 0x75, 0x49,
};

void Require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

void U16(std::vector<uint8_t> *out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

void U32(std::vector<uint8_t> *out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
  out->push_back(static_cast<uint8_t>(value >> 16u));
  out->push_back(static_cast<uint8_t>(value >> 24u));
}

std::vector<uint8_t> Deflate(const std::vector<uint8_t> &input) {
  uLongf size = compressBound(static_cast<uLong>(input.size()));
  std::vector<uint8_t> out(static_cast<size_t>(size));
  Require(compress2(out.data(), &size, input.data(),
                    static_cast<uLong>(input.size()),
                    Z_BEST_COMPRESSION) == Z_OK,
          "Synthetic compression failed");
  out.resize(static_cast<size_t>(size));
  return out;
}

std::vector<uint8_t> BuildPack(const std::u16string &name,
                               const std::vector<uint8_t> &logical,
                               bool compressed, size_t prefixLength) {
  constexpr uint32_t firstOffset = 5120u;
  std::vector<uint8_t> pack(firstOffset, 0u);
  std::vector<uint8_t> stored = compressed ? Deflate(logical) : logical;
  Require(prefixLength <= stored.size() && prefixLength <= 255u,
          "Synthetic prefix is invalid");

  std::vector<uint8_t> index;
  U16(&index, static_cast<uint16_t>(name.size()));
  for (char16_t unit : name)
    U16(&index, static_cast<uint16_t>(unit));
  U16(&index, 0u);
  U16(&index, 0u);
  U16(&index, 0u);
  U32(&index, firstOffset);
  U32(&index, static_cast<uint32_t>(stored.size()));
  const char *magic = compressed ? "DFLT" : "    ";
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
  for (size_t i = 0; i < encoded.size(); ++i)
    encoded[i] ^= kXorKey[i & 15u];
  pack.insert(pack.end(), encoded.begin(), encoded.end());
  pack.insert(pack.end(),
              {'S', 'T', 'K', 'F', 'i', 'l', 'e', '0', 'P', 'I', 'D', 'X'});
  U32(&pack, static_cast<uint32_t>(encoded.size()));
  pack.insert(pack.end(), {'S', 'T', 'K', 'F', 'i', 'l', 'e', '0', 'P', 'A',
                           'C', 'K', 'F', 'I', 'L', 'E'});
  return pack;
}

void Write(const fs::path &path, const std::vector<uint8_t> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(out), "Cannot create synthetic pack");
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(out), "Cannot write synthetic pack");
}

void BuildRetailSet(const fs::path &packs) {
  fs::create_directories(packs);
  const std::vector<uint8_t> raw = {'P', 'R', 'E', 'F', 'I', 'X', '-', 'R',
                                    'A', 'W', '-', 'B', 'O', 'D', 'Y'};
  const std::vector<uint8_t> deflated = {'D', 'F', 'L', 'T', '-', 'F', 'A',
                                         'L', 'L', 'B', 'A', 'C', 'K'};
  for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs()) {
    const std::string prefix(spec.logicalPrefix);
    if (prefix == "BGM")
      Write(packs / spec.filename, BuildPack(u"RAW.BIN", raw, false, 6u));
    else if (prefix == "Movie00")
      Write(packs / spec.filename,
            BuildPack(u"COMPRESSED.BIN", deflated, true, 5u));
    else
      Write(packs / spec.filename, BuildPack(u"DUMMY.BIN", {'x'}, false, 0u));
  }
}

} // namespace

int main() {
  const fs::path root =
      fs::temp_directory_path() /
      ("kotonoha-sd-gpk-stream-" + std::to_string(SDL_GetPerformanceCounter()));
  std::error_code ec;
  fs::remove_all(root, ec);
  try {
    BuildRetailSet(root);
    SchoolDays::Assets assets(root);

    const std::string rawId = assets.resolve("BGM/RAW.BIN");
    Require(Kotonoha::IsSchoolDaysGpkUri(rawId),
            "Raw fixture did not resolve to GPK URI");
    const auto overlay = root / "overlay";
    fs::create_directories(overlay / "BGM");
    Write(overlay / "BGM/RAW.BIN", {'O', 'V', 'E', 'R', 'L', 'A', 'Y'});
    std::string overlayError;
    Require(Kotonoha::SetSchoolDaysAssetOverlay(overlay, &overlayError),
            "Synthetic overlay could not be mounted: " + overlayError);
    const auto effective = assets.resolve("BGM/RAW.BIN");
    Require(!Kotonoha::IsSchoolDaysGpkUri(effective) &&
                fs::equivalent(effective, overlay / "BGM/RAW.BIN"),
            "Normal lookup did not continue to use the overlay");
    const auto forcedOriginal = assets.read(rawId);
    Require(std::string(forcedOriginal.begin(), forcedOriginal.end()) ==
                "PREFIX-RAW-BODY",
            "Explicit GPK URI was redirected through the overlay");
    auto stream = assets.openStream(rawId);
    Require(stream && stream->size() == 15u,
            "Raw GPK entry did not expose seekable stream");
    std::array<Uint8, 8> buffer{};
    Require(stream->read(buffer.data(), 4u) == 4u &&
                std::string(buffer.begin(), buffer.begin() + 4) == "PREF",
            "Raw stream did not reconstruct inline prefix");
    Require(stream->seek(7u) && stream->read(buffer.data(), 8u) == 8u &&
                std::string(buffer.begin(), buffer.end()) == "RAW-BODY",
            "Raw stream seek/read did not address GPK body range");
    Require(!stream->seek(stream->size() + 1u),
            "Raw stream accepted out-of-range seek");

    const std::string compressedId = assets.resolve("Movie00/COMPRESSED.BIN");
    Require(!assets.openStream(compressedId),
            "Compressed GPK entry incorrectly exposed a raw stream");
    const auto inflated = assets.read(compressedId);
    Require(std::string(inflated.begin(), inflated.end()) == "DFLT-FALLBACK",
            "Compressed GPK fallback did not materialize decoded bytes");
  } catch (const std::exception &error) {
    fs::remove_all(root, ec);
    std::cerr << "SchoolDaysGpkStreamingTest: " << error.what() << '\n';
    return 1;
  }
  fs::remove_all(root, ec);
  std::cout << "SchoolDays GPK streaming passed\n";
  return 0;
}
