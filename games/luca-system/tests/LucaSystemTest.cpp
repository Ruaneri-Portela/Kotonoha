// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Integration/format tests for the LucaSystem port. When real LucaSystem data
// is available (LUCA_SYSTEM_TEST_DATA env, defaulting to the Steam AIR
// installation) the tests validate PAK reading and script parsing against it;
// otherwise they exercise a synthetic in-format archive.
#include <LucaSystem/BitmapFont.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <LucaSystem/SceneLayers.hpp>

#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/SettingsMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
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
  struct Scratch {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("luca-system-test-" + std::to_string(SDL_GetPerformanceCounter()));
    Scratch() {
      if (!std::filesystem::create_directory(path))
        throw std::runtime_error("Cannot create LucaSystem test directory");
    }
    ~Scratch() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  };
  static const Scratch scratch;
  return scratch.path;
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
//   [36..39]  nameTablePtr (when named)
//   [40..]    offset table: 8 bytes per entry (offset in blocks, length).
// The first offset is also the sentinel (headerLength / blockSize).
std::vector<std::uint8_t> buildPak(
    uint32_t idStart, uint32_t blockSize, uint32_t flags,
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> &files,
    bool namesInHeader = false) {
  const bool named = (flags & 512u) != 0u;
  const size_t fileCount = files.size();
  const size_t offsetTableStart = 40;

  // Dry run: compute data offsets/lengths and total data size.
  std::vector<uint32_t> offsetBlocks;
  std::vector<uint32_t> lengths;
  const size_t tableEnd = offsetTableStart + 8 * fileCount;
  size_t nameBytes = 0;
  if (named)
    for (const auto &file : files)
      nameBytes += file.first.size() + 1;
  const size_t headerBytes = tableEnd + (namesInHeader ? nameBytes : 0);
  const size_t dataStart =
      (headerBytes + blockSize - 1) / blockSize * blockSize;
  size_t dataCursor = dataStart;
  for (const auto &[name, data] : files) {
    offsetBlocks.push_back(uint32_t(dataCursor / blockSize));
    lengths.push_back(uint32_t(data.size()));
    dataCursor += data.size();
    dataCursor += (blockSize - (data.size() % blockSize)) % blockSize;
  }
  const size_t dataEnd = dataCursor;
  const uint32_t headerLength = uint32_t(dataStart);

  const size_t nameTableStart = namesInHeader ? tableEnd : dataEnd;
  const size_t totalSize = dataEnd + (namesInHeader ? 0 : nameBytes);
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
    if (!data.empty())
      std::memcpy(out.data() + dataCursor, data.data(), data.size());
    dataCursor += data.size();
    dataCursor += (blockSize - (data.size() % blockSize)) % blockSize;
  }

  // Name table (named only).
  if (named) {
    size_t cursor = nameTableStart;
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

void writeBytes(const std::filesystem::path &path,
                const std::vector<uint8_t> &bytes) {
  writeFile(path, {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
}

void checkFontOverlay(const std::filesystem::path &base,
                      const std::filesystem::path &overlay, int advance,
                      bool added = false) {
  // 'advance' is used only when the requested glyph is absent from TTF.
  constexpr char32_t extra = U'\uE000';
  if (added) {
    const Kotonoha::Providers::LucaSystemAssets original(base, {});
    const LucaSystem::BitmapFont font(original.read("info27"));
    check(!font.glyph(extra), "New glyph is absent in the original AIR font");
  }
  const auto assets =
      std::make_shared<Kotonoha::Providers::LucaSystemAssets>(base, overlay);
  LucaSystem::LucaSystemRuntime runtime(assets);
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.launcher = false;
  options.rendererOverride = "software";
  options.settings.vnTextColorOverride = true;
  const SDL_Color dialogue{235, 80, 120, 255};
  const SDL_Color speaker{80, 220, 120, 255};
  options.settings.vnTextColor = dialogue;
  options.settings.vnSpeakerColor = speaker;
  Kotonoha::Engine engine(runtime, options);
  const auto key = [&](SDL_Keycode value) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = value;
    return runtime.onEvent(event);
  };
  const auto saveSlotsOpen = [&] {
    for (auto *scene : runtime.GetActiveScenes()) {
      if (auto *slots = dynamic_cast<Kotonoha::VNSaveMenu *>(scene))
        return slots->isOpen();
    }
    return false;
  };
  auto &menu = Kotonoha::GetInGameMenuRuntime(engine);
  key(SDLK_ESCAPE); // dismiss a retail startup logo, if present
  for (int i = 0; i < 2; ++i)
    key(SDLK_RIGHT);
  key(SDLK_RETURN);
  check(engine.GetSettingsMenu().isOpen() && !menu.IsOpen(),
        "AIR title Options opens native Kotonoha settings");
  engine.GetSettingsMenu().close();
  key(SDLK_RIGHT);
  key(SDLK_RETURN);
  check(menu.IsOpen(), "AIR title Manual opens a modal");
  if (assets->contains("en_help1") && assets->contains("en_help2")) {
    const auto cached = assets->cachedTextures();
    menu.render(engine.GetRenderer(), 1);
    key(SDLK_RIGHT);
    menu.render(engine.GetRenderer(), 1);
    check(assets->cachedTextures() >= cached + 2,
          "AIR manual renders both original English help pages");
  }
  key(SDLK_ESCAPE);
  check(!menu.IsOpen(), "Closing the AIR manual returns to the title");
  key(SDLK_LEFT);
  key(SDLK_LEFT);
  key(SDLK_RETURN);
  check(saveSlotsOpen(), "AIR title Load opens native save slots");
  key(SDLK_ESCAPE);
  check(!saveSlotsOpen(), "AIR title Load can be cancelled");
  if (!runtime.startScenario("seen170", 0, true))
    throw std::runtime_error("Cannot start font overlay test scenario");
  runtime.SetState(Kotonoha::GameState::Gameplay);
  runtime.initGameplay(engine);
  Kotonoha::VNTextBox *box = nullptr;
  for (auto *scene : runtime.GetActiveScenes()) {
    if ((box = dynamic_cast<Kotonoha::VNTextBox *>(scene)))
      break;
  }
  if (!box)
    throw std::runtime_error("AIR did not register its VNTextBox");
  for (int i = 0; i < 1000 && runtime.isRunning() &&
                  !runtime.isDialogueVisible() && !runtime.isChoiceActive();
       ++i) {
    if (runtime.isMoviePlaying())
      runtime.skipMovie();
    else
      runtime.skipWait();
  }
  if (!runtime.isDialogueVisible())
    throw std::runtime_error("AIR did not reach the first dialogue");
  const auto pc = runtime.instructionPosition();
  key(SDLK_ESCAPE);
  check(menu.IsOpen() && runtime.instructionPosition() == pc &&
            !runtime.handleAdvanceAction(),
        "Escape opens the AIR pause menu without advancing dialogue");
  if (assets->contains("systemmenu")) {
    const auto cached = assets->cachedTextures();
    menu.render(engine.GetRenderer(), 1);
    check(assets->cachedTextures() > cached,
          "AIR pause menu renders the original systemmenu assets");
  }
  key(SDLK_RETURN);
  check(saveSlotsOpen() && !menu.IsOpen() && !runtime.handleAdvanceAction(),
        "AIR pause Save opens native slots and blocks dialogue advance");
  key(SDLK_ESCAPE);
  check(!saveSlotsOpen() && runtime.instructionPosition() == pc,
        "Cancelling AIR save slots preserves the script position");
  key(SDLK_B);
  auto &backlog = Kotonoha::GetVNBacklogRuntime(engine);
  check(backlog.isVisible() && !runtime.handleAdvanceAction(),
        "AIR history captures input without advancing dialogue");
  check(std::count_if(backlog.getEntries().begin(), backlog.getEntries().end(),
                      [&](const auto &entry) {
                        return entry.text == runtime.dialogueText();
                      }) == 1,
        "AIR history records the current MESSAGE only once");
  if (assets->contains("backlog_texture")) {
    const auto cached = assets->cachedTextures();
    backlog.render(engine.GetRenderer(), 1);
    check(assets->cachedTextures() > cached,
          "AIR history renders the original backlog texture");
  }
  key(SDLK_ESCAPE);
  check(!backlog.isVisible() && runtime.instructionPosition() == pc,
        "Closing AIR history preserves the script position");
  // The overlay PUA glyph must come from the original bitmap; regular
  // characters must continue through HiDPI SDL_ttf.
  const std::string text = added ? u8"\uE000" : "A";
  box->setDialogue(text, text + text);
  box->show();
  box->completeDialogue();
  Kotonoha::SurfacePtr target(
      SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32));
  Kotonoha::RendererPtr renderer(
      target ? SDL_CreateSoftwareRenderer(target.get()) : nullptr);
  if (!renderer)
    throw std::runtime_error(SDL_GetError());
  SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
  SDL_RenderClear(renderer.get());
  const SDL_Rect parentClip{184, 510, 200, 90};
  SDL_SetRenderClipRect(renderer.get(), &parentClip);
  box->render(*renderer, 1);
  SDL_Rect clip{};
  check(SDL_RenderClipEnabled(renderer.get()) &&
            SDL_GetRenderClipRect(renderer.get(), &clip) &&
            SDL_RectsEqual(&parentClip, &clip),
        "AIR VNTextBox preserves the caller's clip rectangle");
  SDL_RenderPresent(renderer.get());
  const auto containsColor = [&](SDL_Rect area, bool isSpeaker) {
    for (int y = area.y; y < area.y + area.h; ++y) {
      for (int x = area.x; x < area.x + area.w; ++x) {
        Uint8 r = 0, g = 0, b = 0;
        if (!SDL_ReadSurfacePixel(target.get(), x, y, &r, &g, &b, nullptr))
          continue;
        if (isSpeaker ? (g > r + 35 && g > b + 20) : (r > g + 45 && r > b + 40))
          return true;
      }
    }
    return false;
  };
  if (added) {
    const auto matches = [&](int x, int y, SDL_Color color) {
      Uint8 red = 0, green = 0, blue = 0;
      return SDL_ReadSurfacePixel(target.get(), x, y, &red, &green, &blue,
                                  nullptr) &&
             red == color.r && green == color.g && blue == color.b;
    };
    constexpr int offsetX = 3 + 1, offsetY = 4 + 1;
    check(matches(188 + offsetX, 564 + offsetY, dialogue) &&
              matches(188 + offsetX + advance, 564 + offsetY, dialogue) &&
              matches(188 + offsetX, 516 + offsetY, speaker),
          "AIR private-use glyphs use retail bitmap cells at native size");
  } else {
    check(containsColor({188, 516, 95, 28}, true) &&
              containsColor({188, 564, 95, 34}, false),
          "AIR ordinary glyphs remain high-density TTF");
  }
  runtime.shutdown();
}

void syntheticFontOverlay() {
  const auto root = tempDir() / "font-patch";
  const auto base = root / "base";
  const auto overlay = root / "overlay";
  std::filesystem::create_directories(base);
  std::filesystem::create_directories(overlay);
  constexpr size_t header = 18;
  constexpr size_t table = header + 1024;
  std::vector<uint8_t> atlas(table + 12 + 64, 0);
  atlas[0] = 'C';
  atlas[1] = 'Z';
  atlas[2] = '2';
  atlas[4] = header;
  atlas[8] = 8;
  atlas[10] = 4;
  atlas[12] = 8;
  atlas[table] = 1;
  atlas[table + 4] = 64;
  atlas[table + 8] = 32;
  for (size_t i = header; i < header + 8; ++i)
    atlas[i] = i == header + 3 ? 0 : 255;
  constexpr size_t index = 6 + 2 * 3;
  std::vector<uint8_t> info(index + 65536 * 4);
  info[2] = 4;
  info[4] = 2;
  info[10] = 4;
  info[index + 2 * 'A'] = 1;
  const auto source = base / "FONT.PAK";
  writeBytes(source,
             buildPak(1, 4, 512, {{"ゴシック27", atlas}, {"info27", info}}));
  const std::vector<uint8_t> scenario = {21,  0, 36, 0, 0,   0,   1,   0,
                                         'A', 0, 0,  0, 254, 255, 'A', 'A',
                                         0,   0, 0,  0, 0,   0};
  writeBytes(base / "SCRIPT.PAK", buildPak(1, 4, 512, {{"seen170", scenario}}));
  std::vector<uint8_t> alpha(16);
  alpha[5] = alpha[6] = alpha[9] = alpha[10] = 255;
  const auto patched = LucaSystem::PatchCz2Alpha(atlas, 4, 0, 4, 4, alpha);
  LucaSystem::BitmapFont font(info);
  font.setGlyphMetrics('A', 3, 4, 4, 12);
  const auto glyph = font.appendGlyph(U'\uE000', 3, 4, 4, 18);
  const auto grown =
      LucaSystem::GrowCz2Atlas(patched, glyph.x + 4, glyph.y + 4);
  const auto added =
      LucaSystem::PatchCz2Alpha(grown, glyph.x, glyph.y, 4, 4, alpha);
  const auto atlasPath = root / "atlas.cz2";
  const auto infoPath = root / "info.bin";
  writeBytes(atlasPath, added);
  writeBytes(infoPath, font.bytes());
  LucaSystem::PakArchive archive(source);
  std::ostringstream repacked;
  archive.repack(repacked, {{1, atlasPath}, {2, infoPath}});
  writeFile(overlay / "FONT.PAK", repacked.str());
  check(archive.read("ゴシック27") == atlas && archive.read("info27") == info,
        "Font repack leaves original atlas and metrics byte-for-byte intact");
  checkFontOverlay(base, overlay, 12);
  checkFontOverlay(base, overlay, 18, true);
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--font-overlay") {
    try {
      if (argc == 5 && std::string_view(argv[4]) == "added")
        checkFontOverlay(argv[2], argv[3], 32, true);
      else if (argc == 4)
        checkFontOverlay(argv[2], argv[3], 32);
      else if (argc == 2)
        syntheticFontOverlay();
      else
        return 2;
    } catch (const std::exception &e) {
      std::fprintf(stderr, "%s\n", e.what());
      return 1;
    }
    return failures == 0 ? 0 : 1;
  }
  std::printf("LucaSystem format tests\n");

  {
    // A cropped or transparent CZ4 background must not count as backing art
    // for an attached calendar badge (the black-screen seen170 regression).
    std::vector<std::uint8_t> pixels(4 * 4 * 4, 255);
    check(!LucaSystem::hasNonOpaquePixels(pixels),
          "Opaque CZ art does not need transparency bypass");
    pixels[3] = 0;
    check(LucaSystem::hasNonOpaquePixels(pixels),
          "Alpha CZ art must bypass upscaler");
    pixels[3] = 255;
    check(LucaSystem::rasterCoversPointWithOpacity(pixels, 4, 4, 0, 0, 1, 1, 2,
                                                   2, 1),
          "Opaque background covers calendar position");
    pixels[(2 * 4 + 2) * 4 + 3] = 0;
    check(!LucaSystem::rasterCoversPointWithOpacity(pixels, 4, 4, 0, 0, 1, 1, 2,
                                                    2, 1),
          "Transparent background must hide calendar badge");
    check(!LucaSystem::rasterCoversPointWithOpacity(pixels, 4, 4, 100, 100, 1,
                                                    1, 2, 2, 1),
          "Cropped background outside badge cannot expose calendar");
    check(!LucaSystem::rasterCoversPointWithOpacity(pixels, 4, 4, 0, 0, 1, 1, 1,
                                                    1, 0.1f),
          "Dimmed background cannot expose calendar");
  }

  {
    using Bytes = std::vector<std::uint8_t>;
    const auto append = [](Bytes &dst, const Bytes &src) {
      dst.insert(dst.end(), src.begin(), src.end());
    };
    const auto instruction = [&](Bytes &script, uint8_t op, uint8_t flag,
                                 const Bytes &raw) {
      const auto len = uint16_t(4 + raw.size());
      append(script, u16le(len));
      script.push_back(op);
      script.push_back(flag);
      append(script, raw);
      if (len & 1)
        script.push_back(0x42);
    };
    // Voice ID, Japanese "J", English "E", Chinese "C" and opaque suffix.
    const Bytes message{3,   0, 1, 0, 'J', 0, 0, 0, 255, 255,
                        'E', 0, 1, 0, 'C', 0, 0, 0, 0xab};
    Bytes a;
    instruction(a, 36, 0, message);
    const auto originalMessageSize = a.size();
    // ONGOTO: UTF-8 expression "0", count=2, a backward and a forward branch.
    const auto table = [&](uint32_t target) {
      Bytes raw{255, 255, '0', 0, 2, 0};
      append(raw, u32le(0));
      append(raw, u32le(target));
      return raw;
    };
    const auto end = uint32_t(a.size() + 8 + 4 + table(0).size());
    instruction(a, 15, 0, u32le(end));
    instruction(a, 16, 0, table(end));
    instruction(a, 25, 0, {});
    const auto cross = [&](uint32_t target) {
      Bytes raw{8, 0, 249, 255, 's', 'e', 'e', 'n', '1', '7', '0', 0};
      append(raw, u32le(target));
      return raw;
    };
    // The incoming branch must be rewritten even though seen190 has no edit.
    Bytes b;
    instruction(b, 22, 1, cross(end));
    instruction(b, 25, 0, {});
    std::map<std::string, LucaSystem::Script> scripts;
    scripts.try_emplace("seen170", "seen170", a);
    scripts.try_emplace("seen190", "seen190", b);
    check(LucaSystem::PatchAirMessages(scripts, {{"seen170", {{0, "E"}}}})
              .empty(),
          "AIR script import preserves unchanged bytecode");
    const auto patched =
        LucaSystem::PatchAirMessages(scripts, {{"seen170", {{0, "longer"}}}});
    const LucaSystem::Script first("seen170", patched.at("seen170"));
    const LucaSystem::Script second("seen190", patched.at("seen190"));
    const auto &ops = first.instructions();
    check(first.valid() && second.valid() && ops[0].text == "longer" &&
              ops[0].rawBytes.back() == 0xab && ops[0].align.empty() &&
              ops[3].pos == end + 4 && ops[1].paramBytes == u32le(end + 4) &&
              ops[2].paramBytes == table(end + 4) &&
              second.instructions()[0].rawBytes == cross(end + 4),
          "AIR script import resizes MESSAGE and relocates local and incoming "
          "branches");
    scripts.insert_or_assign("seen170", first);
    scripts.insert_or_assign("seen190", second);
    const auto restored =
        LucaSystem::PatchAirMessages(scripts, {{"seen170", {{0, "E"}}}});
    auto expected = a;
    expected[originalMessageSize - 1] = 0; // rebuilt padding
    check(restored.at("seen170") == expected && restored.at("seen190") == b,
          "AIR script import roundtrips bytes and targets after shrinking");
    auto bad = b;
    bad[16] = static_cast<uint8_t>(end + 1);
    scripts.insert_or_assign("seen190", LucaSystem::Script("seen190", bad));
    bool rejected = false;
    try {
      (void)LucaSystem::PatchAirMessages(scripts, {});
    } catch (const std::runtime_error &e) {
      rejected = std::string_view(e.what()).find("jump does not land") !=
                 std::string_view::npos;
    }
    check(rejected, "AIR script import rejects branch into a payload");
  }

  {
    const auto dir = tempDir();
    const auto originalPath = dir / "repack-input.pak";
    const auto outputPath = dir / "repack-output.pak";
    const auto replacementPath = dir / "repack-replacement.bin";
    const auto emptyPath = dir / "repack-empty.bin";
    const std::string payload("patch\0with\0zeros", 16);
    const std::vector<std::uint8_t> kept{0xab, 0, 0xcd};
    writeFile(replacementPath, payload);
    writeFile(emptyPath, {});
    for (const auto layout : {0, 1, 2}) {
      const bool named = layout != 0;
      const bool namesInHeader = layout == 2;
      const auto label = !named          ? "numeric"
                         : namesInHeader ? "named, inline"
                                         : "named, external";
      auto original = buildPak(500, 8, named ? 513 : 1,
                               {{"α", {1}}, {"beta", {2, 3}}, {"keep", kept}},
                               namesInHeader);
      original[16] = 0xc4; // opaque header field
      writeFile(originalPath, {reinterpret_cast<const char *>(original.data()),
                               original.size()});
      const LucaSystem::PakArchive source(originalPath);
      std::ostringstream stream;
      source.repack(stream, {{500, replacementPath}, {501, emptyPath}});
      const auto output = stream.str();
      writeFile(outputPath, output);
      const LucaSystem::PakArchive reopened(outputPath);
      const auto &entries = reopened.entries();
      const auto &sourceEntries = source.entries();
      check(reopened.named() == named && reopened.blockSize() == 8 &&
                reopened.idStart() == 500 && entries.size() == 3 &&
                entries[0].name == sourceEntries[0].name &&
                entries[1].name == sourceEntries[1].name &&
                entries[2].name == sourceEntries[2].name &&
                static_cast<uint8_t>(output[16]) == 0xc4 &&
                static_cast<uint8_t>(output[33]) == original[33] &&
                reopened.findById(502) == &entries[2] &&
                (!named || reopened.findByName("α") == &entries[0]),
            std::string("PAK repack preserves metadata: ") + label);
      check(reopened.read(entries[0]) ==
                    std::vector<std::uint8_t>(payload.begin(), payload.end()) &&
                reopened.read(entries[1]).empty() &&
                reopened.read(entries[2]) == kept &&
                entries[2].offset == entries[0].offset + 16 &&
                source.read(sourceEntries[0]) == std::vector<std::uint8_t>{1},
            std::string("PAK repack preserves exact entry bytes: ") + label);
    }
  }

  {
    // Reuse Kotonoha's checked-in synthetic Vorbis tone, not commercial data.
    const auto testDir = std::filesystem::path(__FILE__).parent_path();
    const auto games = testDir.parent_path().parent_path();
    const auto tone = Kotonoha::Filesystem::readLocalFile(
        games / "school-days/tests/fixtures/Voice/test.OGG");
    const std::vector<std::uint8_t> first{'O', 'g', 'g', 'S', 1};
    std::vector<std::uint8_t> bundle{'O', 'g', 'g', 'P', 'a', 'k', 0};
    for (const auto *clip : {&first, &tone}) {
      for (const auto value : {44100u, uint32_t(clip->size())}) {
        const auto bytes = u32le(value);
        bundle.insert(bundle.end(), bytes.begin(), bytes.end());
      }
      bundle.insert(bundle.end(), clip->begin(), clip->end());
    }
    auto truncated = bundle;
    truncated.pop_back();
    auto shortHeader = bundle;
    shortHeader.push_back(0);
    auto variant = bundle;
    variant[0] = 'X'; // Both references treat the seven bytes as opaque.
    auto badCapture = bundle;
    badCapture[15] = 0;
    const auto pak = buildPak(100, 4, 512,
                              {{"bundle", bundle},
                               {"raw", tone},
                               {"truncated", truncated},
                               {"short_header", shortHeader},
                               {"variant", variant},
                               {"bad_capture", badCapture}});
    const auto dir = tempDir() / "oggpak";
    const auto overlay = dir / "overlays";
    std::filesystem::create_directories(overlay / "voice/100");
    writeFile(dir / "VOICE0.PAK",
              {reinterpret_cast<const char *>(pak.data()), pak.size()});
    // Small PCM WAV replacement with a different sample rate from the Ogg.
    const std::vector<std::uint8_t> wav = {
        'R', 'I', 'F', 'F', 40,  0,  0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16,  0,  0, 0, 1,   0,   1,   0,
        64,  31,  0,   0,   128, 62, 0, 0, 2,   0,   16,  0,
        'd', 'a', 't', 'a', 4,   0,  0, 0, 0,   1,   0,   2};
    writeFile(overlay / "voice/100/1.ogg",
              {reinterpret_cast<const char *>(wav.data()), wav.size()});
    Kotonoha::Providers::LucaSystemAssets assets(dir, overlay);
    check(assets.read("voice:100") == first &&
              assets.read("voice:100:0") == first &&
              assets.readVoiceAsset(100, 1) == tone &&
              assets.read("voice:104:1") == tone,
          "OggPak selects distinct clips and accepts variant headers");
    check(assets.contains("voice:100:1") && !assets.contains("voice:100:2") &&
              !assets.contains("voice:100:") &&
              !assets.contains("voice:100x") &&
              !assets.contains("voice:4294967396"),
          "OggPak rejects invalid IDs and missing clip indices");
    check(assets.read("voice:101") == tone && !assets.contains("voice:101:1"),
          "Direct Ogg voice entries remain unchanged");
    const auto virtualVoiceIds = assets.list("voice:");
    check(std::find(virtualVoiceIds.begin(), virtualVoiceIds.end(),
                    "voice:100") != virtualVoiceIds.end() &&
              std::find(virtualVoiceIds.begin(), virtualVoiceIds.end(),
                        "voice:101") != virtualVoiceIds.end(),
          "Asset Provider Viewer lists numeric archive voice IDs");
    const auto virtualFillIds = assets.list("image:");
    check(std::find(virtualFillIds.begin(), virtualFillIds.end(),
                    "image:65000") != virtualFillIds.end() &&
              assets.contains("image:65000"),
          "Asset Provider Viewer lists synthetic image IDs");
    const auto exportedVoice = dir / "voice-original-dump.ogg";
    check(assets.dumpAsset("voice:100:1", exportedVoice,
                           Kotonoha::AssetSource::Original) == tone.size() &&
              Kotonoha::Filesystem::readLocalFile(exportedVoice) == tone,
          "Debug asset dump preserves original archive voice bytes");

    const auto rejects = [&assets](const char *id, std::string_view error) {
      try {
        (void)assets.read(id);
        return false;
      } catch (const std::runtime_error &e) {
        return std::string_view(e.what()).find(error) != std::string_view::npos;
      }
    };
    check(rejects("voice:102", "truncated clip payload") &&
              rejects("voice:103", "truncated clip header") &&
              rejects("voice:105", "invalid Ogg capture"),
          "OggPak rejects truncated and invalid records even for clip zero");
    const auto original = assets.loadOriginalAudio("voice:100:1");
    const auto replacement = assets.loadAudio("voice:100:1");
    check(!original.data.empty() && original.spec.freq == 44100 &&
              !replacement.data.empty() && replacement.spec.freq == 8000,
          "Kotonoha decodes OggPak audio and honors per-clip voice overlays");
  }

  {
    constexpr size_t index = 6 + 99 * 3;
    std::vector<uint8_t> bytes(index + 65536 * 4);
    bytes[2] = 4;
    bytes[4] = 99;
    bytes[index + 2 * 'A'] = 98;
    bytes[index + 2 * 65536 + 2 * 'A' + 1] = 17;
    bytes.push_back(0xd3); // preserve data after the tables
    LucaSystem::BitmapFont font(bytes);
    const auto newGlyph = font.appendGlyph(U'\uE000', -2, 3, 8, 0);
    const LucaSystem::BitmapFont restored(font.bytes());
    const auto prior = restored.glyph('A');
    check(font.bytes()[4] == 100 && font.bytes()[6] == 100 &&
              font.bytes().size() == bytes.size() + 5 &&
              font.bytes().back() == 0xd3 && restored.glyph(U'\uE000') &&
              newGlyph.x == 396 && newGlyph.y == 0 && newGlyph.offsetX == -2 &&
              newGlyph.advance == 8 && prior && prior->advance == 17,
          "Luca font appends through the 100-glyph marker without losing data");
    const auto next = font.appendGlyph(U'\uE001', 0, 0, 4, 4);
    check(next.x == 0 && next.y == 4 && font.bytes()[6] == 101,
          "Luca font adds the next glyph on a new 100-column row");
    bool duplicate = false;
    try {
      (void)font.appendGlyph('A', 0, 0, 4, 4);
    } catch (const std::runtime_error &) {
      duplicate = true;
    }
    check(duplicate, "Luca font append refuses an existing Unicode mapping");
  }

  {
    constexpr size_t index = 8 + 2 * 3;
    constexpr size_t metrics = index + 65536 * 2;
    std::vector<std::uint8_t> info(metrics + 65536 * 2);
    info[2] = 16;  // cell size
    info[4] = 100; // legacy glyph count marker
    info[6] = 2;   // glyph count
    info[9] = 8;   // space draw width
    info[11] = 255;
    info[12] = 6;
    info[13] = 254;
    info[index + 2 * 'A'] = 1;
    info[metrics + 2 * 'A' + 1] = 7;
    const LucaSystem::BitmapFont font(info);
    const auto glyph = font.glyph('A');
    check(glyph && glyph->x == 16 && glyph->y == 0 && glyph->offsetX == -1 &&
              glyph->offsetY == -2 && glyph->advance == 7 &&
              font.glyph(' ')->advance == 8,
          "Bitmap font maps legacy cells, signed offsets and advances");
    check(font.fitsAtlas(32, 16) && !font.fitsAtlas(31, 16),
          "Bitmap font validates atlas dimensions");
    check(!font.forEachGlyph("A🙂", [](const auto &) {}),
          "Bitmap font requests fallback for an unsupported character");
    info.pop_back();
    bool rejected = false;
    try {
      (void)LucaSystem::BitmapFont(info);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "Bitmap font rejects truncated metrics");
  }

  // Two CZ2 blocks with independent dictionaries. The first mixes narrow and
  // wide codes; the second starts with a different palette index.
  {
    constexpr size_t headerSize = 18;
    constexpr size_t table = headerSize + 256 * 4;
    std::vector<std::uint8_t> cz2(table, 0);
    cz2[0] = 'C';
    cz2[1] = 'Z';
    cz2[2] = '2';
    cz2[4] = headerSize;
    cz2[8] = 4;  // width
    cz2[10] = 1; // height
    cz2[12] = 8; // indexed
    cz2[headerSize + 4] = 10;
    cz2[headerSize + 5] = 20;
    cz2[headerSize + 6] = 30;
    cz2[headerSize + 7] = 40;
    cz2[headerSize + 11] = 100;
    for (const auto value : {2u, 4u, 3u, 2u, 1u}) {
      const auto field = u32le(value);
      cz2.insert(cz2.end(), field.begin(), field.end());
    }
    // Narrow literal 1, wide code 257 (1,1) ending in implicit zero padding,
    // then narrow literal 2 in a separate block.
    cz2.insert(cz2.end(), {2, 0, 3, 2, 4, 0});
    const auto decoded = LucaSystem::DecodeCzImage(cz2);
    const std::vector<std::uint8_t> expected = {
        30, 20, 10, 40, 30, 20, 10, 40, 30, 20, 10, 40, 0, 0, 0, 100};
    check(decoded.rgba == expected,
          "CZ2 decodes mixed-width blocks and BGRA palette");
    check(LucaSystem::PatchCz2Alpha(cz2, 3, 0, 1, 1, {100}) == cz2,
          "CZ2 alpha edit preserves the exact file when pixels match");
    const auto patch = LucaSystem::PatchCz2Alpha(cz2, 3, 0, 1, 1, {41});
    auto expectedPatch = expected;
    std::copy_n(expected.begin(), 4, expectedPatch.begin() + 12);
    constexpr size_t firstBlockEnd = table + 20 + 4;
    check(LucaSystem::DecodeCzImage(patch).rgba == expectedPatch &&
              std::equal(cz2.begin(), cz2.begin() + firstBlockEnd,
                         patch.begin(), patch.begin() + firstBlockEnd),
          "CZ2 alpha edit quantizes to the original palette and preserves "
          "untouched compressed blocks");
    const auto vertical = LucaSystem::GrowCz2Atlas(cz2, 4, 2);
    const auto tall = LucaSystem::DecodeCzImage(vertical);
    check(tall.width == 4 && tall.height == 2 &&
              std::equal(expected.begin(), expected.end(), tall.rgba.begin()) &&
              std::equal(cz2.begin() + table + 20, cz2.end(),
                         vertical.begin() + table + 28) &&
              tall.rgba[19] == 0 && tall.rgba[31] == 0,
          "CZ2 adds a transparent row without re-encoding existing blocks");
    const auto wide =
        LucaSystem::DecodeCzImage(LucaSystem::GrowCz2Atlas(cz2, 5, 2));
    check(wide.width == 5 && wide.height == 2 &&
              std::equal(expected.begin(), expected.end(), wide.rgba.begin()) &&
              wide.rgba[19] == 0 && wide.rgba[39] == 0,
          "CZ2 expands the stride without changing existing pixels");
    const auto rejects = [](const auto &bytes) {
      try {
        (void)LucaSystem::DecodeCzImage(bytes);
        return false;
      } catch (const std::runtime_error &) {
        return true;
      }
    };
    cz2.pop_back();
    check(rejects(cz2), "CZ2 rejects a truncated final block");
    cz2.push_back(0);
    cz2[table + 8] = 5;
    check(rejects(cz2), "CZ2 rejects a block exceeding the image size");
  }

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

  // ---- Native Debug UI scenario and resource integration (synthetic PAK) ----
  {
    const auto root = tempDir() / "debug-catalog";
    std::filesystem::create_directories(root / "image");
    // IMAGELOAD group=2, slot=0, image ID=1127, followed by END.
    const std::vector<uint8_t> seen170 = {8,   0, 55, 0, 2,  0,
                                          103, 4, 4,  0, 25, 0};
    const std::vector<uint8_t> seen190 = {4, 0, 40, 0}; // SELECT blocks
    writeBytes(
        root / "SCRIPT.PAK",
        buildPak(0, 4, 512, {{"seen170", seen170}, {"seen190", seen190}}));
    writeBytes(root / "image" / "BGCG.PAK",
               buildPak(1127, 4, 0, {{"", {1, 2, 3}}}));
    auto provider = std::make_shared<Kotonoha::Providers::LucaSystemAssets>(
        root, std::filesystem::path{});
    LucaSystem::LucaSystemRuntime runtime(provider);
    const auto before = runtime.debugNodes();
    const auto containsNode = [](const auto &nodes, const std::string &id) {
      return std::find_if(nodes.begin(), nodes.end(),
                          [&](const auto &node) { return node.id == id; });
    };
    check(containsNode(before, "luca_system | scenario seen170") !=
                  before.end() &&
              containsNode(before, "luca_system | scenario seen190") !=
                  before.end(),
          "Luca debug lists native SCRIPT.PAK scenarios before game startup");
    check(runtime.startScenario("seen170", 0, true),
          "Luca test scenario loads without executing its bytecode");
    const auto nodes = runtime.debugNodes();
    const auto active =
        containsNode(nodes, "luca_system | scenario seen170 @0");
    const auto scenario = containsNode(nodes, "luca_system | scenario seen170");
    const auto hasResource = [](const auto &node, const std::string &id) {
      return std::find(node.assets.begin(), node.assets.end(), id) !=
             node.assets.end();
    };
    check(active != nodes.end() && scenario != nodes.end() &&
              hasResource(*active, "image:1127") &&
              hasResource(*scenario, "image:1127") &&
              hasResource(*scenario, "seen170") &&
              runtime.debugNodeCatalogKey() == "seen170",
          "Luca native debug node and scenario show resolved archive assets");
    runtime.debugJumpToNode("luca_system | scenario seen190");
    check(runtime.debugNodeCatalogKey() == "seen190",
          "Luca debug jump switches scenario and invalidates node catalog");
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

    const auto originalScriptData = scriptData;
    LucaSystem::Script script("test.scr", std::move(scriptData));
    check(script.valid(), "script valid");
    if (script.valid())
      check(script.serialize() == originalScriptData,
            "script CodeLine roundtrip preserves original bytes");
    check(script.instructionCount() == 1, "script one instruction");
    if (script.valid()) {
      const auto &original = script.instructions().front();
      auto replacement = original.rawBytes;
      if (replacement.size() >= 7)
        replacement[2] = 'H'; // keep length, fixed field and other bytes
      auto patchedData = script.replaceRaw(0, replacement);
      LucaSystem::Script patched("patched.scr", patchedData);
      check(patched.valid() &&
                patched.instructions()[0].rawBytes == replacement,
            "same-size instruction rewrite preserves opcode structure");
      check(script.serialize() == originalScriptData,
            "script patch does not mutate original bytes");
      // Multi-edit raw patching preserves all CodeLine boundaries and unknown
      // opcodes without changing any jump offsets.
      std::vector<std::uint8_t> joined = originalScriptData;
      joined.insert(joined.end(), originalScriptData.begin(),
                    originalScriptData.end());
      LucaSystem::Script pair("pair.scr", joined);
      auto second = replacement;
      if (second.size() >= 3)
        second[2] = 'J';
      const auto edited = pair.replaceRawAtOffsets(
          {{0, replacement},
           {static_cast<std::uint32_t>(originalScriptData.size()), second}});
      LucaSystem::Script updated("updated.scr", edited);
      check(updated.valid() && updated.instructionCount() == 2 &&
                edited.size() == joined.size() &&
                updated.instructions()[0].rawBytes == replacement &&
                updated.instructions()[1].rawBytes == second &&
                pair.serialize() == joined,
            "multi-instruction raw patch preserves offsets and source");
      bool rejectsMisaligned = false;
      try {
        (void)pair.replaceRawAtOffsets({{1, replacement}});
      } catch (const std::out_of_range &) {
        rejectsMisaligned = true;
      }
      check(rejectsMisaligned,
            "raw patch rejects offsets inside an instruction");
      bool rejectsWrongLength = false;
      try {
        (void)pair.replaceRawAtOffsets({{0, {}}});
      } catch (const std::invalid_argument &) {
        rejectsWrongLength = true;
      }
      check(rejectsWrongLength, "raw patch rejects size-changing replacements");
      bool rejectsSize = false;
      try {
        (void)script.replaceRaw(0, {});
      } catch (const std::invalid_argument &) {
        rejectsSize = true;
      }
      check(rejectsSize, "instruction replacement rejects length changes");
    }
    const auto &ins = script.instructions()[0];
    check(ins.opcode == 1, "instruction opcode == 1");
    check(ins.fixedFlag == 1, "instruction fixedFlag == 1");
    check(script.strings().empty(),
          "non-MESSAGE instruction does not expose arbitrary byte strings");
  }

  // ---- Odd-length CodeLine payloads and malformed stream handling ----
  {
    LucaSystem::Script odd("odd.scr", {0x05, 0x00, 0x01, 0x00, 0x7f, 0x00});
    check(odd.valid() && odd.instructionCount() == 1,
          "odd-length CodeLine with alignment parses");
    if (odd.valid())
      check(odd.serialize() ==
                std::vector<std::uint8_t>({0x05, 0x00, 0x01, 0x00, 0x7f, 0x00}),
            "odd-length CodeLine roundtrip keeps alignment byte");
    if (odd.valid() && !odd.instructions().empty()) {
      const auto &entry = odd.instructions().front();
      check(entry.rawBytes == std::vector<std::uint8_t>{0x7f} &&
                entry.align == std::vector<std::uint8_t>{0x00},
            "odd-length CodeLine retains final payload byte");
    }
    LucaSystem::Script noPad("missing-pad.scr", {0x05, 0x00, 0x01, 0x00, 0x7f});
    check(!noPad.valid(), "missing odd alignment is rejected");
    LucaSystem::Script tail("tail.scr", {0x04, 0x00, 0x01, 0x00, 0xff});
    check(!tail.valid(), "truncated trailing instruction is rejected");
    LucaSystem::Script fixed("fixed.scr", {0x04, 0x00, 0x01, 0x02});
    check(!fixed.valid(), "truncated fixed parameters are rejected");
    bool rejectedMalformedSerialization = false;
    try {
      (void)fixed.serialize();
    } catch (const std::runtime_error &) {
      rejectedMalformedSerialization = true;
    }
    check(rejectedMalformedSerialization,
          "malformed script cannot be serialized");
  }

  // An empty Japanese AIR MESSAGE must not suppress the English field.
  {
    // fixed uint16; Japanese UTF-16 length=0; English encoded length=-2;
    // UTF-8 'OK' + NUL, followed by optional Chinese field.
    const std::vector<std::uint8_t> payload = {0,    0,   0,   0, 0xfe,
                                               0xff, 'O', 'K', 0};
    std::vector<std::uint8_t> bytes = {0x0d, 0x00, 36, 0};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    bytes.push_back(0); // alignment: length 13 is odd
    LucaSystem::Script message("message.scr", std::move(bytes));
    check(message.valid() && message.strings().size() == 1 &&
              message.strings().front() == "OK",
          "AIR MESSAGE uses English even when Japanese is empty");
  }

  // AIR VARSTR_SET (`_varstr`) rows: a leading uint16 constant that is NOT a
  // length prefix, a NUL-terminated scenario name, the string id, then
  // JP/EN/ZH length-prefixed UTF-16 strings.
  {
    auto makeInstruction = [](std::uint8_t opcode, std::uint8_t fixedFlag,
                              const std::vector<std::uint8_t> &payload) {
      std::vector<std::uint8_t> raw = payload;
      const auto len = std::uint16_t(4 + raw.size());
      std::vector<std::uint8_t> bytes = u16le(len);
      bytes.push_back(opcode);
      bytes.push_back(fixedFlag);
      bytes.insert(bytes.end(), raw.begin(), raw.end());
      if (len & 1)
        bytes.push_back(0); // alignment byte, not part of len
      return bytes;
    };
    auto utf16 = [](std::string_view text) {
      std::vector<std::uint8_t> out = u16le(std::uint16_t(text.size()));
      for (char c : text) {
        out.push_back(std::uint8_t(c));
        out.push_back(0);
      }
      out.push_back(0);
      out.push_back(0); // terminator
      return out;
    };

    std::vector<std::uint8_t> payload = {0x34, 0x12}; // constant != 0xfff9
    const char scenario[] = "seen999";
    payload.insert(payload.end(), scenario, scenario + sizeof(scenario));
    auto id = u16le(202);
    payload.insert(payload.end(), id.begin(), id.end());
    for (const std::string_view text : {"JP", "EN", "ZH"}) {
      auto field = utf16(text);
      payload.insert(payload.end(), field.begin(), field.end());
    }

    LucaSystem::Script varstr("_varstr", makeInstruction(29, 0, payload));
    check(varstr.valid() && varstr.instructionCount() == 1,
          "VARSTR_SET synthetic script parses");
    if (varstr.valid() && !varstr.instructions().empty()) {
      const auto entry =
          LucaSystem::ParseVarStrSet(varstr.instructions().front());
      check(entry.has_value(), "VARSTR_SET payload decodes");
      if (entry) {
        check(entry->scenario == "seen999",
              "VARSTR_SET reads NUL-terminated scenario name");
        check(entry->id == 202, "VARSTR_SET reads string id");
        check(entry->text == "EN", "VARSTR_SET prefers English translation");
      }

      size_t cursor = 2;
      check(LucaSystem::ReadLsCString(varstr.instructions().front().paramBytes,
                                      cursor) == "seen999",
            "ReadLsCString consumes the NUL terminator");
      check(cursor == 2 + sizeof(scenario),
            "ReadLsCString advances past the terminator");
    }

    // Empty Japanese text must still fall through to English.
    payload = {0x00, 0x00};
    payload.insert(payload.end(), scenario, scenario + sizeof(scenario));
    id = u16le(7);
    payload.insert(payload.end(), id.begin(), id.end());
    for (const std::string_view text : {"", "OK", ""}) {
      auto field = utf16(text);
      payload.insert(payload.end(), field.begin(), field.end());
    }
    LucaSystem::Script fallback("_varstr", makeInstruction(29, 0, payload));
    if (fallback.valid() && !fallback.instructions().empty()) {
      const auto entry =
          LucaSystem::ParseVarStrSet(fallback.instructions().front());
      check(entry && entry->text == "OK",
            "VARSTR_SET falls back to English when Japanese is empty");
    } else {
      check(false, "VARSTR_SET empty-Japanese script parses");
    }
  }

  // ---- Offset jumps must target actual instruction starts, never bytes
  // inside a payload. Symbolic labels are restricted to JUMPPOINT markers.
  {
    std::vector<LucaSystem::ScriptInstruction> instructions(3);
    instructions[0].pos = 0;
    instructions[0].opcode = 12; // an ordinary opcode with matching string
    instructions[0].paramBytes = {'m', 'a', 'i', 'n', 0};
    instructions[1].pos = 8;
    instructions[1].opcode = 24; // JUMPPOINT (0x18 = 24)
    instructions[1].paramBytes = {'m', 'a', 'i', 'n', 0};
    instructions[2].pos = 16;
    instructions[2].opcode = 36;
    check(LucaSystem::FindInstructionAtOffset(instructions, 8) ==
              std::optional<std::size_t>{1},
          "byte offset resolves instruction boundary");
    check(!LucaSystem::FindInstructionAtOffset(instructions, 9),
          "middle of instruction is not a jump target");
    check(!LucaSystem::FindInstructionAtOffset(instructions, 999),
          "out-of-range offsets are rejected");
    check(LucaSystem::FindJumpPoint(instructions, "main") ==
              std::optional<std::size_t>{2},
          "JUMPPOINT resolves after marker");
    check(!LucaSystem::FindJumpPoint(instructions, "missing"),
          "missing label is rejected");
    instructions[1].opcode = 12;
    check(!LucaSystem::FindJumpPoint(instructions, "main"),
          "ordinary opcode payload is not a JUMPPOINT");
  }

  // ---- Real data (AIR) when available ----
  std::string air;
  if (const char *env = std::getenv("LUCA_SYSTEM_TEST_DATA"))
    air = env;
  else
    air = "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files";
  // The retail test env var names the game root (it also holds AIR.exe), while
  // this format test consumes the PAK directory directly. Accept both layouts
  // so the same variable works for the whole LucaSystem test suite.
  if (std::filesystem::is_directory(air) &&
      !std::filesystem::exists(std::filesystem::path(air) / "PARAM.PAK") &&
      std::filesystem::is_directory(std::filesystem::path(air) / "files"))
    air = (std::filesystem::path(air) / "files").string();
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
      if (parsed.valid())
        check(parsed.serialize() == script.read(*seen),
              "AIR seen163 roundtrip preserves retail bytecode");
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
    Kotonoha::Providers::LucaSystemAssets assets(root, {});
    const LucaSystem::BitmapFont font(assets.read("info27"));
    const auto dialogueAtlas = assets.loadSurface("ゴシック27");
    const auto glyph = font.glyph(U'あ');
    check(dialogueAtlas && font.fitsAtlas(dialogueAtlas->w, dialogueAtlas->h) &&
              glyph && glyph->advance == 27,
          "AIR dialogue font loads original atlas and Japanese glyph metrics");
    check(font.forEachGlyph("A é あ", [](const auto &) {}),
          "AIR dialogue bitmap font covers Latin and Japanese");
    {
      Kotonoha::SurfacePtr target(
          SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_RGBA32));
      Kotonoha::RendererPtr renderer(
          target ? SDL_CreateSoftwareRenderer(target.get()) : nullptr);
      bool ink = false;
      bool background = false;
      if (renderer) {
        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
        SDL_RenderClear(renderer.get());
        auto &texture = assets.texture(*renderer, "ゴシック27");
        font.render(*renderer, texture, "あ", 4, 4, {255, 255, 255, 255});
        SDL_RenderPresent(renderer.get());
        for (int y = 4; y < 32; ++y) {
          for (int x = 4; x < 32; ++x) {
            Uint8 r = 0;
            if (SDL_ReadSurfacePixel(target.get(), x, y, &r, nullptr, nullptr,
                                     nullptr)) {
              ink |= r > 128;
              background |= r == 0;
            }
          }
        }
        assets.clearCache();
      }
      check(ink && background,
            "AIR dialogue font renders glyph through cached SDL texture");
    }
    const auto atlasBytes = assets.read("ZCゴシック16");
    check(atlasBytes.size() >= 3 && atlasBytes[2] == '2',
          "AIR font atlas resolves to a retail CZ2 asset");
    const auto atlas = LucaSystem::DecodeCzImage(atlasBytes);
    bool transparent = false;
    bool opaque = false;
    for (size_t i = 3; i < atlas.rgba.size(); i += 4) {
      transparent |= atlas.rgba[i] == 0;
      opaque |= atlas.rgba[i] == 255;
    }
    check(transparent && opaque,
          "AIR CZ2 font atlas preserves transparent and opaque pixels");
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
