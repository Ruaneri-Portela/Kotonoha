#include <Kirikiri/KagScript.hpp>
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kirikiri/PsbFile.hpp>
#include <Kirikiri/ScnRunner.hpp>
#include <Kirikiri/TjsStartupVm.hpp>
#include <Kirikiri/TlgImage.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

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
  // XP3 resource names are often mixed case while scripts use Windows
  // separators. ASCII case folding must work without changing stored names.
  if (!assets.containsAsset("SCENARIO\\FIRST.KS") ||
      !assets.containsAsset("Scenario/First.Ks")) {
    std::printf("FAIL: XP3 paths were not matched case-insensitively\n");
    ++failures;
  }
  try {
    const auto alternate = assets.readAsset("SCENARIO\\FIRST.KS");
    if (std::string(alternate.begin(), alternate.end()) != fileContent) {
      std::printf("FAIL: folded XP3 lookup returned wrong contents\n");
      ++failures;
    }
  } catch (const std::exception &e) {
    std::printf("FAIL: folded XP3 read threw: %s\n", e.what());
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

  auto files = assets.listAsset("SCENARIO/");
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

  // Title artwork must decode from an ordinary mounted file as a real SDL
  // surface; the renderer uses this same virtual provider for layered images.
  {
    const auto titlePath = tempDir / "image" / "title.bmp";
    std::filesystem::create_directories(titlePath.parent_path());
    Kotonoha::SurfacePtr pixels(
        SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32));
    bool wrote = bool(pixels);
    if (pixels) {
      wrote =
          SDL_FillSurfaceRect(pixels.get(), nullptr,
                              SDL_MapSurfaceRGB(pixels.get(), 120, 90, 40)) &&
          SDL_SaveBMP(pixels.get(), titlePath.string().c_str());
    }
    if (!wrote) {
      std::printf("FAIL: cannot create synthetic title artwork\n");
      ++failures;
    } else {
      try {
        Kotonoha::Providers::KirikiriAssets loose(tempDir);
        const auto surface = loose.loadSurface("image/title.bmp");
        if (!surface || surface->w != 2 || surface->h != 2) {
          std::printf("FAIL: title picture was not decoded\n");
          ++failures;
        }
      } catch (const std::exception &error) {
        std::printf("FAIL: title picture decode threw: %s\n", error.what());
        ++failures;
      }
    }
  }

  // XP3 filenames are UTF-16LE, not Latin-1. Kanji and other codepoints
  // above U+07FF must survive lookup so graphical assets can be found.
  {
    const std::u16string japaneseName = u"scenario/猫猫猫猫猫.ks";
    if (japaneseName.size() != fileName.size()) {
      std::printf("FAIL: invalid Unicode XP3 fixture size\n");
      ++failures;
    } else {
      auto unicodeArchive = data;
      const std::size_t namePos = static_cast<std::size_t>(tableMetaOffset) +
                                  17u + infoSizePos + 12u + 22u;
      for (std::size_t i = 0; i < japaneseName.size(); ++i) {
        const std::uint16_t unit = japaneseName[i];
        unicodeArchive.at(namePos + i * 2u) = std::uint8_t(unit & 0xff);
        unicodeArchive.at(namePos + i * 2u + 1u) = std::uint8_t(unit >> 8);
      }
      const auto path = tempDir / "unicode.xp3";
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char *>(unicodeArchive.data()),
                std::streamsize(unicodeArchive.size()));
      out.close();
      assets.mountXp3("unicode.xp3");
      const std::string requested = u8"SCENARIO/猫猫猫猫猫.KS";
      if (!assets.containsAsset(requested) ||
          assets.loadTextUtf8(requested) != fileContent) {
        std::printf("FAIL: Unicode XP3 filename not loadable\n");
        ++failures;
      }

      // A dangling UTF-16 high surrogate in the XP3 metadata is invalid.
      auto corruptArchive = unicodeArchive;
      corruptArchive.at(namePos + 0u) = 0x00;
      corruptArchive.at(namePos + 1u) = 0xd8;
      const auto invalidPath = tempDir / "invalid-unicode.xp3";
      std::ofstream invalidOut(invalidPath, std::ios::binary);
      invalidOut.write(reinterpret_cast<const char *>(corruptArchive.data()),
                       std::streamsize(corruptArchive.size()));
      invalidOut.close();
      bool rejected = false;
      try {
        assets.mountXp3("invalid-unicode.xp3");
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: malformed UTF-16 XP3 path was accepted\n");
        ++failures;
      }
    }
  }

  // The standard KAG3 [bg storage] / [image storage] commands must invoke
  // the same callbacks as Kotonoha's earlier bgshow/chara_show aliases.
  {
    auto script = Kirikiri::KagScript::parse(
        "@bg storage=room time=120\n"
        "[image layer=base storage=title.png]\n"
        "[image layer=0 storage=cat.png left=10 top=20]\n"
        "[layopt layer=0 visible=false]\n"
        "[image layer=2 storage=hero.png left=30 top=40]\n"
        "[freeimage layer=2]\n"
        "[end]\n",
        "visual-tags.ks");
    std::vector<std::string> calls;
    Kirikiri::KagScript::Callbacks cb;
    cb.showBg = [&](std::string file, int time) {
      calls.push_back("bg:" + file + ":" + std::to_string(time));
    };
    cb.showChar = [&](std::string layer, std::string file, int x, int y, int) {
      calls.push_back("fg:" + layer + ":" + file + ":" + std::to_string(x) +
                      ":" + std::to_string(y));
    };
    cb.hideChar = [&](std::string layer, int) {
      calls.push_back("hide:" + layer);
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    const std::vector<std::string> expected = {
        "bg:room:120", "bg:title.png:0",      "fg:0:cat.png:10:20",
        "hide:0",      "fg:2:hero.png:30:40", "hide:2"};
    if (calls != expected || !runner.isFinished()) {
      std::printf("FAIL: KAG graphical tag callbacks did not run\n");
      ++failures;
    }
  }

  // Layer options must mutate an existing image rather than calling
  // hideChar (which permanently destroys the retained texture).
  {
    const auto script = Kirikiri::KagScript::parse(
        "[image layer=2 storage=hero.png left=20 top=30]"
        "[layopt layer=2 visible=false opacity=90 left=42 top=21 order=10]"
        "[layopt layer=2 visible=true]"
        "[freeimage layer=2][end]",
        "layopt-persistence.ks");
    std::vector<std::string> emitted;
    Kirikiri::KagScript::Callbacks cb;
    cb.showChar = [&](std::string layer, std::string file, int x, int y, int) {
      emitted.push_back("show:" + layer + ":" + file + ":" +
                        std::to_string(x) + ":" + std::to_string(y));
    };
    cb.hideChar = [&](std::string layer, int) {
      emitted.push_back("free:" + layer);
    };
    cb.updateLayer = [&](Kirikiri::KagScript::LayerOptions options) {
      if (options.layer != "2")
        emitted.push_back("bad-layer");
      else if (options.visible && !*options.visible) {
        if (!options.left || *options.left != 42 || !options.top ||
            *options.top != 21 || !options.opacity ||
            *options.opacity != 90 || !options.order ||
            *options.order != 10)
          emitted.push_back("bad-layout");
        emitted.push_back("hidden");
      } else if (options.visible && *options.visible) {
        if (options.left || options.top || options.opacity || options.order)
          emitted.push_back("bad-reset");
        emitted.push_back("visible");
      }
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (!runner.isFinished() ||
        emitted != std::vector<std::string>({
            "show:2:hero.png:20:30", "hidden", "visible", "free:2"})) {
      std::printf("FAIL: KAG layopt destroyed image or reset properties\n");
      ++failures;
    }

    bool invalid = false;
    try {
      Kirikiri::KagScript::Runner broken(
          Kirikiri::KagScript::parse(
              "[layopt layer=2 visible=perhaps][end]", "bad-layopt.ks"),
          cb);
      broken.run();
    } catch (const std::runtime_error &) {
      invalid = true;
    }
    if (!invalid) {
      std::printf("FAIL: malformed KAG layopt visibility accepted\n");
      ++failures;
    }
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

  // A malformed MDF/PSB payload must report a decode failure, not stall
  // indefinitely in zlib. No proprietary game files are used.
  {
    const auto path = tempDir / "corrupt.ks.scn";
    const std::uint8_t truncatedMdf[] = {'m', 'd', 'f', 0, 0, 0, 0, 0};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(truncatedMdf),
              sizeof(truncatedMdf));
    out.close();
    bool rejected = false;
    try {
      (void)assets.loadTextUtf8("corrupt.ks.scn");
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected) {
      std::printf("FAIL: truncated MDF stream was accepted\n");
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

  // KAG syntax used by Kirikiri2/Z: @commands, column-one comments
  // and escaped brackets must survive parsing without becoming dialogue.
  {
    const auto parsed = Kirikiri::KagScript::parse("; comment ignored\n"
                                                   "@wait time=12\n"
                                                   "*next|Caption\n"
                                                   "hello [[world\n"
                                                   "[cm text=\"ready\"]",
                                                   "kag-compat.ks");
    if (parsed.tags.size() != 4 || parsed.tags[0].name != "wait" ||
        parsed.tags[0].attrs.at("time") != "12" || parsed.tags[0].line != 2 ||
        parsed.labels.count("next") != 1 ||
        parsed.tags[2].type != Kirikiri::KagScript::TagType::Text ||
        parsed.tags[2].text != "hello [world") {
      std::printf("FAIL: KAG @commands/comments/escaped brackets\n");
      ++failures;
    }
  }

  // The original Kirikiri KAG parser skips leading tabs and aliases
  // repeated labels. Edited scripts can also indent these with spaces.
  {
    const auto parsed = Kirikiri::KagScript::parse(
        "\t; ignored comment\r\n"
        "\t@wait time=12\r\n"
        "  *intro|First page\r\n"
        "\t*intro|Second page\r\n"
        "\t*|Third page\r\n"
        "[cm text='ready ] now'][end]",
        "indented-labels.ks");
    const bool valid =
        parsed.tags.size() == 6 && parsed.tags[0].name == "wait" &&
        parsed.tags[0].line == 2 && parsed.tags[0].column == 2 &&
        parsed.labels.count("intro") == 1 &&
        parsed.labels.count("intro:2") == 1 &&
        parsed.labels.count("intro:3") == 1 &&
        parsed.labels.at("intro") == 1 &&
        parsed.labels.at("intro:2") == 2 &&
        parsed.labels.at("intro:3") == 3 &&
        parsed.tags[1].line == 3 && parsed.tags[1].column == 3 &&
        parsed.tags[4].attrs.at("text") == "ready ] now";
    Kirikiri::KagScript::Callbacks cb;
    Kirikiri::KagScript::Runner runner(parsed, cb);
    if (!valid || !runner.jumpToLabel("*intro:3") ||
        runner.position() != parsed.labels.at("intro:3")) {
      std::printf("FAIL: KAG indentation, duplicate labels or quoted ]\n");
      ++failures;
    }
  }
  // Malformed scripts must produce a finite, actionable parser error.
  {
    const std::vector<std::string> invalid = {
        "*|Unnamed first label\n[end]", "[cm text='unclosed]",
        "[cm text=missing"};
    for (const auto &source : invalid) {
      bool rejected = false;
      try {
        (void)Kirikiri::KagScript::parse(source, "malformed.ks");
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: malformed KAG scenario was accepted\n");
        ++failures;
      }
    }
  }

  // Inline TJS must not be lexed as KAG, even if it contains brackets,
  // quotes or text resembling KAG tags. Command names ignore ASCII case.
  {
    auto parsed = Kirikiri::KagScript::parse(
        "@ISCRIPT\n"
        "var title = \"[cm text=hidden]\";\n"
        "var item = options[0];\n"
        "  @EnDsCrIpT\n"
        "[CM text=shown][END]",
        "inline-tjs.ks");
    if (parsed.tags.size() != 4 || parsed.tags[0].name != "iscript" ||
        parsed.tags[1].name != "endscript" || parsed.tags[1].line != 4 ||
        parsed.tags[1].column != 3 || parsed.tags[2].name != "cm" ||
        parsed.tags[3].name != "end") {
      std::printf("FAIL: inline TJS body leaked into the KAG parser\n");
      ++failures;
    } else {
      std::vector<std::string> shown;
      Kirikiri::KagScript::Callbacks cb;
      cb.showText = [&](std::string, std::string text) {
        shown.push_back(std::move(text));
      };
      Kirikiri::KagScript::Runner runner(parsed, cb);
      runner.run();
      if (shown != std::vector<std::string>{"shown"} || !runner.isBlocked()) {
        std::printf("FAIL: inline TJS skipped valid dialogue\n");
        ++failures;
      }
      runner.resumeWait();
      if (!runner.isFinished()) {
        std::printf("FAIL: inline TJS prevented KAG completion\n");
        ++failures;
      }
    }
    const auto bracketed = Kirikiri::KagScript::parse(
        "[iscript]\r\nvar item = values[0];\r\n[endscript]\r\n"
        "[cm text=ok][end]", "bracketed-tjs.ks");
    if (bracketed.tags.size() != 4 ||
        bracketed.tags[1].name != "endscript" ||
        bracketed.tags[2].attrs.at("text") != "ok") {
      std::printf("FAIL: bracketed inline TJS/CRLF parser\n");
      ++failures;
    }
    for (const auto &unterminated : {"[iscript]\nvar x = array[0];",
                                     "@iscript\nvar x = '[cm]';"}) {
      bool rejected = false;
      try {
        (void)Kirikiri::KagScript::parse(unterminated, "missing-end.ks");
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: unterminated inline TJS was accepted\n");
        ++failures;
      }
    }
  }

  // An [ignore] condition is inverted relative to [if]. Its nested
  // exclusions must not leak dialogue when the outer condition is true.
  {
    auto parsed = Kirikiri::KagScript::parse(
        "[set name=flag value=1]"
        "[ignore exp=\"flag == 1\"]"
        "[cm text=hidden]"
        "[ignore exp=false][cm text=also-hidden][endignore]"
        "[endignore]"
        "[ignore exp=\"flag != 1\"][cm text=shown][endignore]"
        "[end]",
        "ignore.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string text) {
      shown.push_back(std::move(text));
    };
    Kirikiri::KagScript::Runner runner(parsed, cb);
    runner.run();
    if (shown != std::vector<std::string>{"shown"} || !runner.isBlocked()) {
      std::printf("FAIL: KAG [ignore] conditional or nested skip\n");
      ++failures;
    }
    runner.resumeWait();
    if (!runner.isFinished()) {
      std::printf("FAIL: KAG [endignore] failed to resume\n");
      ++failures;
    }
    bool rejected = false;
    try {
      Kirikiri::KagScript::Runner invalid(
          Kirikiri::KagScript::parse("[ignore exp=true][end]",
                                     "unclosed-ignore.ks"),
          {});
      invalid.run();
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    if (!rejected) {
      std::printf("FAIL: unterminated [ignore] was accepted\n");
      ++failures;
    }
  }

  // Preserve and execute KAG inline TJS without confusing inner TJS
  // brackets with KAG markup. Retained variables survive a second block.
  {
    Kotonoha::Providers::KirikiriAssets source(tempDir);
    Kotonoha::Providers::TjsStartupResult scope;
    const auto parsed = Kirikiri::KagScript::parse(
        "[iscript]\n"
        "var menuTitle = \"Original\";\n"
        "System.title = menuTitle + \" Menu\";\n"
        "[endscript]\n"
        "@iscript\n"
        "System.title = menuTitle + \" Menu 2\";\n"
        "@endscript\n"
        "[cm text=ready][end]\n", "inline-title.ks");
    std::vector<std::string> displayed;
    unsigned executed = 0;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string text) {
      displayed.push_back(std::move(text));
    };
    cb.executeInlineTjs = [&](std::string inlineCode) {
      ++executed;
      if (!Kotonoha::Providers::executeTjsInlineSubset(
              scope, source, inlineCode, "inline-title.ks"))
        throw std::runtime_error(scope.error);
    };
    Kirikiri::KagScript::Runner runner(parsed, cb);
    runner.run();
    if (executed != 2 || scope.title != "Original Menu 2" ||
        !scope.error.empty() || displayed != std::vector<std::string>{"ready"} ||
        !runner.isBlocked()) {
      std::printf("FAIL: KAG to TJS2 inline execution/retained state\n");
      ++failures;
    }
    runner.resumeWait();
    if (!runner.isFinished()) {
      std::printf("FAIL: KAG inline TJS scenario termination\n");
      ++failures;
    }
    const bool rejected = !Kotonoha::Providers::executeTjsInlineSubset(
        scope, source, "new UnsupportedNative();", "broken.ks", 32);
    if (!rejected || scope.error.empty()) {
      std::printf("FAIL: KAG inline TJS native object was accepted\n");
      ++failures;
    }
  }

  // KAG macros are declarations, not dialogue. Expand parameters and
  // preserve the target labels after macro definitions are removed.
  {
    const auto script = Kirikiri::KagScript::parse(
        "[macro name=message]"
        "[cm text=%caption|Default]"
        "[endmacro]\n"
        "*begin\n"
        "[message caption=First]"
        "[message]"
        "[end]",
        "macros.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string text) {
      shown.push_back(std::move(text));
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    if (!runner.jumpToLabel("*begin") ||
        script.labels.at("begin") != 0 || script.tags.size() != 4) {
      std::printf("FAIL: macro expansion shifted KAG labels incorrectly\n");
      ++failures;
    } else {
      runner.run();
      runner.resumeWait();
      runner.resumeWait();
      if (shown != std::vector<std::string>({"First", "Default"}) ||
          !runner.isFinished()) {
        std::printf("FAIL: macro arguments/defaults or runner resume\n");
        ++failures;
      }
    }
    for (const auto &bad : {
             "[macro name=x][x][endmacro][x]",
             "[macro name=x][cm text=bad]",
             "[endmacro]",
             "[macro name=x]\n*invalid\n[endmacro][x]"}) {
      bool rejected = false;
      try {
        (void)Kirikiri::KagScript::parse(bad, "invalid-macros.ks");
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: malformed KAG macro accepted\n");
        ++failures;
      }
    }
  }

  // Real KAG conditional expressions combine boolean operators, numeric
  // comparisons, quoted literals, parentheses and variable dereferences.
  {
    auto script = Kirikiri::KagScript::parse(
        "[set name=flag value=2]"
        "[if exp=\"(flag >= 2 && flag < 3) || false\"]"
        "[cm text=compound]"
        "[else][cm text=bad-branch][endif]"
        "[if exp=\"!(flag == 2) || ('a==b' == 'a==b')\"]"
        "[cm text=quoted][endif]"
        "[ignore exp=\"flag == 1 || (flag == 2 && false)\"]"
        "[cm text=visible][endignore]"
        "[if exp=\"&f.menuEnabled && flag != 0\"]"
        "[cm text=external][endif]"
        "[end]",
        "compound-condition.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string value) {
      shown.push_back(std::move(value));
    };
    cb.getVar = [](std::string name) {
      return name == "f.menuEnabled" ? std::string("1") : std::string();
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    for (int i = 0; i < 4; ++i)
      runner.resumeWait();
    if (shown != std::vector<std::string>({
                     "compound", "quoted", "visible", "external"}) ||
        !runner.isFinished()) {
      std::printf("FAIL: compound KAG boolean predicate precedence\n");
      ++failures;
    }
    bool rejected = false;
    try {
      Kirikiri::KagScript::Runner malformed(
          Kirikiri::KagScript::parse(
              "[if exp=\"(flag == 1 &&\"][cm text=bad][endif][end]",
              "bad-predicate.ks"), cb);
      malformed.run();
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    if (!rejected) {
      std::printf("FAIL: malformed KAG boolean predicate was accepted\n");
      ++failures;
    }
  }

  // In-script [call target=*label] and [return] must preserve continuation
  // after a blocked text command. A saved PC cannot inherit stale frames.
  {
    auto script = Kirikiri::KagScript::parse("[jump target=*start]\n"
                                             "[cm text=\"skipped\"]\n"
                                             "*sub\n"
                                             "[cm text=\"inside\"]\n"
                                             "[return]\n"
                                             "*start\n"
                                             "[call target=*sub]\n"
                                             "[cm text=\"after\"]\n"
                                             "[end]\n",
                                             "calls.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string value) {
      shown.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (shown != std::vector<std::string>{"inside"} ||
        runner.callDepth() != 1 || !runner.isBlocked()) {
      std::printf("FAIL: KAG local subroutine did not enter correctly\n");
      ++failures;
    }
    // A save taken inside the subroutine must resume the pending return.
    const auto savedInsideCall = runner.saveState();
    std::vector<std::string> resumed;
    Kirikiri::KagScript::Callbacks restoredCallbacks;
    restoredCallbacks.showText = [&](std::string, std::string value) {
      resumed.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner reloaded(script, restoredCallbacks);
    reloaded.restoreState(savedInsideCall);
    reloaded.run();
    if (resumed != std::vector<std::string>{"after"} ||
        reloaded.callDepth() != 0 || !reloaded.isBlocked()) {
      std::printf("FAIL: KAG saved subroutine does not return correctly\n");
      ++failures;
    }
    const auto stateBeforeInvalidRestore = reloaded.saveState();
    auto invalid = savedInsideCall;
    invalid.returnStack.push_back(script.tags.size() + 1);
    bool rejectedInvalidState = false;
    try {
      reloaded.restoreState(invalid);
    } catch (const std::out_of_range &) {
      rejectedInvalidState = true;
    }
    if (!rejectedInvalidState ||
        reloaded.saveState().position != stateBeforeInvalidRestore.position ||
        reloaded.callDepth() != stateBeforeInvalidRestore.returnStack.size()) {
      std::printf("FAIL: KAG invalid restore changed VM state\n");
      ++failures;
    }
    runner.resumeWait(); // advance from the blocking dialogue
    if (shown != std::vector<std::string>({"inside", "after"}) ||
        runner.callDepth() != 0 || !runner.isBlocked()) {
      std::printf("FAIL: KAG return did not resume after call\n");
      ++failures;
    }
    runner.resumeWait();
    if (!runner.isFinished()) {
      std::printf("FAIL: KAG end after returned call\n");
      ++failures;
    }
    Kirikiri::KagScript::Runner restored(script, cb);
    restored.jumpToLabel("*start");
    restored.run();
    if (restored.callDepth() != 1) {
      std::printf("FAIL: KAG starred jump target\n");
      ++failures;
    }
    restored.restorePosition(0);
    if (restored.callDepth() != 0) {
      std::printf("FAIL: KAG restored position retains stale call stack\n");
      ++failures;
    }
  }

  // KAG flags/branches and nested conditionals must follow the selected
  // control path, and local variables survive a save within that path.
  {
    auto script = Kirikiri::KagScript::parse("[set name=flag value=1]\n"
                                             "[if exp=\"flag == 1\"]\n"
                                             "  [if exp=\"flag == 0\"]\n"
                                             "    [cm text=\"wrong-inner\"]\n"
                                             "  [else]\n"
                                             "    [cm text=\"inner\"]\n"
                                             "  [endif]\n"
                                             "[else]\n"
                                             "  [cm text=\"wrong-outer\"]\n"
                                             "[endif]\n"
                                             "[if exp=\"flag != 1\"]\n"
                                             "  [cm text=\"wrong-elsif\"]\n"
                                             "[elsif exp=\"flag >= 1\"]\n"
                                             "  [cm text=\"elseif\"]\n"
                                             "[else]\n"
                                             "  [cm text=\"wrong-else\"]\n"
                                             "[endif]\n"
                                             "[end]\n",
                                             "conditionals.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string value) {
      shown.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (shown != std::vector<std::string>{"inner"} ||
        runner.saveState().variables.at("flag") != "1") {
      std::printf("FAIL: nested KAG if/else or variables\n");
      ++failures;
    }
    const auto saved = runner.saveState();
    runner.resumeWait();
    if (shown != std::vector<std::string>({"inner", "elseif"})) {
      std::printf("FAIL: KAG elsif branch or outer else skipping\n");
      ++failures;
    }
    runner.resumeWait();
    if (!runner.isFinished()) {
      std::printf("FAIL: KAG conditions did not finish\n");
      ++failures;
    }
    std::vector<std::string> replay;
    Kirikiri::KagScript::Callbacks resumeCallbacks;
    resumeCallbacks.showText = [&](std::string, std::string value) {
      replay.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner restored(script, std::move(resumeCallbacks));
    restored.restoreState(saved);
    restored.run();
    if (replay != std::vector<std::string>{"elseif"})
      std::printf("FAIL: KAG flags not restored with save\n"), ++failures;
  }
  {
    // No timer callback must not leave a headless engine permanently blocked.
    auto script = Kirikiri::KagScript::parse(
        "[wait time=10][set name=ready value=1][if exp=\"ready\"]"
        "[cm text=\"ready\"][endif][end]",
        "no-timer.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string value) {
      shown.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (shown != std::vector<std::string>{"ready"}) {
      std::printf("FAIL: KAG no-timer fallback or variable truthiness\n");
      ++failures;
    }
  }

  // Only the first matching elsif branch may run. A false if must resume at
  // the candidate tag so the candidate's own expression gets evaluated.
  {
    auto script = Kirikiri::KagScript::parse(
        "[set name=flag value=2]"
        "[if exp=\"flag == 1\"][cm text=\"wrong-if\"]"
        "[elsif exp=\"flag == 0\"][cm text=\"wrong-first-elsif\"]"
        "[elsif exp=\"flag == 2\"][cm text=\"matched\"]"
        "[else][cm text=\"wrong-else\"][endif]"
        "[cm text=\"after\"][end]",
        "elsif.ks");
    std::vector<std::string> shown;
    Kirikiri::KagScript::Callbacks cb;
    cb.showText = [&](std::string, std::string value) {
      shown.push_back(std::move(value));
    };
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (shown != std::vector<std::string>{"matched"} ||
        runner.saveState().ifBranches != std::vector<std::uint8_t>{1}) {
      std::printf("FAIL: KAG elsif selection\n");
      ++failures;
    }
    const auto state = runner.saveState();
    Kirikiri::KagScript::Runner reloaded(script, cb);
    reloaded.restoreState(state);
    reloaded.run();
    if (shown != std::vector<std::string>({"matched", "after"}) ||
        !reloaded.saveState().ifBranches.empty()) {
      std::printf("FAIL: KAG save within conditional did not skip else\n");
      ++failures;
    }
  }
  {
    // An unconditional self-jump yields to the host at the instruction
    // budget instead of blocking the window or growing the call stack.
    auto script =
        Kirikiri::KagScript::parse("*loop\n[jump target=*loop]", "budget.ks");
    Kirikiri::KagScript::Callbacks cb;
    Kirikiri::KagScript::Runner runner(script, cb);
    runner.run();
    if (runner.isBlocked() || runner.isFinished())
      std::printf("FAIL: KAG infinite loop budget\n"), ++failures;
  }
  {
    auto script = Kirikiri::KagScript::parse(
        "[if exp=false][cm text=\"wrong\"][endif][end]", "invalid-state.ks");
    Kirikiri::KagScript::Callbacks cb;
    Kirikiri::KagScript::Runner runner(script, cb);
    auto invalid = runner.saveState();
    invalid.ifBranches.push_back(2);
    bool rejected = false;
    try {
      runner.restoreState(invalid);
    } catch (const std::out_of_range &) {
      rejected = true;
    }
    if (!rejected || !runner.saveState().ifBranches.empty())
      std::printf("FAIL: KAG invalid conditional state accepted\n"), ++failures;
  }

  // KAG original title menus use [glink] and inline [link] anchors
  // followed by [s]. They must expose actionable choices rather than
  // completing the VM without user input.
  {
    const auto script = Kirikiri::KagScript::parse(
        "*menu\n"
        "[glink text=Play target=*play]"
        "[link target=*config]Settings[endlink]"
        "[s]\n"
        "*play\n[cm text=Playing][end]\n"
        "*config\n[cm text=Settings-open][end]",
        "menu-links.ks");
    for (int selected = 0; selected < 2; ++selected) {
      std::vector<std::string> shown, options;
      std::function<void(int)> choose;
      Kirikiri::KagScript::Callbacks cb;
      cb.showText = [&](std::string, std::string text) {
        shown.push_back(std::move(text));
      };
      cb.showChoice = [&](std::string, std::vector<std::string> displayed,
                          std::function<void(int)> callback) {
        options = std::move(displayed);
        choose = std::move(callback);
      };
      Kirikiri::KagScript::Runner runner(script, cb);
      runner.run();
      if (options != std::vector<std::string>({"Play", "Settings"}) ||
          !runner.isBlocked() || !choose) {
        std::printf("FAIL: KAG glink/link menu not presented\n");
        ++failures;
        continue;
      }
      choose(selected);
      const std::string expected = selected == 0 ? "Playing" : "Settings-open";
      if (shown != std::vector<std::string>{expected} || !runner.isBlocked()) {
        std::printf("FAIL: KAG glink/link menu selection target\n");
        ++failures;
      }
      runner.resumeWait();
      if (!runner.isFinished()) {
        std::printf("FAIL: KAG glink/link menu failed to finish\n");
        ++failures;
      }
    }
    bool rejected = false;
    try {
      Kirikiri::KagScript::Runner invalid(
          Kirikiri::KagScript::parse("[endlink][end]", "orphan-link.ks"), {});
      invalid.run();
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    if (!rejected) {
      std::printf("FAIL: orphan [endlink] was accepted\n");
      ++failures;
    }
  }

  // Numbered KAG choices must render in numeric order. std::map's
  // lexical order would wrongly place option10 before option2.
  {
    auto script = Kirikiri::KagScript::parse(
        "[choices question=Pick option10=ten option2=two option1=one "
        "btn3=three optionColor=red][end]",
        "choice-order.ks");
    std::vector<std::string> shown;
    std::function<void(int)> select;
    Kirikiri::KagScript::Callbacks callbacks;
    callbacks.showChoice = [&](std::string, std::vector<std::string> options,
                               std::function<void(int)> onChoose) {
      shown = std::move(options);
      select = std::move(onChoose);
    };
    Kirikiri::KagScript::Runner runner(std::move(script), callbacks);
    runner.run();
    if (!runner.isBlocked() ||
        shown != std::vector<std::string>({"one", "two", "three", "ten"}) ||
        !select) {
      std::printf("FAIL: numeric KAG choice ordering or filtering\n");
      ++failures;
    } else {
      select(1);
      if (!runner.isFinished()) {
        std::printf("FAIL: KAG choice callback did not resume script\n");
        ++failures;
      }
    }
  }

  // KiriKiri's canonical XP3 index is a 1-byte flag followed by one size
  // for a raw index (two for compressed). Chained indexes have a 64-bit
  // pointer immediately after their payload. One storage may have several
  // physical, independently compressed segments.
  {
    using Segment =
        std::array<std::uint64_t, 4>; // method, offset, orig, stored
    const auto patch64 = [](std::vector<std::uint8_t> &bytes,
                            std::size_t offset, std::uint64_t value) {
      for (std::size_t i = 0; i < 8; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const auto makeRecord = [&](const std::string &name, std::uint64_t fileSize,
                                const std::vector<Segment> &segments) {
      std::vector<std::uint8_t> record;
      const auto tag = [&](const char *value) {
        record.insert(record.end(), value, value + 4);
      };
      tag("File");
      putLE(record, std::uint64_t(0));
      tag("info");
      putLE(record, std::uint64_t(22 + name.size() * 2));
      putLE(record, std::uint32_t(0));
      putLE(record, fileSize);
      std::uint64_t stored = 0;
      for (const auto &segment : segments)
        stored += segment[3];
      putLE(record, stored);
      putLE(record, std::uint16_t(name.size()));
      for (unsigned char ch : name)
        putLE(record, std::uint16_t(ch));
      tag("segm");
      putLE(record, std::uint64_t(28 * segments.size()));
      for (const auto &segment : segments) {
        putLE(record, std::uint32_t(segment[0]));
        putLE(record, segment[1]);
        putLE(record, segment[2]);
        putLE(record, segment[3]);
      }
      tag("adlr");
      putLE(record, std::uint64_t(4));
      putLE(record, std::uint32_t(0));
      patch64(record, 4, record.size() - 12);
      return record;
    };
    const auto compress = [](const std::vector<std::uint8_t> &input) {
      uLongf size = compressBound(static_cast<uLong>(input.size()));
      std::vector<std::uint8_t> packed(size);
      if (compress2(packed.data(), &size, input.data(),
                    static_cast<uLong>(input.size()), Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("Cannot compress XP3 test fixture");
      packed.resize(size);
      return packed;
    };
    const std::string secondName = "scenario/second.ks";
    const std::string secondText =
        "[bg storage=room]\n[image layer=0 storage=cat.png]\n[end]\n";
    const auto cut = secondText.size() / 2;
    const std::vector<std::uint8_t> remaining(secondText.begin() + cut,
                                              secondText.end());
    const auto packedRemaining = compress(remaining);
    std::vector<std::uint8_t> standard(magic, magic + 11);
    putLE(standard, std::uint64_t(0)); // patched canonical index pointer
    const auto firstOffset = static_cast<std::uint64_t>(standard.size());
    standard.insert(standard.end(), fileContent.begin(), fileContent.end());
    const auto rawSecondOffset = static_cast<std::uint64_t>(standard.size());
    standard.insert(standard.end(), secondText.begin(),
                    secondText.begin() + cut);
    const auto compressedSecondOffset =
        static_cast<std::uint64_t>(standard.size());
    standard.insert(standard.end(), packedRemaining.begin(),
                    packedRemaining.end());

    const auto firstIndex =
        makeRecord(fileName, fileContent.size(),
                   {{0, firstOffset, fileContent.size(), fileContent.size()}});
    const auto secondIndex =
        makeRecord(secondName, secondText.size(),
                   {{0, rawSecondOffset, cut, cut},
                    {1, compressedSecondOffset, remaining.size(),
                     packedRemaining.size()}});
    const auto compressedIndex = compress(secondIndex);
    const auto indexOffset = static_cast<std::uint64_t>(standard.size());
    const auto nextIndex = indexOffset + 9 + firstIndex.size() + 8;
    patch64(standard, 11, indexOffset);
    standard.push_back(0x80); // canonical raw index with continuation
    putLE(standard, std::uint64_t(firstIndex.size()));
    standard.insert(standard.end(), firstIndex.begin(), firstIndex.end());
    const auto continuationPointer = standard.size();
    putLE(standard, nextIndex);
    standard.push_back(1); // canonical compressed index
    putLE(standard, std::uint64_t(compressedIndex.size()));
    putLE(standard, std::uint64_t(secondIndex.size()));
    standard.insert(standard.end(), compressedIndex.begin(),
                    compressedIndex.end());

    const auto writeArchive = [&](const char *name,
                                  const std::vector<std::uint8_t> &bytes) {
      const auto path = tempDir / name;
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char *>(bytes.data()),
                std::streamsize(bytes.size()));
    };
    writeArchive("standard-chained.xp3", standard);
    try {
      assets.mountXp3("standard-chained.xp3");
      auto first = assets.readAsset(fileName);
      auto second = assets.readAsset(secondName);
      if (std::string(first.begin(), first.end()) != fileContent ||
          std::string(second.begin(), second.end()) != secondText ||
          !assets.containsAsset(secondName)) {
        std::printf(
            "FAIL: canonical XP3 index chain/segments returned bad data\n");
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: canonical XP3 index chain/segments: %s\n",
                  error.what());
      ++failures;
    }
    // Updates to a path in the second index take precedence over the
    // first index (without replacing unrelated storages).
    auto replacedIndex = secondIndex;
    auto laterRecord = makeRecord(fileName, secondText.size(),
                                  {{0, rawSecondOffset, cut, cut},
                                   {1, compressedSecondOffset, remaining.size(),
                                    packedRemaining.size()}});
    replacedIndex.insert(replacedIndex.end(), laterRecord.begin(),
                         laterRecord.end());
    const auto compressedReplacement = compress(replacedIndex);
    auto replacementArchive = standard;
    replacementArchive.resize(static_cast<std::size_t>(nextIndex));
    replacementArchive.push_back(1);
    putLE(replacementArchive, std::uint64_t(compressedReplacement.size()));
    putLE(replacementArchive, std::uint64_t(replacedIndex.size()));
    replacementArchive.insert(replacementArchive.end(),
                              compressedReplacement.begin(),
                              compressedReplacement.end());
    writeArchive("index-update.xp3", replacementArchive);
    try {
      assets.mountXp3("index-update.xp3");
      const auto changed = assets.readAsset(fileName);
      if (std::string(changed.begin(), changed.end()) != secondText) {
        std::printf(
            "FAIL: later XP3 index entry did not override earlier path\n");
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: later XP3 index entry: %s\n", error.what());
      ++failures;
    }
    // Segment offsets must point inside the physical archive.
    auto invalidSegment = standard;
    const auto firstSegOffset = static_cast<std::size_t>(indexOffset) + 9u +
                                12u + 12u + 22u + fileName.size() * 2u + 12u +
                                4u;
    patch64(invalidSegment, firstSegOffset, standard.size() + 1024);
    writeArchive("bad-segment.xp3", invalidSegment);
    bool rejectedSegment = false;
    try {
      assets.mountXp3("bad-segment.xp3");
    } catch (const std::runtime_error &) {
      rejectedSegment = true;
    }
    if (!rejectedSegment) {
      std::printf("FAIL: out-of-bounds XP3 segment was accepted\n");
      ++failures;
    }

    auto cycle = standard;
    patch64(cycle, continuationPointer, indexOffset);
    writeArchive("cyclic.xp3", cycle);
    bool rejectedCycle = false;
    try {
      assets.mountXp3("cyclic.xp3");
    } catch (const std::runtime_error &) {
      rejectedCycle = true;
    }
    if (!rejectedCycle) {
      std::printf("FAIL: cyclic XP3 index chain was accepted\n");
      ++failures;
    }
    auto oversized = standard;
    patch64(oversized, indexOffset + 1, 64ull * 1024 * 1024 + 1);
    writeArchive("huge-index.xp3", oversized);
    bool rejectedSize = false;
    try {
      assets.mountXp3("huge-index.xp3");
    } catch (const std::runtime_error &) {
      rejectedSize = true;
    }
    if (!rejectedSize) {
      std::printf("FAIL: oversized XP3 index was accepted\n");
      ++failures;
    }
  }

  // Decode canonical TLG5 images without relying on FFmpeg or proprietary
  // assets. A single pixel exercises blue/green residuals and RGB ordering.
  {
    std::vector<std::uint8_t> tlg5{'T', 'L', 'G', '5', '.', '0',
                                   0,   'r', 'a', 'w', 0x1a};
    tlg5.push_back(3);
    putLE(tlg5, std::uint32_t(1)); // width
    putLE(tlg5, std::uint32_t(1)); // height
    putLE(tlg5, std::uint32_t(1)); // block height
    putLE(tlg5, std::uint32_t(0)); // one ignored block-size hint
    const auto rawPlane = [&](std::vector<std::uint8_t> &image,
                              std::uint8_t value) {
      image.push_back(1); // raw plane
      putLE(image, std::uint32_t(1));
      image.push_back(value);
    };
    rawPlane(tlg5, 5);  // blue difference
    rawPlane(tlg5, 10); // green difference
    rawPlane(tlg5, 20); // red difference
    const auto checkColor = [&](const std::vector<std::uint8_t> &image,
                                std::uint8_t alpha) {
      const auto surface = Kotonoha::Providers::decodeTlgImage(image);
      if (!surface || surface->w != 1 || surface->h != 1)
        return false;
      const auto *pixel = static_cast<const std::uint8_t *>(surface->pixels);
      return pixel[0] == 30 && pixel[1] == 10 && pixel[2] == 15 &&
             pixel[3] == alpha;
    };
    try {
      if (!checkColor(tlg5, 255)) {
        std::printf("FAIL: TLG5 raw RGB pixel decoding\n");
        ++failures;
      }
      std::vector<std::uint8_t> compressed = tlg5;
      // Replace the first raw plane (1 + size4 + data1) with a literal-only
      // slide/LZSS stream, then retain the green/red raw planes.
      const std::size_t planeOffset = 11 + 1 + 4 * 4;
      compressed.erase(compressed.begin() + planeOffset,
                       compressed.begin() + planeOffset + 6);
      const std::vector<std::uint8_t> encodedBlue{
          0, 2, 0, 0, 0, 0, 5}; // flag 0, stored size 2, literal flag+5
      compressed.insert(compressed.begin() + planeOffset, encodedBlue.begin(),
                        encodedBlue.end());
      if (!checkColor(compressed, 255)) {
        std::printf("FAIL: TLG5 slide-compressed RGB pixel decoding\n");
        ++failures;
      }
      auto alphaImage = tlg5;
      alphaImage[11] = 4; // RGBA
      rawPlane(alphaImage, 64);
      if (!checkColor(alphaImage, 64)) {
        std::printf("FAIL: TLG5 raw alpha channel decoding\n");
        ++failures;
      }
      std::vector<std::uint8_t> wrapped{'T', 'L', 'G', '0', '.', '0',
                                        0,   's', 'd', 's', 0x1a};
      putLE(wrapped, std::uint32_t(tlg5.size()));
      wrapped.insert(wrapped.end(), tlg5.begin(), tlg5.end());
      if (!checkColor(wrapped, 255)) {
        std::printf("FAIL: TLG0 structured wrapper\n");
        ++failures;
      }
      // Two scanlines in separate blocks exercise vertical prediction,
      // while two pixels per row exercise horizontal delta composition.
      std::vector<std::uint8_t> tiled{'T', 'L', 'G', '5', '.', '0',
                                      0,   'r', 'a', 'w', 0x1a};
      tiled.push_back(3);
      putLE(tiled, std::uint32_t(2)); // width
      putLE(tiled, std::uint32_t(2)); // height
      putLE(tiled, std::uint32_t(1)); // separate scanline blocks
      putLE(tiled, std::uint32_t(0));
      putLE(tiled, std::uint32_t(0));
      const auto rawRow = [&](std::uint8_t first, std::uint8_t second) {
        tiled.push_back(1);
        putLE(tiled, std::uint32_t(2));
        tiled.push_back(first);
        tiled.push_back(second);
      };
      rawRow(5, 0);  // first row blue
      rawRow(10, 0); // green
      rawRow(20, 0); // red
      rawRow(0, 0);  // second row reuses previous row
      rawRow(0, 0);
      rawRow(0, 0);
      auto tiledSurface = Kotonoha::Providers::decodeTlgImage(tiled);
      bool tiledOK =
          tiledSurface && tiledSurface->w == 2 && tiledSurface->h == 2;
      if (tiledOK) {
        const auto *pixel =
            static_cast<const std::uint8_t *>(tiledSurface->pixels);
        for (unsigned y = 0; y < 2; ++y)
          for (unsigned x = 0; x < 2; ++x) {
            const auto *p = pixel + y * tiledSurface->pitch + x * 4;
            if (p[0] != 30 || p[1] != 10 || p[2] != 15 || p[3] != 255)
              tiledOK = false;
          }
      }
      if (!tiledOK) {
        std::printf("FAIL: TLG5 horizontal/vertical predictor\n");
        ++failures;
      }
      std::vector<std::uint8_t> unsupported{'T', 'L', 'G', '6', '.', '0',
                                            0,   'r', 'a', 'w', 0x1a};
      bool rejectedTlg6 = false;
      try {
        (void)Kotonoha::Providers::decodeTlgImage(unsupported);
      } catch (const std::runtime_error &) {
        rejectedTlg6 = true;
      }
      if (!rejectedTlg6) {
        std::printf("FAIL: unsupported TLG6 was accepted as a TLG5 image\n");
        ++failures;
      }

      auto truncated = tlg5;
      truncated.pop_back();
      bool rejected = false;
      try {
        (void)Kotonoha::Providers::decodeTlgImage(truncated);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: truncated TLG5 image was accepted\n");
        ++failures;
      }

      const auto imageFolder = tempDir / "image";
      std::filesystem::create_directories(imageFolder);
      const auto filePath = imageFolder / "tlg5.tlg";
      std::ofstream file(filePath, std::ios::binary);
      file.write(reinterpret_cast<const char *>(tlg5.data()),
                 std::streamsize(tlg5.size()));
      file.close();
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      const auto surface = provider.loadSurface("image/tlg5.tlg");
      const auto *pixel =
          surface ? static_cast<const std::uint8_t *>(surface->pixels)
                  : nullptr;
      if (!pixel || pixel[0] != 30 || pixel[1] != 10 || pixel[2] != 15 ||
          pixel[3] != 255) {
        std::printf("FAIL: Kirikiri provider did not route TLG5 decoding\n");
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: TLG5 image compatibility: %s\n", error.what());
      ++failures;
    }
  }

  // A small, real TJS2 source subset now executes variable expressions,
  // conditional branches and script calls. It must not claim native UI or
  // load plugins while inspecting third-party game data.
  {
    const auto bootDir = tempDir / "boot";
    std::filesystem::create_directories(bootDir);
    const auto write = [](const std::filesystem::path &path,
                          const std::string &text) {
      std::ofstream out(path, std::ios::binary);
      out << text;
    };
    write(bootDir / "next.tjs", "var scenario = \"scenario/\" + \"first.ks\";\n"
                                "Scripts.execStorage(scenario);\n");
    write(tempDir / "startup.tjs",
          "// Scripts.execStorage(\"comment-not-real.tjs\");\n"
          "var fake = 'Plugins.link(\"string-not-real.dll\")';\n"
          "var count = 0;\n"
          "var target = 0x2;\n"
          "while (count < target) { count++; }\n"
          "if (count == target && "
          "Storages.isExistentStorage(\"boot/next.tjs\")) {\n"
          "  Scripts.execStorage(\"boot/next.tjs\");\n"
          "} else { Scripts.execStorage(\"never.tjs\"); }\n"
          "System.title = \"TJS2\" + \" demo\";\n"
          "Plugins.link(\"plugins/unused.dll\");\n");
    try {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.kagEntry != "scenario/first.ks" || run.title != "TJS2 demo" ||
          !run.requiresNativeRuntime ||
          run.visitedScripts !=
              std::vector<std::string>{"startup.tjs", "boot/next.tjs"} ||
          run.requestedPlugins !=
              std::vector<std::string>{"plugins/unused.dll"} ||
          !run.missing.empty()) {
        std::printf("FAIL: TJS2 source subset bootstrap flow\n");
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: TJS2 source subset threw: %s\n", error.what());
      ++failures;
    }

    // User functions preserve parameter/local scopes, short-circuit returns
    // and drive the KAG startup path without a hardcoded alphabetical entry.
    write(tempDir / "startup.tjs",
          "var title = \"Global\";\n"
          "function choose(base, leaf) {\n"
          "  var title = \"Local\";\n"
          "  if (leaf == \"first\") return base + leaf + \".ks\";\n"
          "  return \"missing.ks\";\n"
          "}\n"
          "function doubled(n) { return n + n; }\n"
          "var route = choose(\"scenario/\", \"first\");\n"
          "if (doubled(2) == 4) Scripts.execStorage(route);\n"
          "System.title = title;\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.kagEntry != "scenario/first.ks" || run.title != "Global" ||
          !run.requiresNativeRuntime) {
        std::printf("FAIL: TJS2 function return/local scope/KAG entry\n");
        ++failures;
      }
    }
    // A TJS Dictionary has shared identity and nested properties; function
    // arguments may modify configuration used by the title/KAG launcher.
    write(tempDir / "startup.tjs",
          "var config = new Dictionary();\n"
          "config.menu = new Dictionary;\n"
          "config.menu.caption = \"Initial\";\n"
          "function prepare(menu) { menu.caption = \"Configured\"; "
          "return menu.caption; }\n"
          "var caption = prepare(config.menu);\n"
          "if (caption == \"Configured\" && config.menu.caption == caption) "
          "  Scripts.execStorage(\"scenario/first.ks\");\n"
          "System.title = config.menu.caption;\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.kagEntry != "scenario/first.ks" || run.title != "Configured") {
        std::printf("FAIL: TJS2 Dictionary objects/member updates\n");
        ++failures;
      }
    }
    // Disabled native branches must not attempt class construction.
    write(tempDir / "startup.tjs",
          "if (false) { var unused = new Window(this); }\n"
          "var options = new Dictionary();\n"
          "options.caption = \"Safe\";\n"
          "System.title = options.caption;\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || run.title != "Safe") {
        std::printf("FAIL: inactive native constructor was executed\n");
        ++failures;
      }
    }
    // Script-level Dictionary objects are not native Window/Layer classes.
    write(tempDir / "startup.tjs", "var native = new UnsupportedNative();\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted ||
          run.error.find("native class not implemented") == std::string::npos) {
        std::printf("FAIL: unsupported native TJS2 object was accepted\n");
        ++failures;
      }
    }

    // Common TJS2 bootstrap for-loops run with bounded post-increment and
    // scoped variables; a disabled for-loop has no observable side effects.
    write(tempDir / "startup.tjs",
          "var sum = 0;\n"
          "for (var i = 0; i < 3; i++) { sum += 1; }\n"
          "if (false) for (var j = 0; j < 2; j++) { sum += 99; }\n"
          "if (sum == 3) Scripts.execStorage(\"scenario/first.ks\");\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.kagEntry != "scenario/first.ks") {
        std::printf("FAIL: TJS2 bounded for loop/inactive branch\n");
        ++failures;
      }
    }
    write(tempDir / "startup.tjs", "for (;;) { var tick = 1; }");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input, 10000);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf("FAIL: unbounded TJS2 for-loop not stopped\n");
        ++failures;
      }
    }

    // Mutual/nested recursion must terminate even with a high instruction
    // budget, independent of the script-call depth limiter.
    write(tempDir / "startup.tjs", "function again() { return again(); }\n"
                                   "again();\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input, 10000);
      if (run.subsetCompleted ||
          run.error.find("recursion limit") == std::string::npos) {
        std::printf("FAIL: TJS2 function recursion cap\n");
        ++failures;
      }
    }
    write(tempDir / "startup.tjs",
          "if (false) { function hidden() { return 1; } }\n"
          "hidden();\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf("FAIL: inactive TJS2 function was invoked\n");
        ++failures;
      }
    }
    write(tempDir / "startup.tjs", "return 4;\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted ||
          run.error.find("return outside function") == std::string::npos) {
        std::printf("FAIL: top-level TJS2 return silently accepted\n");
        ++failures;
      }
    }

    write(tempDir / "startup.tjs", "Scripts.execStorage(\"startup.tjs\");");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted ||
          run.error.find("cyclic") == std::string::npos) {
        std::printf("FAIL: cyclic TJS2 storage execution not rejected\n");
        ++failures;
      }
    }
    write(tempDir / "startup.tjs", "while (true) { var x = 1; }");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input, 40);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf("FAIL: unbounded TJS2 loop was not stopped\n");
        ++failures;
      }
    }
    write(tempDir / "startup.tjs", "class GameWindow { property title; }");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf(
            "FAIL: unsupported TJS2 class property accepted silently\n");
        ++failures;
      }
    }
    // Unicode identifiers from a BOM-marked UTF-8 TJS2 source are handled
    // without case-folding or corrupting the multibyte storage names.
    write(tempDir / "startup.tjs",
          std::string("\xef\xbb\xbf") +
              u8"var 猫 = 2; if (猫 == 2) "
              u8"{ Scripts.execStorage(\"scenario/first.ks\"); }");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || run.kagEntry != "scenario/first.ks") {
        std::printf("FAIL: UTF-8 TJS2 identifiers not evaluated\n");
        ++failures;
      }
    }
    std::filesystem::remove(tempDir / "startup.tjs");
  }

  // The upstream KiriKiri decoder reserves entropy ID 3 for uncompressed
  // samples but does not implement it. Accept only exactly 8 bits per pixel;
  // Gamma (ID 1) and experimental LZSS (ID 2) remain rejected.
  {
    std::vector<std::uint8_t> tlg{'T', 'L', 'G',  '6', '.', '0', 0, 'r',
                                  'a', 'w', 0x1a, 3,   0,   0,   0};
    putLE(tlg, std::uint32_t(1)); // width
    putLE(tlg, std::uint32_t(1)); // height
    putLE(tlg, std::uint32_t(8)); // max bits
    putLE(tlg, std::uint32_t(2)); // LZSS filter bytes
    tlg.push_back(0);
    tlg.push_back(0); // literal filter 0 = MED
    const auto infoAt = tlg.size();
    putLE(tlg, std::uint32_t(0xc0000008)); // raw 8 bits / pixel
    tlg.push_back(5);                      // blue residual
    for (int i = 0; i < 2; ++i) {
      putLE(tlg, std::uint32_t(2)); // Golomb, zero run of 1
      tlg.push_back(2);
    }
    try {
      auto image = Kotonoha::Providers::decodeTlgImage(tlg);
      const auto *p = static_cast<const std::uint8_t *>(image->pixels);
      if (image->w != 1 || image->h != 1 || p[0] != 0 || p[1] != 0 ||
          p[2] != 5 || p[3] != 255) {
        std::printf("FAIL: TLG6 raw residual decode\n");
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: TLG6 raw residual decode: %s\n", error.what());
      ++failures;
    }
    const auto patchInfo = [](std::vector<std::uint8_t> &image,
                              std::size_t index, std::uint32_t value) {
      for (unsigned i = 0; i < 4; ++i)
        image[index + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    for (const auto method : {1u, 2u, 3u}) {
      auto invalid = tlg;
      const auto bits = method == 3 ? 7u : 8u;
      patchInfo(invalid, infoAt, (method << 30) | bits);
      bool rejected = false;
      try {
        (void)Kotonoha::Providers::decodeTlgImage(invalid);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected) {
        std::printf("FAIL: TLG6 rejected entropy/invalid size accepted\n");
        ++failures;
      }
    }
  }

  // Event handlers must have an independent instruction budget. A title
  // remains interactive after many hover/movement updates.
  {
    Kotonoha::Providers::KirikiriAssets input(tempDir);
    Kotonoha::Providers::TjsStartupResult menu;
    const bool ok = Kotonoha::Providers::executeTjsInlineSubset(
        menu, input,
        "var w = new Window(); var b = new Layer(w, w.primaryLayer);"
        "b.setSize(100,100);"
        "function tick(x) { System.title = \"Alive\"; }"
        "b.onMouseMove = tick;",
        "menu-budget.tjs", 24);
    if (!ok || !menu.error.empty()) {
      std::printf("FAIL: event-budget menu fixture\n");
      ++failures;
    } else {
      bool healthy = true;
      for (int n = 0; n < 100; ++n) {
        if (!Kotonoha::Providers::dispatchTjsUiMouseMove(
                menu, 12.0f, 12.0f) ||
            !menu.error.empty()) {
          healthy = false;
          break;
        }
      }
      if (!healthy || menu.title != "Alive") {
        std::printf("FAIL: TJS2 repeated UI events exhausted startup budget\n");
        ++failures;
      }
    }
  }

  // KiriKiri menu rollover scripts can update state from onMouseMove.
  {
    Kotonoha::Providers::KirikiriAssets input(tempDir);
    Kotonoha::Providers::TjsStartupResult menu;
    const std::string source =
        "var window = new Window();"
        "var layer = new Layer(window, window.primaryLayer);"
        "layer.setPos(20, 20); layer.setSize(120, 60);"
        "function moved(x, y) { System.title = \"Hovered\"; }"
        "layer.onMouseMove = moved;";
    const bool parsed = Kotonoha::Providers::executeTjsInlineSubset(
        menu, input, source, "menu-move.tjs");
    if (!parsed || !menu.error.empty() ||
        Kotonoha::Providers::dispatchTjsUiMouseMove(menu, -1.0f, -1.0f) ||
        !Kotonoha::Providers::dispatchTjsUiMouseMove(menu, 44.0f, 38.0f) ||
        menu.title != "Hovered") {
      std::printf("FAIL: TJS2 onMouseMove original menu rollover\n");
      ++failures;
    }
  }

  // A native Window.onKeyDown can control an original text-layer title
  // menu with KiriKiri virtual key codes (Enter=13, Escape=27).
  {
    Kotonoha::Providers::KirikiriAssets input(tempDir);
    Kotonoha::Providers::TjsStartupResult menu;
    const std::string source =
        "var win = new Window();"
        "var button = new Layer(win, win.primaryLayer);"
        "button.setPos(50, 50); button.setSize(120, 40);"
        "button.drawText(0, 0, \"Start\");"
        "function selectKey(code, modifiers) {"
        "  if (code == 13) { System.title = \"Keyboard\"; }"
        "}"
        "win.onKeyDown = selectKey;";
    const bool parsed = Kotonoha::Providers::executeTjsInlineSubset(
        menu, input, source, "menu-keyboard.tjs");
    if (!parsed || !menu.error.empty() ||
        menu.onKeyDown != "selectKey" ||
        Kotonoha::Providers::dispatchTjsUiKeyDown(menu, 999) ||
        !Kotonoha::Providers::dispatchTjsUiKeyDown(menu, 27) ||
        menu.title == "Keyboard" ||
        !Kotonoha::Providers::dispatchTjsUiKeyDown(menu, 13) ||
        menu.title != "Keyboard") {
      std::printf("FAIL: TJS2 Window.onKeyDown title navigation\n");
      ++failures;
    }
  }

  // A menu that activates on release instead of mouse-down must be
  // selectable, as used by KiriKiri's onMouseUp Layer events.
  {
    Kotonoha::Providers::KirikiriAssets input(tempDir);
    Kotonoha::Providers::TjsStartupResult menu;
    const std::string source =
        "var win = new Window();"
        "var button = new Layer(win, win.primaryLayer);"
        "button.setPos(20, 30); button.setSize(180, 70);"
        "button.drawText(4, 6, \"Load\");"
        "function released(x, y) { System.title = \"Released\"; }"
        "button.onMouseUp = released;";
    const bool ok = Kotonoha::Providers::executeTjsInlineSubset(
        menu, input, source, "menu-release.tjs");
    if (!ok || !menu.error.empty() || menu.layers.size() != 2 ||
        menu.layers.back().onMouseUp.empty() ||
        Kotonoha::Providers::dispatchTjsUiMouseUp(menu, 0.0f, 0.0f) ||
        !Kotonoha::Providers::dispatchTjsUiMouseUp(menu, 30.0f, 45.0f) ||
        menu.title != "Released") {
      std::printf("FAIL: TJS2 Layer onMouseUp menu navigation\n");
      ++failures;
    }
  }

  // Script-created text-only title menus now project Layer.drawText onto
  // Kotonoha's native UI renderer, with font.height and click callbacks.
  {
    Kotonoha::Providers::KirikiriAssets input(tempDir);
    Kotonoha::Providers::TjsStartupResult menu;
    const std::string source =
        "var win = new Window();"
        "var button = new Layer(win, win.primaryLayer);"
        "button.setPos(40, 80); button.setSize(160, 60);"
        "button.font.height = 32;"
        "button.fillRect(0, 0, 160, 60, 65280, 160);"
        "button.drawText(8, 5, \"Start\", 16711680, 220, true);"
        "function pressed() { System.title = \"Clicked\"; }"
        "button.onClick = pressed;";
    const bool ok = Kotonoha::Providers::executeTjsInlineSubset(
        menu, input, source, "title-menu.tjs");
    const auto it = std::find_if(
        menu.layers.begin(), menu.layers.end(),
        [](const Kotonoha::Providers::TjsUiLayer &layer) {
          return !layer.texts.empty();
        });
    if (!ok || !menu.error.empty() || it == menu.layers.end() ||
        it->texts.size() != 1 || it->texts[0].text != "Start" ||
        it->texts[0].left != 8 || it->texts[0].top != 5 ||
        it->texts[0].size != 32 || it->texts[0].opacity != 220 ||
        it->texts[0].rgb != 16711680 ||
        it->rectangles.size() != 1 ||
        it->rectangles[0].width != 160 ||
        it->rectangles[0].height != 60 ||
        it->rectangles[0].rgb != 65280 ||
        it->rectangles[0].opacity != 160 ||
        !Kotonoha::Providers::dispatchTjsUiClick(menu, 60.0f, 100.0f) ||
        menu.title != "Clicked") {
      std::printf("FAIL: TJS2 text-only title menu drawing and click\n");
      ++failures;
    }
  }

  // A synthetic Window and clickable Layer use the same TLG image provider
  // as the KAG renderer. The VM retains function state for click dispatch
  // without calling any platform-native Window/Layer constructor.
  {
    const auto write = [](const std::filesystem::path &path,
                          const std::string &content) {
      std::ofstream out(path, std::ios::binary);
      out << content;
    };
    write(tempDir / "startup.tjs",
          "var win = new Window();\n"
          "win.left = 10; win.top = 20;\n"
          "win.primaryLayer.setPos(3, 4);\n"
          "var logo = new Layer(win, win.primaryLayer);\n"
          "logo.setPos(20, 30); logo.setSize(60, 40);\n"
          "logo.loadImages(\"image/tlg5.tlg\");\n"
          "logo.opacity = 180;\n"
          "function chooseTitle() {\n"
          "  logo.visible = false;\n"
          "  Scripts.execStorage(\"scenario/first.ks\");\n"
          "}\n"
          "logo.onClick = chooseTitle;\n"
          "var cover = new Layer(win);\n"
          "cover.setPos(0, 0); cover.setSize(150, 150);\n"
          "cover.order = 100; cover.visible = false;\n"
          "cover.onClick = chooseTitle;\n");
    try {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      auto found =
          std::find_if(run.layers.begin(), run.layers.end(),
                       [](const Kotonoha::Providers::TjsUiLayer &layer) {
                         return layer.storage == "image/tlg5.tlg";
                       });
      if (!run.subsetCompleted || !run.error.empty() || !run.uiSession ||
          run.layers.size() != 3 || found == run.layers.end() ||
          found->left != 33 || found->top != 54 || found->width != 60 ||
          found->height != 40 || found->opacity != 180 ||
          found->onClick != "chooseTitle") {
        std::printf("FAIL: TJS2 Window/Layer hierarchy or image projection\n");
        ++failures;
      } else {
        if (Kotonoha::Providers::dispatchTjsUiClick(run, 5.0f, 5.0f) ||
            !Kotonoha::Providers::dispatchTjsUiClick(run, 40.0f, 65.0f) ||
            run.kagEntry != "scenario/first.ks" || !run.error.empty()) {
          std::printf("FAIL: TJS2 Layer onClick did not select KAG entry\n");
          ++failures;
        } else {
          auto after =
              std::find_if(run.layers.begin(), run.layers.end(),
                           [](const Kotonoha::Providers::TjsUiLayer &layer) {
                             return layer.storage == "image/tlg5.tlg";
                           });
          if (after == run.layers.end() || after->visible ||
              Kotonoha::Providers::dispatchTjsUiClick(run, 40.0f, 65.0f)) {
            std::printf("FAIL: TJS2 click handler did not retain visibility\n");
            ++failures;
          }
        }
      }
    } catch (const std::exception &error) {
      std::printf("FAIL: TJS2 Window/Layer synthetic bridge: %s\n",
                  error.what());
      ++failures;
    }
    // Real KiriKiri title scripts often assign an inline function expression
    // to an event property. Mouse coordinates and button data are forwarded
    // through the same retained, bounded source VM.
    write(tempDir / "startup.tjs",
          "var win = new Window();\n"
          "var button = new Layer(win);\n"
          "button.setPos(8, 10); button.setSize(80, 50);\n"
          "button.loadImages(\"image/tlg5.tlg\");\n"
          "button.onMouseDown = function(x, y, mb, shift) {\n"
          "  if (x >= 8 && y >= 10 && mb == 1 && shift == 0)\n"
          "    Scripts.execStorage(\"scenario/first.ks\");\n"
          "};\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      input.mountXp3("data.xp3");
      auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.layers.size() != 2 ||
          !Kotonoha::Providers::dispatchTjsUiClick(run, 20.0f, 30.0f) ||
          run.kagEntry != "scenario/first.ks" || !run.error.empty()) {
        std::printf("FAIL: anonymous TJS2 onMouseDown callback arguments\n");
        ++failures;
      }
    }
    // Hover state is retained across frames and only changes on
    // enter/leave; callbacks update script-owned Layer properties.
    write(tempDir / "startup.tjs",
          "var win = new Window();\n"
          "var button = new Layer(win);\n"
          "button.setPos(8, 10); button.setSize(80, 50);\n"
          "button.loadImages(\"image/tlg5.tlg\");\n"
          "button.onMouseEnter = function(x, y) {\n"
          "  if (x >= 8 && y >= 10) button.opacity = 140;\n"
          "};\n"
          "button.onMouseLeave = function(x, y) {\n"
          "  button.opacity = 255;\n"
          "};\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      const auto getOpacity = [&] {
        for (const auto &layer : run.layers)
          if (layer.storage == "image/tlg5.tlg")
            return layer.opacity;
        return -1;
      };
      if (!run.subsetCompleted || !run.error.empty() ||
          !Kotonoha::Providers::dispatchTjsUiHover(run, 20.0f, 30.0f) ||
          getOpacity() != 140 ||
          Kotonoha::Providers::dispatchTjsUiHover(run, 22.0f, 32.0f) ||
          !Kotonoha::Providers::dispatchTjsUiHover(run, -2.0f, -2.0f) ||
          getOpacity() != 255 || !run.error.empty()) {
        std::printf("FAIL: TJS2 onMouseEnter/onMouseLeave hover state\n");
        ++failures;
      }
    }
    // Layer constructor ownership may not cross Window boundaries.
    write(tempDir / "startup.tjs",
          "var a = new Window(); var b = new Window();\n"
          "var bad = new Layer(a, b.primaryLayer);\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input);
      if (run.subsetCompleted ||
          run.error.find("another Window") == std::string::npos) {
        std::printf("FAIL: TJS2 cross-window Layer owner was accepted\n");
        ++failures;
      }
    }
    // Guard the allocation budget even for source code without file loops.
    write(tempDir / "startup.tjs",
          "var w = new Window();\n"
          "for (var i = 0; i < 300; i++) { var x = new Layer(w); }\n");
    {
      Kotonoha::Providers::KirikiriAssets input(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(input, 10000);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf("FAIL: TJS2 repeated Layer allocation was unbounded\n");
        ++failures;
      }
    }
    std::filesystem::remove(tempDir / "startup.tjs");
  }

  // TJS2 script-defined classes retain per-instance fields and can expose
  // named methods as the script-built menu's click handlers. These fixtures
  // contain no original or commercial game assets.
  {
    const auto write = [](const std::filesystem::path &path,
                          const std::string &source) {
      std::ofstream file(path, std::ios::binary);
      file << source;
    };
    write(tempDir / "startup.tjs",
          "class SourceConfig {\n"
          "  function SourceConfig(prefix) { this.prefix = prefix; }\n"
          "  function target(name) { return this.prefix + name; }\n"
          "}\n"
          "var config = new SourceConfig(\"scenario/\");\n"
          "Scripts.execStorage(config.target(\"first.ks\"));\n"
          "System.title = \"Class bootstrap\";\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      provider.mountXp3("data.xp3");
      const auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      if (!run.subsetCompleted || !run.error.empty() ||
          run.kagEntry != "scenario/first.ks" ||
          run.title != "Class bootstrap") {
        std::printf("FAIL: TJS2 class constructor, this and method call\n");
        ++failures;
      }
    }

    // Controller objects may expose a bound method to an ordinary Layer,
    // even when the Controller itself does not derive from Layer.
    write(tempDir / "startup.tjs",
          "class MenuController {\n"
          "  function MenuController(layer) { this.layer = layer; }\n"
          "  function start() {\n"
          "    this.layer.visible = false;\n"
          "    Scripts.execStorage(\"scenario/first.ks\");\n"
          "  }\n"
          "}\n"
          "var win = new Window();\n"
          "var button = new Layer(win);\n"
          "button.setPos(10, 20); button.setSize(100, 70);\n"
          "button.loadImages(\"image/tlg5.tlg\");\n"
          "var controller = new MenuController(button);\n"
          "button.onClick = controller.start;\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      provider.mountXp3("data.xp3");
      auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      if (!run.subsetCompleted || !run.error.empty() ||
          !Kotonoha::Providers::dispatchTjsUiClick(run, 30.0f, 40.0f) ||
          run.kagEntry != "scenario/first.ks" || !run.error.empty()) {
        std::printf("FAIL: TJS2 separate controller bound to Layer onClick\n");
        ++failures;
      } else {
        const auto after =
            std::find_if(run.layers.begin(), run.layers.end(),
                         [](const Kotonoha::Providers::TjsUiLayer &layer) {
                           return layer.storage == "image/tlg5.tlg";
                         });
        if (after == run.layers.end() || after->visible) {
          std::printf("FAIL: bound TJS2 controller lost its Layer receiver\n");
          ++failures;
        }
      }
    }

    write(tempDir / "startup.tjs",
          "class TitleButton extends Layer {\n"
          "  function TitleButton(owner) {\n"
          "    this.setPos(20, 30); this.setSize(70, 40);\n"
          "    this.loadImages(\"image/tlg5.tlg\");\n"
          "    this.onClick = this.activate;\n"
          "  }\n"
          "  function activate() {\n"
          "    this.visible = false;\n"
          "    Scripts.execStorage(\"scenario/first.ks\");\n"
          "  }\n"
          "}\n"
          "var window = new Window();\n"
          "var button = new TitleButton(window);\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      provider.mountXp3("data.xp3");
      auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      const auto before =
          std::find_if(run.layers.begin(), run.layers.end(),
                       [](const Kotonoha::Providers::TjsUiLayer &layer) {
                         return layer.storage == "image/tlg5.tlg";
                       });
      if (!run.subsetCompleted || !run.error.empty() ||
          run.layers.size() != 2 || before == run.layers.end() ||
          before->left != 20 || before->top != 30 || before->width != 70 ||
          before->height != 40 || before->onClick.empty() ||
          !Kotonoha::Providers::dispatchTjsUiClick(run, 30.0f, 40.0f) ||
          run.kagEntry != "scenario/first.ks" || !run.error.empty()) {
        std::printf("FAIL: TJS2 subclass of Layer and bound onClick\n");
        ++failures;
      } else {
        const auto after =
            std::find_if(run.layers.begin(), run.layers.end(),
                         [](const Kotonoha::Providers::TjsUiLayer &layer) {
                           return layer.storage == "image/tlg5.tlg";
                         });
        if (after == run.layers.end() || after->visible ||
            Kotonoha::Providers::dispatchTjsUiClick(run, 30.0f, 40.0f)) {
          std::printf("FAIL: TJS2 bound method retained Layer visibility\n");
          ++failures;
        }
      }
    }

    write(tempDir / "startup.tjs",
          "if (false) { class Hidden { function Hidden() {} } }\n"
          "var bad = new Hidden();\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      if (run.subsetCompleted || run.error.empty()) {
        std::printf("FAIL: inactive TJS2 class became executable\n");
        ++failures;
      }
    }

    write(tempDir / "startup.tjs",
          "class Unavailable extends MissingNative { }\n"
          "var bad = new Unavailable();\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      if (run.subsetCompleted ||
          run.error.find("unsupported source class base") ==
              std::string::npos) {
        std::printf("FAIL: unsupported TJS2 class parent accepted\n");
        ++failures;
      }
    }

    write(tempDir / "startup.tjs", "class Broken {\n"
                                   "  function explode() { return 8 / 0; }\n"
                                   "}\n"
                                   "var instance = new Broken();\n"
                                   "instance.explode();\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      const auto run = Kotonoha::Providers::runTjsStartupSubset(provider);
      if (run.subsetCompleted ||
          run.error.find("division by zero") == std::string::npos) {
        std::printf("FAIL: TJS2 class method errors were swallowed\n");
        ++failures;
      }
    }

    write(tempDir / "startup.tjs", "class Recursive {\n"
                                   "  function next() { return this.next(); }\n"
                                   "}\n"
                                   "var instance = new Recursive();\n"
                                   "instance.next();\n");
    {
      Kotonoha::Providers::KirikiriAssets provider(tempDir);
      const auto run =
          Kotonoha::Providers::runTjsStartupSubset(provider, 10000);
      if (run.subsetCompleted ||
          run.error.find("recursion limit") == std::string::npos) {
        std::printf("FAIL: TJS2 class method recursion not bounded\n");
        ++failures;
      }
    }
    std::filesystem::remove(tempDir / "startup.tjs");
  }

  // PSB v2 + MDF binary serialization/parsing and ScnRunner execution (PT-BR
  // slot selection, speaker nameplate mapping, multi-channel voices, savepoint
  // snapshot restoration, and cross-chapter nexts transitions).
  {
    using PsbValue = Kotonoha::Providers::PsbValue;
    using PsbFile = Kotonoha::Providers::PsbFile;

    PsbValue::List textEntry = {
        PsbValue::makeString("ショコラ"),
        PsbValue::makeNull(),
        PsbValue::makeList({
            PsbValue::makeList(
                {PsbValue::makeNull(), PsbValue::makeString("「おはよう！」")}),
            PsbValue::makeList({PsbValue::makeString("Chocola"),
                                PsbValue::makeString("「Good morning!」")}),
            PsbValue::makeList(
                {PsbValue::makeString("巧克力"),
                 PsbValue::makeString("%fSourceHanSansCN-M;「%f;Bom "
                                      "dia!%fSourceHanSansCN-M;」")}),
        }),
        PsbValue::makeList({
            PsbValue::makeDict({
                {"name", PsbValue::makeString("ショコラ")},
                {"voice", PsbValue::makeString("syo_drama_0001")},
            }),
        }),
    };

    PsbValue snapshot = PsbValue::makeDict({
        {"data",
         PsbValue::makeList({
             PsbValue::makeList({
                 PsbValue::makeString("bg"),
                 PsbValue::makeString("stage"),
                 PsbValue::makeDict({
                     {"name", PsbValue::makeString("bg")},
                     {"class", PsbValue::makeString("stage")},
                     {"redraw",
                      PsbValue::makeDict({
                          {"imageFile",
                           PsbValue::makeDict(
                               {{"file", PsbValue::makeString("room_day")}})},
                      })},
                 }),
             }),
         })},
    });

    PsbValue scene = PsbValue::makeDict({
        {"label", PsbValue::makeString("*start")},
        {"title", PsbValue::makeList({PsbValue::makeString("JP"),
                                      PsbValue::makeString("EN"),
                                      PsbValue::makeString("Capítulo 1")})},
        {"texts",
         PsbValue::makeList({PsbValue::makeList(std::move(textEntry))})},
        {"lines", PsbValue::makeList({
                      PsbValue::makeString("cg_zero_01a"),
                      PsbValue::makeList({PsbValue::makeInt(1), snapshot}),
                      PsbValue::makeInt(1),
                  })},
        {"nexts", PsbValue::makeList({
                      PsbValue::makeDict({
                          {"storage", PsbValue::makeString("00_02.ks")},
                          {"target", PsbValue::makeString("*start")},
                          {"type", PsbValue::makeInt(0)},
                      }),
                  })},
    });

    PsbValue root = PsbValue::makeDict({
        {"name", PsbValue::makeString("00_01.ks")},
        {"scenes", PsbValue::makeList({scene})},
    });

    try {
      const auto mdfBytes = PsbFile::serializeV2(root, {}, true);
      const auto parsed = PsbFile::parse(mdfBytes);
      if (parsed.root()["name"].asString() != "00_01.ks") {
        std::printf("FAIL: PSB v2 / MDF round-trip failed\n");
        ++failures;
      }

      std::string shownSpeaker, shownText, shownPlate, jumpedTo;
      std::vector<std::string> shownVoices;
      Kirikiri::ScnRunner::Callbacks cb;
      cb.showDialogue = [&](std::string sp, std::string tx, std::string plate,
                            std::vector<std::string> vc) {
        shownSpeaker = std::move(sp);
        shownText = std::move(tx);
        shownPlate = std::move(plate);
        shownVoices = std::move(vc);
      };
      cb.kag.jump = [&](std::string target) { jumpedTo = std::move(target); };

      Kirikiri::ScnRunner runner(parsed, cb, 2);
      runner.run();
      if (!runner.isBlocked() || shownSpeaker != "Chocola" ||
          shownText != "Bom dia!" || shownPlate != "VNX+/cn_name_chocola.png" ||
          shownVoices != std::vector<std::string>{"syo_drama_0001"} ||
          !runner.unlockedFlags().count("cg_zero_01a")) {
        std::printf("FAIL: ScnRunner PT-BR dialogue or nameplate failed\n");
        ++failures;
      }
      const auto saved = runner.saveState();
      runner.resumeDialogue();
      if (!runner.isFinished() || jumpedTo != "00_02.ks#*start") {
        std::printf("FAIL: ScnRunner nexts transition failed\n");
        ++failures;
      }

      Kirikiri::ScnRunner restored(parsed, cb, 2);
      restored.restoreState(saved);
      if (restored.stage().file != "room_day") {
        std::printf("FAIL: ScnRunner snapshot restore failed\n");
        ++failures;
      }
    } catch (const std::exception &e) {
      std::printf("FAIL: PSB/ScnRunner exception: %s\n", e.what());
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
