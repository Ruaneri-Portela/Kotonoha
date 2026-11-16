// Inspect a legitimately obtained, decrypted PS Vita Amagami game directory.
// Does not decrypt or package retail assets.
#include <Amagami/Formats.hpp>
#include <Amagami/VitaCpk.hpp>
#include <Amagami/VitaGraphics.hpp>
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
  if (!data.empty() &&
      !file.read(reinterpret_cast<char *>(data.data()),
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
  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: AmagamiVitaInspect DECRYPTED_GAME_DIRECTORY "
                 "[--verify] [--verify-graphics]\n"
                 "Read-only inspection of a decrypted PCSG00291 copy.\n";
    return 2;
  }
  bool verify = false, verifyGraphics = false;
  for (int i = 2; i < argc; ++i) {
    const std::string flag(argv[i]);
    if (flag == "--verify")
      verify = true;
    else if (flag == "--verify-graphics")
      verifyGraphics = true;
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
          throw std::runtime_error("System class name mismatch: " +
                                   klass.name);
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
      const std::string graphicsPath =
          (root / "graph" / "GRAPH.CPK").string();
      const auto graphics = Amagami::Vita::cpk_read_index(graphicsPath);
      size_t decoded = 0;
      for (const auto &item : graphics) {
        if (!Amagami::Vita::pictureLayout(item.first))
          continue; // Font and packed system pictures need other decoders.
        std::string encoded;
        if (!Amagami::Vita::cpk_read(graphicsPath, graphics,
                                     item.first, encoded))
          throw std::runtime_error("Missing Vita graphic " + item.first);
        const auto picture = Amagami::Vita::decodeVitaPicture(
            item.first, encoded);
        if (picture.rgba.empty())
          throw std::runtime_error("Empty Vita graphic " + item.first);
        ++decoded;
      }
      if (!decoded)
        throw std::runtime_error("No recognised Vita graphics in GRAPH.CPK");
      std::cout << "Vita palette graphics decoded: " << decoded << '\n';
    }
    std::cout << "Inspection only: Vita game execution is not implemented.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Amagami Vita inspection failed: " << error.what() << '\n';
    return 1;
  }
}
