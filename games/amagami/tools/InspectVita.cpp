// Inspect a legitimately obtained, decrypted PS Vita Amagami game directory.
// Does not decrypt or package retail assets.
#include <Amagami/Formats.hpp>
#include <Amagami/VitaCpk.hpp>
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
  if (argc < 2 || argc > 3 ||
      (argc == 3 && std::string(argv[2]) != "--verify")) {
    std::cerr << "Usage: AmagamiVitaInspect DECRYPTED_GAME_DIRECTORY [--verify]\n"
                 "This inspects a decrypted PCSG00291 copy; it cannot decrypt "
                 "or run the Vita game.\n";
    return 2;
  }
  try {
    const std::filesystem::path root(argv[1]);
    const auto system = Amagami::ParseClassImage(
        Amagami::Unpack(readFile(root / "AMAGAMI.IMG")));
    const std::string cpk = (root / "SCENARIO.CPK").string();
    const auto index = Amagami::Vita::cpk_read_index(cpk);
    size_t scripts = 0, systemClasses = 0;
    const bool verify = argc == 3;
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
    std::cout << "Inspection only: Vita game execution is not implemented.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Amagami Vita inspection failed: " << error.what() << '\n';
    return 1;
  }
}
