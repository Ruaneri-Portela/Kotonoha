// Inspect a legitimately obtained, decrypted PS Vita Amagami game directory.
// Does not decrypt or package retail assets.
#include <Amagami/Formats.hpp>
#include <Amagami/VitaCpk.hpp>
#include <Amagami/VitaGraphics.hpp>
#include <Amagami/VitaSystemGraphics.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
Amagami::Bytes readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error("Cannot open " + path.string());
  const auto size = file.tellg();
  if (size < 0 || size > 64 * 1024 * 1024)
    throw std::runtime_error("Invalid system image size");
  Amagami::Bytes data(size_t(size), 0);
  file.seekg(0);
  if (!data.empty() && !file.read(reinterpret_cast<char *>(data.data()),
                                  std::streamsize(data.size())))
    throw std::runtime_error("Truncated " + path.string());
  return data;
}
bool hasSuffix(const std::string &name, const std::string &extension) {
  return name.size() >= extension.size() &&
         name.compare(name.size() - extension.size(), extension.size(),
                      extension) == 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 7) {
    std::cerr << "Usage: AmagamiVitaInspect DECRYPTED_GAME_DIRECTORY "
                 "[--verify] [--verify-graphics] [--verify-fonts] "
                 "[--verify-ui PLAIN_EBOOT_ELF]\n"
                 "Read-only inspection of a decrypted PCSG00291 copy.\n";
    return 2;
  }
  bool verify = false, verifyGraphics = false, verifyFonts = false;
  std::string uiElf;
  for (int i = 2; i < argc; ++i) {
    const std::string flag(argv[i]);
    if (flag == "--verify")
      verify = true;
    else if (flag == "--verify-graphics")
      verifyGraphics = true;
    else if (flag == "--verify-fonts")
      verifyFonts = true;
    else if (flag == "--verify-ui" && i + 1 < argc)
      uiElf = argv[++i];
    else {
      std::cerr << "Unsupported argument: " << flag << '\n';
      return 2;
    }
  }
  try {
    const std::filesystem::path root(argv[1]);
    const auto system = Amagami::ParseClassImage(
        Amagami::Unpack(readFile(root / "AMAGAMI.IMG")));
    const std::string cpk = (root / "SCENARIO.CPK").string();
    const auto index = Amagami::Vita::cpk_read_index(cpk);
    size_t scripts = 0, systemClasses = 0;
    if (verify)
      for (const auto &klass : system) {
        const auto parsed = Amagami::ParseScf(klass.bytes);
        if (parsed.name != klass.name)
          throw std::runtime_error("System class name mismatch: " + klass.name);
        ++systemClasses;
      }
    for (const auto &[name, entry] : index) {
      if (!hasSuffix(name, ".scf"))
        continue;
      ++scripts;
      if (!verify)
        continue;
      std::string content;
      if (!Amagami::Vita::cpk_read(cpk, index, name, content))
        throw std::runtime_error("Script not found: " + name);
      Amagami::Bytes bytes(content.begin(), content.end());
      const auto parsed = Amagami::ParseScf(bytes);
      if (parsed.name.empty())
        throw std::runtime_error("Unnamed SCF class: " + name);
    }
    if (!scripts || system.empty())
      throw std::runtime_error("Missing Vita scenario or system classes");
    std::cout << "Vita SCF system image: " << system.size()
              << " classes; SCENARIO.CPK: " << scripts << " scripts\n";
    if (verify)
      std::cout << "Successfully parsed " << systemClasses
                << " system classes and " << scripts << " scenario scripts\n";
    if (verifyGraphics) {
      const std::string graphicsPath = (root / "graph" / "GRAPH.CPK").string();
      const auto graphics = Amagami::Vita::cpk_read_index(graphicsPath);
      size_t decoded = 0;
      for (const auto &item : graphics) {
        if (!Amagami::Vita::pictureLayout(item.first))
          continue; // Font and packed system pictures need other decoders.
        std::string encoded;
        if (!Amagami::Vita::cpk_read(graphicsPath, graphics, item.first,
                                     encoded))
          throw std::runtime_error("Missing Vita graphic " + item.first);
        const auto picture =
            Amagami::Vita::decodeVitaPicture(item.first, encoded);
        if (picture.rgba.empty())
          throw std::runtime_error("Empty Vita graphic " + item.first);
        ++decoded;
      }
      if (!decoded)
        throw std::runtime_error("No recognised Vita graphics in GRAPH.CPK");
      std::cout << "Vita palette graphics decoded: " << decoded << '\n';
    }
    if (verifyFonts || !uiElf.empty()) {
      const auto graphicsPath = (root / "graph" / "GRAPH.CPK").string();
      const auto graphics = Amagami::Vita::cpk_read_index(graphicsPath);
      if (verifyFonts) {
        for (const char *name :
             {"font/AMV_font18_21.tm2", "font/AMV_font18_21im.tm2"}) {
          std::string bytes;
          if (!Amagami::Vita::cpk_read(graphicsPath, graphics, name, bytes))
            throw std::runtime_error(std::string("Vita font missing: ") + name);
          // Verify all four palettes with one widely present Shift-JIS glyph.
          for (unsigned palette = 0; palette < 4; ++palette) {
            const auto glyph =
                Amagami::Vita::decodeFontGlyph(bytes, 0x8140, palette);
            if (glyph.width != 36 || glyph.height != 42)
              throw std::runtime_error("Invalid Vita font glyph dimensions");
          }
        }
        std::cout << "Vita font atlases decoded: 2 (4 palettes each)\n";
      }
      if (!uiElf.empty()) {
        std::string packed;
        if (!Amagami::Vita::cpk_read(graphicsPath, graphics,
                                     "system/sysgraph.tm2", packed))
          throw std::runtime_error("Vita sysgraph not found");
        const auto elfData = readFile(uiElf);
        Amagami::Vita::UiAtlas atlas(
            std::string(elfData.begin(), elfData.end()), std::move(packed));
        size_t count = 0;
        for (uint32_t entry = 1; entry < 762; ++entry)
          if (atlas.decodeEntry(entry))
            ++count;
        std::cout << "Vita sysgraph UI textures decoded: " << count << '\n';
      }
    }
    std::cout << "Inspection only: Vita game execution is not implemented.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Amagami Vita inspection failed: " << error.what() << '\n';
    return 1;
  }
}
