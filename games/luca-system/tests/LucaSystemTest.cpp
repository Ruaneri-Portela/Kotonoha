// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Integration/format tests for the LucaSystem port. When real LucaSystem data
// is available (LUCA_SYSTEM_TEST_DATA env, defaulting to the Steam AIR
// installation) the tests validate PAK reading and script parsing against it;
// otherwise they exercise a synthetic in-format archive.
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemScript.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  if (ok) {
    std::printf("ok   - %s\n", what.c_str());
  } else {
    std::printf("FAIL - %s\n", what.c_str());
    ++failures;
  }
}

std::filesystem::path tempDir() {
  auto p = std::filesystem::temp_directory_path() / "luca_system_test";
  std::filesystem::create_directories(p);
  return p;
}

std::vector<std::uint8_t> u32le(uint32_t v) {
  return {std::uint8_t(v & 0xff), std::uint8_t((v >> 8) & 0xff),
          std::uint8_t((v >> 16) & 0xff), std::uint8_t((v >> 24) & 0xff)};
}

std::vector<std::uint8_t> u16le(uint16_t v) {
  return {std::uint8_t(v & 0xff), std::uint8_t((v >> 8) & 0xff)};
}

// Build a minimal LucaSystem PAK (matching wetor/LuckSystem pak.go layout).
// Layout:
//   [0..35]   header: headerLength, fileCount, idStart, blockSize, unk..,
//             flags (at offset 32)
//   [36..39]  nameTablePtr (points to name table after data, when named)
//   [40..43]  sentinel = headerLength / blockSize
//   [44..]    offset table: 8 bytes per entry (offsetInBlocks uint32, length
//   uint32) then      data region (each entry padded to blockSize) then name
//   table: null-terminated names (one per entry, in order)
std::vector<std::uint8_t>
buildPak(uint32_t idStart, uint32_t blockSize, uint32_t flags,
         const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
             &files) {
  const bool named = (flags & 512u) != 0u;
  const size_t fileCount = files.size();
  const size_t offsetTableStart = 40;

  // Dry run: compute data offsets/lengths and total data size.
  std::vector<uint32_t> offsetBlocks;
  std::vector<uint32_t> lengths;
  const size_t dataStart = (offsetTableStart + 8 * fileCount + blockSize - 1) /
                           blockSize * blockSize;
  size_t dataCursor = dataStart;
  for (const auto &[name, data] : files) {
    offsetBlocks.push_back(uint32_t(dataCursor / blockSize));
    lengths.push_back(uint32_t(data.size()));
    dataCursor += data.size();
    dataCursor += (blockSize - (data.size() % blockSize)) % blockSize;
  }
  const size_t dataEnd = dataCursor; // end of data region (= names start)
  const uint32_t headerLength = uint32_t(dataStart);

  // Name table start (= end of data region), and total file size (after names).
  size_t nameTableStart = dataEnd;
  size_t totalSize = dataEnd;
  if (named) {
    for (const auto &[name, _] : files) {
      totalSize += name.size() + 1; // + NUL
    }
    nameTableStart = dataEnd;
  }

  std::vector<std::uint8_t> out(totalSize, 0);
  auto u32 = [&](size_t off, uint32_t v) {
    std::memcpy(&out[off], u32le(v).data(), 4);
  };

  // Header (36 bytes).
  u32(0, headerLength);
  u32(4, uint32_t(fileCount));
  u32(8, idStart);
  u32(12, blockSize);
  // offsets 16..31 are unk (zero)
  u32(32, flags); // flags at offset 32
  if (named)
    u32(36, uint32_t(nameTableStart)); // nameTablePtr (offsetPos - 4)

  // Offset table + data region.
  dataCursor = headerLength;
  for (size_t i = 0; i < files.size(); ++i) {
    const auto &data = files[i].second;
    size_t base = offsetTableStart + 8 * i;
    u32(base, offsetBlocks[i]);
    u32(base + 4, lengths[i]);
    std::memcpy(&out[dataCursor], data.data(), data.size());
    dataCursor += data.size();
    dataCursor += (blockSize - (data.size() % blockSize)) % blockSize;
  }

  // Name table (named only).
  if (named) {
    size_t cursor = dataEnd;
    for (const auto &[name, _] : files) {
      std::memcpy(&out[cursor], name.data(), name.size());
      cursor += name.size() + 1; // + NUL
    }
  }
  return out;
}

void writeFile(std::filesystem::path p, std::string_view data) {
  std::ofstream f(p, std::ios::binary);
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main() {
  std::printf("LucaSystem format tests\n");

  // ---- Synthetic PAK round-trip ----
  {
    auto dir = tempDir();
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files = {
        {"alpha.txt", {0x61, 0x62, 0x63}},
        {"beta.bin",
         {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}},
    };
    auto pak = buildPak(100, 4, 512, files);
    writeFile(dir / "test.pak", std::string(pak.begin(), pak.end()));

    LucaSystem::PakArchive archive(dir / "test.pak");
    check(archive.fileCount() == 2, "PAK fileCount == 2");
    check(archive.idStart() == 100, "PAK idStart == 100");
    check(archive.blockSize() == 4, "PAK blockSize == 4");
    check(archive.named(), "PAK named entries");

    const LucaSystem::PakEntry *e0 = archive.findByName("alpha.txt");
    check(e0 != nullptr && e0->id == 100, "PAK findByName alpha.txt id=100");
    check(e0 != nullptr && e0->length == 3, "PAK alpha.txt length=3");
    const LucaSystem::PakEntry *e1 = archive.findById(101);
    check(e1 != nullptr && e1->name == "beta.bin", "PAK findById 101 beta.bin");
    const LucaSystem::PakEntry *e2 = archive.findByIndex(0);
    check(e2 != nullptr && e2->name == "alpha.txt", "PAK findByIndex 0");

    auto data = archive.read("alpha.txt");
    check(data.size() == 3 && data[0] == 0x61 && data[1] == 0x62 &&
              data[2] == 0x63,
          "PAK read alpha.txt contents");
    auto data2 = archive.read(*e1);
    check(data2.size() == 10, "PAK read beta.bin length=10");
    check(archive.findByName("missing") == nullptr, "PAK missing entry null");
  }

  // ---- Synthetic script parse ----
  {
    // One instruction: opcode 1, fixedFlag 1 (one fixed uint16 = 42),
    // param bytes holding a Shift-JIS string "hello" + NUL, then an align.
    // len = 4 header + raw. raw = fixed(2) + param("hello\0") + no align if
    // even.
    std::vector<std::uint8_t> raw;
    raw.push_back(0x2a);
    raw.push_back(0x00); // fixed uint16 = 42
    const char *sjis = "hello\0";
    raw.insert(raw.end(), sjis, sjis + 6);
    uint16_t len = uint16_t(4 + raw.size());
    if (len & 1) {
      raw.push_back(0);
      len = uint16_t(4 + raw.size());
    }
    std::vector<std::uint8_t> scriptData;
    auto lenb = u16le(len);
    scriptData.insert(scriptData.end(), lenb.begin(), lenb.end());
    scriptData.push_back(1); // opcode
    scriptData.push_back(1); // fixedFlag
    scriptData.insert(scriptData.end(), raw.begin(), raw.end());

    LucaSystem::Script script("test.scr", std::move(scriptData));
    check(script.valid(), "script valid");
    check(script.instructionCount() == 1, "script one instruction");
    const auto &ins = script.instructions()[0];
    check(ins.opcode == 1, "instruction opcode == 1");
    check(ins.fixedFlag == 1, "instruction fixedFlag == 1");
    check(script.strings().empty(),
          "non-MESSAGE instruction does not expose arbitrary byte strings");
  }

  // ---- Real data (AIR) when available ----
  std::string air;
  if (const char *env = std::getenv("LUCA_SYSTEM_TEST_DATA"))
    air = env;
  else
    air = "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files";
  if (std::filesystem::is_directory(air)) {
    std::printf("ok   - AIR data dir present: %s\n", air.c_str());

    LucaSystem::PakArchive param(air + "/PARAM.PAK");
    check(param.fileCount() >= 1, "AIR PARAM.PAK has entries");
    const LucaSystem::PakEntry *cg = param.findByName("_cggallery");
    check(cg != nullptr, "AIR PARAM.PAK entry _cggallery");

    LucaSystem::PakArchive script(air + "/SCRIPT.PAK");
    check(script.fileCount() > 10, "AIR SCRIPT.PAK has many entries");
    const LucaSystem::PakEntry *seen = script.findByName("seen163");
    check(seen != nullptr, "AIR SCRIPT.PAK entry seen163");
    if (seen) {
      auto bytes = script.read(*seen);
      check(bytes.size() > 100, "AIR seen163 has substantial bytecode");
      LucaSystem::Script parsed("seen163", std::move(bytes));
      check(parsed.valid(), "AIR seen163 parses");
      check(parsed.instructionCount() > 10,
            "AIR seen163 has many instructions");
      check(!parsed.strings().empty(), "AIR seen163 extracts dialogue strings");
      std::printf("      seen163: %zu instructions, %zu strings\n",
                  parsed.instructionCount(), parsed.strings().size());
      if (!parsed.strings().empty())
        std::printf("      first string: %s\n", parsed.strings()[0].c_str());
    }

    LucaSystem::PakArchive sysse(air + "/SYSSE.PAK");
    check(sysse.fileCount() == 8, "AIR SYSSE.PAK has 8 SE entries");
    check(sysse.findByName("CANCEL") != nullptr, "AIR SYSSE.PAK entry CANCEL");
    check(sysse.blockSize() == 2048, "AIR SYSSE.PAK blockSize 2048");

    LucaSystem::PakArchive syscg(air + "/image/SYSCG.PAK");
    for (const char *name : {"title_menu", "title_arrow", "title1a", "title2a",
                             "title3a", "title4a"}) {
      const auto *entry = syscg.findByName(name);
      check(entry != nullptr, std::string("AIR title asset ") + name);
      if (entry) {
        const auto image = syscg.read(*entry);
        check(image.size() >= 4 && image[0] == 'C' && image[1] == 'Z',
              std::string("AIR title asset CZ image ") + name);
        if (image.size() >= 15) {
          const auto u16 = [&image](size_t p) {
            return unsigned(image[p]) | (unsigned(image[p + 1]) << 8);
          };
          std::printf("      %s: CZ%d, %ux%u %u-bit, %zu bytes\n", name,
                      image[2] - '0', u16(8), u16(10), u16(12), image.size());
        }
        if (std::string(name) == "title_menu" ||
            std::string(name) == "title1a" ||
            std::string(name) == "title_arrow") {
          const auto decoded = LucaSystem::DecodeCzImage(image);
          check(decoded.rgba.size() ==
                    size_t(decoded.width) * size_t(decoded.height) * 4,
                std::string("AIR decodes original title pixels ") + name);
          if (std::string(name) == "title_menu") {
            bool active = false;
            int start = 0, gap = 0;
            std::printf("      title menu alpha groups:");
            for (int x = 0; x < decoded.width; ++x) {
              bool ink = false;
              for (int y = 70; y < 140; ++y)
                ink =
                    ink ||
                    decoded.rgba[(size_t(y) * decoded.width + x) * 4 + 3] > 16;
              if (ink) {
                if (!active) {
                  active = true;
                  start = x;
                }
                gap = 0;
              } else if (active && ++gap > 24) {
                std::printf(" [%d,%d]", start, x - gap);
                active = false;
              }
            }
            if (active)
              std::printf(" [%d,%d]", start, decoded.width - 1);
            std::printf("\n");
          }
        }
      }
    }

    LucaSystem::PakArchive parts(air + "/image/PARTS.PAK");
    for (const char *name :
         {"MWIN1280_0", "MWIN1280_1", "MWIN_CURSOR", "SELWIN", "SELWIN_s"}) {
      const auto *entry = parts.findByName(name);
      check(entry != nullptr, std::string("AIR VN UI asset ") + name);
      if (entry) {
        const auto bytes = parts.read(*entry);
        check(bytes.size() >= 15 && bytes[0] == 'C' && bytes[1] == 'Z',
              std::string("AIR VN UI asset CZ image ") + name);
        if (bytes.size() >= 15)
          std::printf("      %s: CZ%c, %ux%u %u-bit\n", name, bytes[2],
                      unsigned(bytes[8]) | unsigned(bytes[9]) << 8,
                      unsigned(bytes[10]) | unsigned(bytes[11]) << 8,
                      unsigned(bytes[12]) | unsigned(bytes[13]) << 8);
        const auto decoded = LucaSystem::DecodeCzImage(bytes);
        check(decoded.rgba.size() ==
                  size_t(decoded.width) * size_t(decoded.height) * 4,
              std::string("AIR decodes original VN UI pixels ") + name);
      }
    }

    const auto root = std::filesystem::path(air).filename() == "files"
                          ? std::filesystem::path(air).parent_path()
                          : std::filesystem::path(air);
    LucaSystem::LucaSystemAssets assets(root, {});
    check(assets.containsAsset("seen163"),
          "AIR root path resolves files/SCRIPT.PAK");
    check(assets.containsAsset("CANCEL"),
          "AIR recursively mounted SYSSE asset");
    check(assets.containsAsset("AIR_OP_A") ||
              assets.listAsset().size() >
                  script.fileCount() + param.fileCount() + sysse.fileCount(),
          "AIR recursively mounts resource PAKs");
  } else {
    std::printf("skip - AIR data dir not found at %s (set "
                "LUCA_SYSTEM_TEST_DATA)\n",
                air.c_str());
  }

  if (failures == 0) {
    std::printf("All LucaSystem format tests passed.\n");
    return 0;
  }
  std::printf("%d test(s) failed.\n", failures);
  return 1;
}
