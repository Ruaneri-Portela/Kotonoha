// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/PfsArchive.hpp>

#include <Kotonoha/Filesystem/FileHandle.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

class MemoryFileHandle final : public Kotonoha::Filesystem::FileHandle {
public:
  explicit MemoryFileHandle(std::vector<std::uint8_t> data)
      : data_(std::move(data)) {}

  std::int64_t read(void *destination, std::size_t count) override {
    if (pos_ >= data_.size())
      return 0;
    const std::size_t avail = data_.size() - pos_;
    const std::size_t n = std::min(count, avail);
    std::memcpy(destination, data_.data() + pos_, n);
    pos_ += n;
    return static_cast<std::int64_t>(n);
  }

  std::int64_t seek(std::int64_t offset,
                    Kotonoha::Filesystem::SeekOrigin origin) override {
    std::int64_t next = 0;
    if (origin == Kotonoha::Filesystem::SeekOrigin::Begin)
      next = offset;
    else if (origin == Kotonoha::Filesystem::SeekOrigin::Current)
      next = static_cast<std::int64_t>(pos_) + offset;
    else
      next = static_cast<std::int64_t>(data_.size()) + offset;
    if (next < 0 || static_cast<std::size_t>(next) > data_.size())
      return -1;
    pos_ = static_cast<std::size_t>(next);
    return next;
  }

  std::int64_t tell() const override { return static_cast<std::int64_t>(pos_); }
  std::int64_t size() const override {
    return static_cast<std::int64_t>(data_.size());
  }

private:
  std::vector<std::uint8_t> data_;
  std::size_t pos_ = 0;
};

void appendU32LE(std::vector<std::uint8_t> &buf, std::uint32_t v) {
  buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

std::vector<std::uint8_t> buildSyntheticPfs(
    char versionChar,
    const std::vector<std::pair<std::string, std::string>> &files) {
  // Compute table size first.
  std::uint32_t tableLen = 4;
  for (const auto &[name, content] : files) {
    tableLen += 4 + static_cast<std::uint32_t>(name.size()) + 12;
  }

  std::uint32_t dataOffset = 7 + tableLen;
  std::vector<std::uint8_t> table;
  appendU32LE(table, static_cast<std::uint32_t>(files.size()));
  for (const auto &[name, content] : files) {
    appendU32LE(table, static_cast<std::uint32_t>(name.size()));
    table.insert(table.end(), name.begin(), name.end());
    appendU32LE(table, 0);
    appendU32LE(table, dataOffset);
    appendU32LE(table, static_cast<std::uint32_t>(content.size()));
    dataOffset += static_cast<std::uint32_t>(content.size());
  }

  const auto key = Artemis::PfsArchive::sha1(table.data(), table.size());
  const bool encrypt = (versionChar >= '8');

  std::vector<std::uint8_t> out;
  out.push_back('p');
  out.push_back('f');
  out.push_back(static_cast<std::uint8_t>(versionChar));
  appendU32LE(out, static_cast<std::uint32_t>(table.size()));
  out.insert(out.end(), table.begin(), table.end());

  for (const auto &[name, content] : files) {
    for (std::size_t i = 0; i < content.size(); ++i) {
      std::uint8_t b = static_cast<std::uint8_t>(content[i]);
      if (encrypt)
        b ^= key[i % 20];
      out.push_back(b);
    }
  }
  return out;
}

void testSha1() {
  // RFC 3174: SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
  const char *msg = "abc";
  const auto digest =
      Artemis::PfsArchive::sha1(reinterpret_cast<const std::uint8_t *>(msg), 3);
  const std::uint8_t expected[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81,
                                     0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50,
                                     0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
  assert(std::memcmp(digest.data(), expected, 20) == 0);
}

void testPf8AndPf6Streaming() {
  const std::string payload(100, 'A');
  const std::string longPayload = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGH"
                                  "IJKLMNOPQRSTUVWXYZ!@#$%^&*()";

  auto pf8Bytes = buildSyntheticPfs(
      '8', {{"system.ini", "[WINDOWS]\nWIDTH = 1600\nHEIGHT = 900\n"},
            {"script\\gamestart.ast", longPayload}});
  auto pf8 = Artemis::PfsArchive::open(
      std::make_unique<MemoryFileHandle>(std::move(pf8Bytes)), "test.pfs");
  assert(pf8 != nullptr);
  assert(pf8->version() == 8);
  assert(pf8->encrypted());
  assert(pf8->entries().size() == 2);

  const auto *scriptEntry = pf8->find("script/gamestart.ast");
  assert(scriptEntry != nullptr);
  auto decoded = pf8->readEntry(*scriptEntry);
  assert(std::string(decoded.begin(), decoded.end()) == longPayload);

  // Test random-access streaming across 20-byte XOR key boundary.
  auto stream = pf8->openStream(*scriptEntry);
  assert(stream != nullptr);
  assert(stream->size() == longPayload.size());
  assert(stream->seek(17));
  std::vector<Uint8> slice(25);
  assert(stream->read(slice.data(), slice.size()) == 25);
  assert(std::string(slice.begin(), slice.end()) == longPayload.substr(17, 25));

  // Test unencrypted pf6 overlay.
  auto pf6Bytes =
      buildSyntheticPfs('6', {{"script\\gamestart.ast", "PATCHED_PF6_CONTENT"},
                              {"sound\\bgm\\bgm01_a.ogg", "INTRO"},
                              {"sound\\bgm\\bgm01_b.ogg", "LOOP"}});
  auto pf6 = Artemis::PfsArchive::open(
      std::make_unique<MemoryFileHandle>(std::move(pf6Bytes)), "test.pfs.000");
  assert(pf6 != nullptr);
  assert(pf6->version() == 6);
  assert(!pf6->encrypted());

  Artemis::ArtemisAssets assets;
  assets.mountArchive(pf8);
  assets.mountArchive(pf6);

  assert(assets.systemConfig().width == 1600);
  assert(assets.systemConfig().height == 900);

  auto patched = assets.read("script/gamestart.ast");
  assert(std::string(patched.begin(), patched.end()) == "PATCHED_PF6_CONTENT");

  auto [intro, loop] = assets.resolveBgmPaths("bgm01");
  assert(intro == "sound/bgm/bgm01_a.ogg");
  assert(loop == "sound/bgm/bgm01_b.ogg");
}

void testMultiVolumeDirectoryOverlay() {
  const auto tmpDir =
      std::filesystem::temp_directory_path() / "kotonoha_artemis_pfs_test";
  std::filesystem::remove_all(tmpDir);
  std::filesystem::create_directories(tmpDir);

  auto writeBin = [&](const std::string &name,
                      const std::vector<std::uint8_t> &bytes) {
    std::ofstream ofs(tmpDir / name, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  };

  writeBin("root.pfs",
           buildSyntheticPfs('8', {{"script\\01_c00.ast", "BASE_SCRIPT"},
                                   {"image\\bg\\bg01.png", "BASE_BG"}}));
  writeBin("root.pfs.000", buildSyntheticPfs('8', {{"script\\01_c00.ast",
                                                    "PATCH_000_SCRIPT"}}));
  writeBin("root.pfs.001",
           buildSyntheticPfs('6', {{"image\\bg\\bg01.png", "PATCH_001_BG"}}));

  Artemis::ArtemisAssets assets(tmpDir.string());
  assert(assets.archiveCount() == 3);
  assert(assets.resolveScriptPath("01_c_00") == "script/01_c00.ast");
  assert(assets.resolveImagePath(":bg/", "bg01") == "image/bg/bg01.png");

  auto sBytes = assets.read("script/01_c00.ast");
  assert(std::string(sBytes.begin(), sBytes.end()) == "PATCH_000_SCRIPT");
  auto bBytes = assets.read("image/bg/bg01.png");
  assert(std::string(bBytes.begin(), bBytes.end()) == "PATCH_001_BG");

  std::filesystem::remove_all(tmpDir);
}

void testNativeUiResolution() {
  const auto dir = std::filesystem::temp_directory_path() /
                   "kotonoha_artemis_ui_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::string uiTable =
      "lang = {\n"
      "\tui_title01_en = {\n"
      "\t\tbt_start = { name=\"bt_start\", file=\"title/bt_new_game\", "
      "x=30, y=744, w=278, h=121, p1=\"start\", p2=\"gamestart\" },\n"
      "\t\tbg = { name=\"bg\", file=\"title/background_2a\", "
      "x=0, y=0, w=1600, h=900 },\n"
      "\t},\n"
      "}\n";
  const auto writeBin = [](const std::filesystem::path &path,
                           const std::vector<std::uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  };
  writeBin(dir / "root.pfs",
           buildSyntheticPfs('8', {
                                      {"system.ini", "[WINDOWS]\nWIDTH=1600\n"
                                                     "HEIGHT=900\n"
                                                     "BOOT=system/first.iet\n"},
                                      {"script\\gamestart.ast", "GAME_START"},
                                      {"system\\table\\list_windows_en.tbl", uiTable},
                                      {"pc\\en\\title\\bt_new_game.png", "PNG"},
                                      {"pc\\en\\title\\background_trial.png",
                                       "TRIAL_BG"},
                                  }));

  Artemis::ArtemisAssets assets(dir.string());
  assert(assets.systemConfig().bootFile == "system/first.iet");
  const auto languages = assets.uiLanguages();
  assert(std::find(languages.begin(), languages.end(), "en") !=
         languages.end());
  assert(assets.uiTablePath() == "system/table/list_windows_en.tbl");
  assert(assets.resolveUiImagePath("title/bt_new_game") ==
         "pc/en/title/bt_new_game.png");
  assert(assets.resolveUiImagePath("title/does_not_exist").empty());
  // Build-variant fallback: the table names background_2a while the
  // shipped build contains only background_trial.png.
  assert(assets.resolveUiImagePath("title/background_2a") ==
         "pc/en/title/background_trial.png");
  assets.setUiLanguage("ja");
  assert(assets.uiLanguage() == "ja");
  // Only the English table exists in this synthetic archive.
  assert(assets.uiTablePath().empty());

  std::filesystem::remove_all(dir);
}

} // namespace

int main() {
  testSha1();
  testPf8AndPf6Streaming();
  testMultiVolumeDirectoryOverlay();
  testNativeUiResolution();
  std::cout << "[PfsArchiveTest] All tests passed!\n";
  return 0;
}
