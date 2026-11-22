#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <LucaSystem/BitmapFont.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemScript.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {

namespace fs = std::filesystem;

const LucaSystem::PakEntry &select(const LucaSystem::PakArchive &archive,
                                   std::string_view selector) {
  const LucaSystem::PakEntry *entry = nullptr;
  if (selector.substr(0, 5) == "name:" && selector.size() > 5) {
    entry = archive.findByName(selector.substr(5));
  } else if (selector.substr(0, 3) == "id:" && selector.size() > 3) {
    uint32_t id = 0;
    const auto end = selector.data() + selector.size();
    const auto result = std::from_chars(selector.data() + 3, end, id);
    if (result.ec == std::errc{} && result.ptr == end)
      entry = archive.findById(id);
  }
  if (!entry)
    throw std::runtime_error("Unknown PAK entry: " + std::string(selector));
  return *entry;
}

struct Staging {
  fs::path directory;
  fs::path archive;
  explicit Staging(const fs::path &parent) {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int i = 0; i < 100; ++i) {
      auto path = parent / (".lucapak-" + std::to_string(stamp) + "-" +
                            std::to_string(i));
      if (fs::create_directory(path)) {
        directory = std::move(path);
        archive = directory / "output.pak";
        return;
      }
    }
    throw std::runtime_error("Cannot create PAK staging directory");
  }

  ~Staging() {
    std::error_code ignored;
    for (auto it = payloads.rbegin(); it != payloads.rend(); ++it)
      fs::remove(*it, ignored);
    fs::remove(archive, ignored);
    fs::remove(directory, ignored);
  }

  std::vector<fs::path> payloads;
};

bool isAirScenario(std::string_view name) {
  return name.size() > 4 && name.substr(0, 4) == "seen" &&
         std::all_of(name.begin() + 4, name.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

std::map<uint32_t, fs::path> patchAir(const LucaSystem::PakArchive &archive,
                                      Staging &staging, int argc, char **argv) {
  LucaSystem::AirMessageEdits edits;
  for (int i = 4; i < argc; i += 3) {
    const auto &entry = select(archive, argv[i]);
    if (!isAirScenario(entry.name))
      throw std::runtime_error("Not an AIR scenario: " + entry.name);
    const std::string_view offset(argv[i + 1]);
    uint32_t value = 0;
    const auto end = offset.data() + offset.size();
    const auto parsed = std::from_chars(offset.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
      throw std::runtime_error("Invalid decimal instruction offset: " +
                               std::string(offset));
    const auto text = Kotonoha::Filesystem::readLocalFile(argv[i + 2], 65535);
    if (!edits[entry.name]
             .emplace(value, std::string(text.begin(), text.end()))
             .second)
      throw std::runtime_error("Duplicate MESSAGE edit: " + entry.name +
                               " at byte " + std::to_string(value));
  }

  std::map<std::string, LucaSystem::Script> scripts;
  for (const auto &entry : archive.entries()) {
    if (isAirScenario(entry.name))
      scripts.try_emplace(entry.name, entry.name, archive.read(entry));
  }
  const auto patched = LucaSystem::PatchAirMessages(scripts, edits);
  std::map<uint32_t, fs::path> replacements;
  for (const auto &[name, bytes] : patched) {
    const auto id = archive.findByName(name)->id;
    staging.payloads.push_back(staging.directory / std::to_string(id));
    const auto &path = staging.payloads.back();
    Kotonoha::Filesystem::OutputFile file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
      throw std::runtime_error("Cannot stage patched scenario: " + name);
    replacements.emplace(id, path);
  }
  return replacements;
}

void requireNewFile(const fs::path &path) {
  if (fs::exists(fs::symlink_status(path)))
    throw std::runtime_error("Output already exists: " + path.string());
}

template <class T> T parseNumber(std::string_view text, int base = 10) {
  T value{};
  const auto end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, value, base);
  if (parsed.ec != std::errc{} || parsed.ptr != end)
    throw std::runtime_error("Invalid number: " + std::string(text));
  return value;
}

void writePayload(const fs::path &path, const std::vector<uint8_t> &bytes) {
  Kotonoha::Filesystem::OutputFile file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  if (!file)
    throw std::runtime_error("Cannot stage payload: " + path.string());
}

void writePak(const LucaSystem::PakArchive &archive, const fs::path &path,
              const std::map<uint32_t, fs::path> &replacements) {
  Kotonoha::Filesystem::OutputFile file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot open staged PAK");
  archive.repack(file, replacements);
  file.close();
  if (!file)
    throw std::runtime_error("Cannot close staged PAK");
}

// Same-length raw payload edits work with any structurally valid LucaSystem
// script. We never interpret game-specific opcode operands or move branch PCs.
std::map<uint32_t, fs::path>
patchRaw(const LucaSystem::PakArchive &archive, Staging &staging, int argc,
         char **argv) {
  std::map<uint32_t, std::map<uint32_t, std::vector<uint8_t>>> edits;
  for (int i = 4; i < argc; i += 3) {
    const auto &entry = select(archive, argv[i]);
    const auto offset = parseNumber<uint32_t>(argv[i + 1]);
    const auto payload =
        Kotonoha::Filesystem::readLocalFile(argv[i + 2], 65535);
    if (!edits[entry.id].emplace(offset, payload).second)
      throw std::runtime_error("Duplicate raw instruction edit: " +
                               entry.name + " at byte " +
                               std::to_string(offset));
  }

  std::map<uint32_t, fs::path> replacements;
  for (const auto &[id, changes] : edits) {
    const auto *entry = archive.findById(id);
    if (!entry)
      throw std::runtime_error("Missing PAK entry ID " + std::to_string(id));
    LucaSystem::Script script(entry->name, archive.read(*entry));
    if (!script.valid())
      throw std::runtime_error(entry->name + ": " + script.error());
    const auto patched = script.replaceRawAtOffsets(changes);
    const auto path = staging.directory / ("raw-" + std::to_string(id));
    staging.payloads.push_back(path);
    writePayload(path, patched);
    replacements.emplace(id, path);
  }
  return replacements;
}

void changeGlyph(char **argv, bool add) {
  const LucaSystem::PakArchive atlas(argv[2]);
  const auto &atlasEntry = select(atlas, argv[3]);
  const LucaSystem::PakArchive metrics(argv[4]);
  const auto &metricsEntry = select(metrics, argv[5]);
  const auto destination = fs::absolute(argv[6]).lexically_normal();
  requireNewFile(destination);
  const bool samePak = fs::equivalent(atlas.path(), metrics.path());
  if (samePak ? atlasEntry.id == metricsEntry.id
              : atlas.path().filename() == metrics.path().filename())
    throw std::runtime_error("Font inputs collide in the overlay");
  const std::string_view unicode(argv[7]);
  if (unicode.substr(0, 2) != "U+")
    throw std::runtime_error("Expected a hexadecimal codepoint: U+XXXX");
  const auto cp = parseNumber<uint32_t>(unicode.substr(2), 16);
  if (cp > 0xffff || (cp >= 0xd800 && cp <= 0xdfff))
    throw std::runtime_error("Luca font glyph must be a BMP Unicode scalar");
  LucaSystem::BitmapFont font(metrics.read(metricsEntry));
  const auto x = parseNumber<int>(argv[9]);
  const auto y = parseNumber<int>(argv[10]);
  const auto width = parseNumber<int>(argv[11]);
  const auto advance = parseNumber<int>(argv[12]);
  if (add)
    font.appendGlyph(cp, x, y, width, advance);
  else
    font.setGlyphMetrics(cp, x, y, width, advance);
  const auto glyph = *font.glyph(cp);
  const auto image = Kotonoha::MediaDecoder::decodePicture(
      Kotonoha::Filesystem::readLocalFile(argv[8], 16 * 1024 * 1024));
  const auto size = font.cellSize();
  if (!image || image->w != size || image->h != size)
    throw std::runtime_error("Glyph image must match the atlas cell size: " +
                             std::to_string(size));
  const Kotonoha::SurfacePtr rgba(
      SDL_ConvertSurface(image.get(), SDL_PIXELFORMAT_RGBA32));
  if (!rgba)
    throw std::runtime_error(SDL_GetError());
  std::vector<uint8_t> alpha;
  alpha.reserve(size_t(size) * size);
  for (int y = 0; y < size; ++y) {
    const auto *row =
        static_cast<const uint8_t *>(rgba->pixels) + size_t(y) * rgba->pitch;
    for (int x = 0; x < size; ++x)
      alpha.push_back(row[x * 4 + 3]);
  }
  auto atlasBytes = atlas.read(atlasEntry);
  if (add)
    atlasBytes =
        LucaSystem::GrowCz2Atlas(atlasBytes, glyph.x + size, glyph.y + size);
  const auto patchedAtlas = LucaSystem::PatchCz2Alpha(
      atlasBytes, glyph.x, glyph.y, size, size, alpha);

  Staging staging(destination.parent_path());
  const auto overlay = staging.directory / "overlay";
  const auto atlasPayload = staging.directory / "atlas";
  const auto metricsPayload = staging.directory / "metrics";
  staging.payloads = {atlasPayload, metricsPayload, overlay};
  fs::create_directory(overlay);
  writePayload(atlasPayload, patchedAtlas);
  writePayload(metricsPayload, font.bytes());
  const auto atlasOutput = overlay / atlas.path().filename();
  staging.payloads.push_back(atlasOutput);
  std::map<uint32_t, fs::path> replacements{{atlasEntry.id, atlasPayload}};
  if (samePak)
    replacements.emplace(metricsEntry.id, metricsPayload);
  writePak(atlas, atlasOutput, replacements);
  if (!samePak) {
    const auto metricsOutput = overlay / metrics.path().filename();
    staging.payloads.push_back(metricsOutput);
    writePak(metrics, metricsOutput, {{metricsEntry.id, metricsPayload}});
  }
  requireNewFile(destination);
  fs::rename(overlay, destination);
  std::cout << "Repacked font glyph " << unicode << " into " << destination
            << '\n';
}

void usage(std::ostream &output) {
  output << "Usage: LucaPak list INPUT.pak\n"
            "       LucaPak repack INPUT.pak OUTPUT.pak"
            " [name:NAME|id:ID FILE]...\n"
            "       LucaPak patch-air SCRIPT.PAK OUTPUT.pak"
            " {name:SCENARIO|id:ID OFFSET UTF8_FILE}...\n"
            "       LucaPak patch-raw SCRIPT.PAK OUTPUT.pak"
            " {name:SCENARIO|id:ID OFFSET RAW_PAYLOAD_FILE}...\n"
            "       LucaPak {edit-glyph|add-glyph} ATLAS.pak SELECTOR"
            " INFO.pak SELECTOR"
            " OVERLAY_DIR U+XXXX GLYPH.png X Y WIDTH ADVANCE\n";
}

} // namespace

int main(int argc, char **argv) {
  const std::string_view command = argc > 1 ? argv[1] : "";
  if (command == "--help" && argc == 2) {
    usage(std::cout);
    return 0;
  }
  const bool list = command == "list" && argc == 3;
  const bool repack = command == "repack" && argc >= 4 && (argc - 4) % 2 == 0;
  const bool patch = command == "patch-air" && argc >= 7 && (argc - 4) % 3 == 0;
  const bool raw = command == "patch-raw" && argc >= 7 && (argc - 4) % 3 == 0;
  const bool glyph =
      (command == "edit-glyph" || command == "add-glyph") && argc == 13;
  if (!list && !repack && !patch && !raw && !glyph) {
    usage(std::cerr);
    return 2;
  }

  try {
    if (glyph) {
      changeGlyph(argv, command == "add-glyph");
      return 0;
    }
    const LucaSystem::PakArchive archive(argv[2]);
    if (list) {
      for (const auto &entry : archive.entries())
        std::cout << entry.id << '\t' << entry.length << '\t' << entry.name
                  << '\n';
      return std::cout ? 0 : 1;
    }
    const auto destination = fs::absolute(argv[3]);
    requireNewFile(destination);
    Staging staging(destination.parent_path());
    auto replacements = patch ? patchAir(archive, staging, argc, argv)
                        : raw ? patchRaw(archive, staging, argc, argv)
                              : std::map<uint32_t, fs::path>{};
    for (int i = 4; repack && i < argc; i += 2) {
      const auto &entry = select(archive, argv[i]);
      if (!replacements.emplace(entry.id, argv[i + 1]).second)
        throw std::runtime_error("Duplicate replacement: " +
                                 std::to_string(entry.id));
    }
    writePak(archive, staging.archive, replacements);
    requireNewFile(destination);
    fs::rename(staging.archive, destination);
    std::cout << "Repacked " << archive.fileCount() << " entries ("
              << replacements.size() << " replaced)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LucaPak: " << error.what() << '\n';
    return 1;
  }
}
