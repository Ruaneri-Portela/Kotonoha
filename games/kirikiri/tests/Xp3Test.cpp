#include <Kirikiri/KagScript.hpp>
#include <Kirikiri/KirikiriAssets.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Helper: append little-endian integer to a byte vector.
template <typename T> void putLE(std::vector<std::uint8_t> &v, T value) {
  const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(&value);
  v.insert(v.end(), p, p + sizeof(T));
}

inline void putLe64(std::vector<std::uint8_t> &v, std::uint64_t x) {
  putLE(v, x);
}

} // namespace

int main() {
  const std::string fileContent =
      "[cm text=\"Hello, world!\" speaker=\"Narrator\"]\n"
      "[wait time=\"1000\"]";
  const std::string fileName = "scenario/first.ks";

  // ---- File table (uncompressed) ----
  std::vector<std::uint8_t> table;

  auto tag = [&](const char *s) { table.insert(table.end(), s, s + 4); };
  auto u16 = [&](std::uint16_t x) { putLE(table, x); };
  auto u32 = [&](std::uint32_t x) { putLE(table, x); };
  auto u64 = [&](std::uint64_t x) { putLE(table, x); };

  // "File" entry chunk
  size_t fileChunkSizePos = table.size();
  tag("File"); // chunk magic
  u64(0);      // chunk size (placeholder, patched later)

  // info sub-chunk
  size_t infoSizePos = table.size();
  tag("info");             // sub-chunk magic
  u64(0);                  // sub-chunk size (placeholder)
  u32(0);                  // flags (not encrypted)
  u64(fileContent.size()); // original size
  u64(fileContent.size()); // archive size
  u16(static_cast<std::uint16_t>(fileName.size())); // name length (chars)
  for (char c : fileName)
    u16(static_cast<std::uint16_t>(static_cast<unsigned char>(c))); // UTF-16LE
  // info sub-chunk size = content after size field =
  // flags+orig+arch+nameLen+name
  {
    size_t end = table.size();
    std::uint64_t size = end - (infoSizePos + 12); // exclude magic(4)+size(8)
    for (int i = 0; i < 8; ++i)
      table[infoSizePos + 4 + i] = std::uint8_t((size >> (8 * i)) & 0xFF);
  }

  // segm sub-chunk
  size_t segmSizePos = table.size();
  tag("segm");
  u64(0);                  // size (placeholder)
  u32(0);                  // flags (not compressed)
  u64(0);                  // data offset (placeholder)
  u64(fileContent.size()); // original size
  u64(fileContent.size()); // archive size
  {
    size_t end = table.size();
    std::uint64_t size = end - (segmSizePos + 12); // exclude magic(4)+size(8)
    for (int i = 0; i < 8; ++i)
      table[segmSizePos + 4 + i] = std::uint8_t((size >> (8 * i)) & 0xFF);
  }

  // Patch segm data offset = 40 (header start of file content)
  // segm layout: magic(4) + size(8) + flags(4) + offset(8) + origSize(8) +
  // archiveSize(8)
  std::uint64_t segmDataOffset = 40;
  for (int i = 0; i < 8; ++i)
    table[segmSizePos + 16 + i] =
        std::uint8_t((segmDataOffset >> (8 * i)) & 0xFF);

  // adlr sub-chunk
  size_t adlrSizePos = table.size();
  tag("adlr");
  u64(0);  // size (placeholder)
  u32(42); // key
  {
    size_t end = table.size();
    std::uint64_t size = end - (adlrSizePos + 12); // exclude magic(4)+size(8)
    for (int i = 0; i < 8; ++i)
      table[adlrSizePos + 4 + i] = std::uint8_t((size >> (8 * i)) & 0xFF);
  }

  // Patch "File" chunk total size = content after size field = info+segm+adlr
  {
    size_t end = table.size();
    std::uint64_t size =
        end - (fileChunkSizePos + 12); // exclude magic(4)+size(8)
    for (int i = 0; i < 8; ++i)
      table[fileChunkSizePos + 4 + i] = std::uint8_t((size >> (8 * i)) & 0xFF);
  }

  // ---- Assemble archive ----
  std::vector<std::uint8_t> data;

  const std::uint8_t magic[11] = {0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20,
                                  0x0A, 0x1A, 0x8B, 0x67, 0x01};
  data.insert(data.end(), magic, magic + 11);

  // XP3 v2 header: magic(11) + info_offset(8) + version(4) +
  //                table_size(8) + flags(1) + table_offset(8)
  const size_t headerSize = 40;
  const std::uint64_t tableMetaOffset =
      headerSize + fileContent.size(); // table metadata starts after content

  putLE(data, tableMetaOffset);  // info_offset
  putLE(data, std::uint32_t(1)); // version = XP3 v2
  putLe64(data, table.size());   // table_size
  data.push_back(0);             // flags
  putLE(data, tableMetaOffset);  // table_offset

  // File content
  data.insert(data.end(), fileContent.begin(), fileContent.end());

  // Table metadata: compressed(1) + compressed_size(8) + original_size(8)
  data.push_back(0);         // not compressed
  putLE(data, table.size()); // compressed size
  putLE(data, table.size()); // original size

  // Table data
  data.insert(data.end(), table.begin(), table.end());

  // ---- Write to temp dir ----
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "kirikiri_xp3_test";
  std::filesystem::create_directories(tempDir);
  std::filesystem::path xp3Path = tempDir / "data.xp3";
  {
    std::ofstream out(xp3Path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              std::streamsize(data.size()));
  }

  Kotonoha::Providers::KirikiriAssets assets(tempDir);
  assets.mountXp3("data.xp3");

  int failures = 0;

  if (!assets.containsAsset("scenario/first.ks")) {
    std::printf("FAIL: containsAsset(scenario/first.ks) should be true\n");
    ++failures;
  }
  if (assets.containsAsset("nonexistent.ks")) {
    std::printf("FAIL: containsAsset(nonexistent.ks) should be false\n");
    ++failures;
  }

  try {
    auto content = assets.readAsset("scenario/first.ks");
    std::string text(content.begin(), content.end());
    if (text != fileContent) {
      std::printf("FAIL: read content mismatch\n");
      ++failures;
    } else {
      std::printf("PASS: readAsset returned KAG script\n");
    }
  } catch (const std::exception &e) {
    std::printf("FAIL: readAsset threw: %s\n", e.what());
    ++failures;
  }

  auto files = assets.listAsset("scenario/");
  bool found = false;
  for (const auto &f : files) {
    if (f == "scenario/first.ks")
      found = true;
  }
  if (!found) {
    std::printf("FAIL: listAsset did not include scenario/first.ks\n");
    ++failures;
  } else {
    std::printf("PASS: listAsset found scenario/first.ks\n");
  }

  auto scenarios = assets.scenarioFiles();
  bool scenarioFound = false;
  for (const auto &s : scenarios) {
    if (s == "scenario/first.ks")
      scenarioFound = true;
  }
  if (!scenarioFound) {
    std::printf("FAIL: scenarioFiles did not include scenario/first.ks\n");
    ++failures;
  } else {
    std::printf("PASS: scenarioFiles found scenario/first.ks\n");
  }

  // Loose KAG patches commonly use a UTF-16 BOM (the NEKOPARA VNX+
  // sysmenu.ks is one real-world example). They must reach the parser as
  // UTF-8 instead of producing one NUL byte between every character.
  {
    const std::vector<std::uint8_t> utf16 = {
        0xff, 0xfe, '[', 0, 'c', 0, 'm', 0, ' ', 0, 't', 0, 'e', 0, 'x', 0,
        't',  0,    '=', 0, '"', 0, 'O', 0, 'K', 0, '"', 0, ']', 0};
    const auto path = tempDir / "utf16.ks";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(utf16.data()),
              std::streamsize(utf16.size()));
    out.close();
    Kotonoha::Providers::KirikiriAssets looseAssets(tempDir);
    const auto text = looseAssets.loadTextUtf8("utf16.ks");
    if (looseAssets.read("utf16.ks") != utf16) {
      std::printf("FAIL: raw read changed the UTF-16 asset\n");
      ++failures;
    }
    if (text != "[cm text=\"OK\"]") {
      std::printf("FAIL: UTF-16 KAG script was not decoded\n");
      ++failures;
    }
  }

  // Save restoration resumes after the already-presented dialogue, and label
  // jumps alter the actual runner rather than being a logging-only stub.
  {
    auto script = Kirikiri::KagScript::parse(
        "[cm text=\"first\"]\n*later\n[cm text=\"second\"]", "state.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks callbacks;
    callbacks.showText = [&shown](std::string, std::string text) {
      shown.push_back(std::move(text));
    };
    Kirikiri::KagScript::Runner runner(script, callbacks);
    runner.run();
    const auto savedPosition = runner.position();
    Kirikiri::KagScript::Runner restored(script, callbacks);
    restored.restorePosition(savedPosition);
    restored.run();
    if (shown.size() != 2 || shown.back() != "second") {
      std::printf("FAIL: restored KAG runner did not resume after dialogue\n");
      ++failures;
    }
    Kirikiri::KagScript::Runner jumped(script, callbacks);
    if (!jumped.jumpToLabel("later")) {
      std::printf("FAIL: KAG label jump was not resolved\n");
      ++failures;
    }
  }

  std::filesystem::remove_all(tempDir);

  if (failures == 0) {
    std::printf("All XP3 tests passed.\n");
    return 0;
  }
  std::printf("%d test(s) failed.\n", failures);
  return 1;
}
