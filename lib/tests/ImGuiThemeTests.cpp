#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>

#include <SDL3/SDL.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace Kotonoha;
namespace fs = std::filesystem;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TempWorkspace {
  fs::path root;
  fs::path previousCwd;

  TempWorkspace()
      : root(fs::temp_directory_path() /
             ("kotonoha-imgui-theme-" +
              std::to_string(SDL_GetPerformanceCounter()))),
        previousCwd(fs::current_path()) {
    fs::create_directories(root / "assets" / "fonts");
    fs::current_path(root);
  }

  ~TempWorkspace() {
    std::error_code ec;
    fs::current_path(previousCwd, ec);
    fs::remove_all(root, ec);
  }
};

void writeBe16(std::vector<std::uint8_t> &out, std::size_t offset,
               std::uint16_t value) {
  out[offset] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
  out[offset + 1] = static_cast<std::uint8_t>(value & 0xffu);
}

void writeBe32(std::vector<std::uint8_t> &out, std::size_t offset,
               std::uint32_t value) {
  out[offset] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
  out[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
  out[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
  out[offset + 3] = static_cast<std::uint8_t>(value & 0xffu);
}

void writeTag(std::vector<std::uint8_t> &out, std::size_t offset,
              const char *tag) {
  std::memcpy(out.data() + offset, tag, 4);
}

void writeBytes(const fs::path &path, const std::vector<std::uint8_t> &data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(out), "Cannot open synthetic font file for write");
  if (!data.empty())
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  require(static_cast<bool>(out), "Cannot write synthetic font bytes");
}

// Builds a structurally valid SFNT container (>100 bytes) with a valid cmap
// and required metrics tables, omitting both glyf and CFF so stb_truetype's
// stbtt_InitFont() rejects it after container header checks pass.
std::vector<std::uint8_t> makeIncompatibleSfnt(bool asTtcCollection) {
  const std::uint16_t numTables = 5;
  const std::size_t ttcHeaderSize = asTtcCollection ? 16u : 0u;
  const std::size_t sfntOffset = ttcHeaderSize;
  const std::size_t dirSize = 12u + numTables * 16u;
  const std::size_t dataOffset = sfntOffset + dirSize;

  // Table payloads: CFF2 (16B), cmap (32B), head (56B), hhea (36B), hmtx (4B).
  const std::size_t cff2Off = dataOffset;
  const std::size_t cmapOff = cff2Off + 16u;
  const std::size_t headOff = cmapOff + 32u;
  const std::size_t hheaOff = headOff + 56u;
  const std::size_t hmtxOff = hheaOff + 36u;
  const std::size_t totalSize = hmtxOff + 4u;

  std::vector<std::uint8_t> buf(totalSize, 0u);
  if (asTtcCollection) {
    writeTag(buf, 0, "ttcf");
    writeBe32(buf, 4, 0x00010000u);
    writeBe32(buf, 8, 1u);
    writeBe32(buf, 12, static_cast<std::uint32_t>(sfntOffset));
  }

  writeTag(buf, sfntOffset, "OTTO");
  writeBe16(buf, sfntOffset + 4, numTables);

  struct TableEntry {
    const char *tag;
    std::size_t off;
    std::size_t len;
  };
  const TableEntry tables[] = {
      {"CFF2", cff2Off, 16u}, {"cmap", cmapOff, 32u}, {"head", headOff, 56u},
      {"hhea", hheaOff, 36u}, {"hmtx", hmtxOff, 4u},
  };
  for (std::size_t i = 0; i < numTables; ++i) {
    const std::size_t rec = sfntOffset + 12u + i * 16u;
    writeTag(buf, rec, tables[i].tag);
    writeBe32(buf, rec + 8, static_cast<std::uint32_t>(tables[i].off));
    writeBe32(buf, rec + 12, static_cast<std::uint32_t>(tables[i].len));
  }

  // cmap: 1 Microsoft Unicode BMP encoding record pointing at format 4 stub.
  writeBe16(buf, cmapOff + 2, 1u);
  writeBe16(buf, cmapOff + 4, 3u);
  writeBe16(buf, cmapOff + 6, 1u);
  writeBe32(buf, cmapOff + 8, 12u);
  writeBe16(buf, cmapOff + 12, 4u);
  writeBe16(buf, cmapOff + 14, 16u);

  return buf;
}

// Builds a truncated SFNT (>100 bytes) whose table directory claims an offset
// far beyond the buffer end.
std::vector<std::uint8_t> makeTruncatedSfnt() {
  std::vector<std::uint8_t> buf(160u, 0u);
  writeBe32(buf, 0, 0x00010000u);
  writeBe16(buf, 4, 1u);
  writeTag(buf, 12, "head");
  writeBe32(buf, 20, 0x00100000u);
  writeBe32(buf, 24, 54u);
  return buf;
}

// Builds a TTC with an invalid font count (>100 bytes) to exercise
// stbtt_GetFontOffsetForIndex failure.
std::vector<std::uint8_t> makeBadTtcHeader() {
  std::vector<std::uint8_t> buf(128u, 0u);
  writeTag(buf, 0, "ttcf");
  writeBe32(buf, 4, 0x00010000u);
  writeBe32(buf, 8, 0u);
  return buf;
}

void runFontTests(const fs::path &root, const fs::path &validTtf) {
  // Engine initializes UI (SDL_ttf) and DebugOverlay (ImGui context inside
  // KotonohaEngine), avoiding duplicate static ImGui contexts across DLLs.
  const fs::path defaultFont = root / "assets" / "fonts" / defaultFontName();
  fs::copy_file(validTtf, defaultFont, fs::copy_options::overwrite_existing);

  class DummyRuntime final : public Runtime {
  public:
    DummyRuntime() : Runtime("ImGuiThemeTest", "1", "imgui-theme-test") {}
  };

  const fs::path emptyFile = root / "empty.ttf";
  const fs::path tinyFile = root / "tiny.ttf";
  const fs::path garbageFile = root / "garbage.ttf";
  const fs::path truncatedFile = root / "truncated.ttf";
  const fs::path badTtcFile = root / "bad-header.ttc";
  const fs::path incompatibleOtf = root / "incompatible.otf";
  const fs::path incompatibleTtc = root / "incompatible.ttc";

  writeBytes(emptyFile, {});
  writeBytes(tinyFile, std::vector<std::uint8_t>(64u, 0x41u));
  writeBytes(garbageFile, std::vector<std::uint8_t>(256u, 0x5Au));
  writeBytes(truncatedFile, makeTruncatedSfnt());
  writeBytes(badTtcFile, makeBadTtcHeader());
  writeBytes(incompatibleOtf, makeIncompatibleSfnt(false));
  writeBytes(incompatibleTtc, makeIncompatibleSfnt(true));

  {
    DummyRuntime runtime;
    EngineOptions options;
    options.persistSettings = false;
    Engine engine(runtime, options);

    // Case 1: Both custom and default fonts are incompatible (Android scenario).
    // Must fall back to AddFontDefaultVector() without asserting or crashing.
    for (const auto &bad : {emptyFile, tinyFile, garbageFile, truncatedFile,
                            badTtcFile, incompatibleOtf, incompatibleTtc,
                            root / "does-not-exist.ttf"}) {
      require(!ImGuiTheme::loadFontsFromPaths(bad, incompatibleTtc),
              "Invalid/incompatible font pair must fall back to built-in font");
      engine.Iterate();
    }

    // Case 2: Empty custom path with incompatible default font.
    require(!ImGuiTheme::loadFontsFromPaths({}, incompatibleTtc),
            "Empty custom path with incompatible default must fall back safely");
    engine.Iterate();

    // Case 3: Valid TTF must still load as the active ImGui font.
    require(ImGuiTheme::loadFontsFromPaths(validTtf, incompatibleTtc),
            "Valid TTF font must load instead of built-in fallback");
    engine.Iterate();

    // Case 4: Invalid custom font with valid default font falls back to default.
    require(ImGuiTheme::loadFontsFromPaths(incompatibleTtc, validTtf),
            "Invalid custom font must fall back to valid default TTF");
    engine.Iterate();

    // Case 5: Public DebugOverlay::reloadFonts() and loadFonts() paths.
    engine.GetDebug().reloadFonts(incompatibleTtc.string());
    engine.Iterate();
    engine.GetDebug().reloadFonts(validTtf.string());
    engine.Iterate();
    ImGuiTheme::loadFonts();
    engine.Iterate();

    // Case 6: Repeated alternating reloads must not leak or corrupt atlas state.
    for (int i = 0; i < 12; ++i) {
      const bool expectFile = (i % 2 != 0);
      const bool loaded = ImGuiTheme::loadFontsFromPaths(
          expectFile ? validTtf : incompatibleOtf, incompatibleTtc);
      require(loaded == expectFile, "Repeated font reloads must be consistent");
      engine.Iterate();
    }
  }

  // Case 7: High-DPI / scaled Debug UI fallback across FrameCount == 2.
  // ImGui 1.92+ asserts in ErrorCheckNewFrameSanityChecks() if AddFontDefault()
  // heuristic parameters change between init and frame 2; explicit vector
  // fallback must survive multiple frames at scaled DPI.
  {
    DummyRuntime scaledRuntime;
    EngineOptions scaledOptions;
    scaledOptions.persistSettings = false;
    scaledOptions.settings.debugUIScale = 2.0f;
    Engine scaledEngine(scaledRuntime, scaledOptions);
    require(!ImGuiTheme::loadFontsFromPaths(incompatibleOtf, incompatibleTtc),
            "High-DPI fallback must load built-in default font");
    for (int frame = 0; frame < 4; ++frame)
      scaledEngine.Iterate();
  }
}
} // namespace

int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
  try {
    SDL_SetAppMetadata("ImGuiThemeTests", "1", "org.kotonoha.imguithemetests");
    const fs::path validTtf = KOTONOHA_TEST_FONT;
    require(fs::exists(validTtf), "Missing KOTONOHA_TEST_FONT fixture");
    TempWorkspace workspace;
    runFontTests(workspace.root, validTtf);
#ifdef NDEBUG
    std::cout << "Kotonoha.ImGuiTheme PASS (asserts=disabled)\n";
#else
    std::cout << "Kotonoha.ImGuiTheme PASS (asserts=enabled)\n";
#endif
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kotonoha.ImGuiTheme FAIL: " << error.what() << '\n';
    return 1;
  }
}
