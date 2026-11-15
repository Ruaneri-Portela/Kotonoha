#include "PmailPack.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void word(std::vector<uint8_t> &data, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  try {
    // Synthetic archive: a short uncompressed object, one raw 0x50-byte
    // index record, and the format's XOR-encoded 12-byte index footer.
    const std::vector<uint8_t> object{0x21, 0x22, 0x23, 0x24};
    std::vector<uint8_t> archive = object;
    std::vector<uint8_t> index(0x50, 0);
    const char name[] = "start.obj";
    std::memcpy(index.data() + 4, name, sizeof(name));
    index[0x48] = static_cast<std::uint8_t>(object.size());
    archive.insert(archive.end(), index.begin(), index.end());
    word(archive, static_cast<std::uint32_t>(index.size()) ^ 0xf0f0f0f0u);
    word(archive, 0xf0f0f0f0u);  // unknown raw index size: scan directly
    word(archive, 0xf0f0f0f0u);

    Kotonoha::PmailPort::PmailPack reader;
    require(reader.openBytes(archive, "data/pack/script.dat"),
            "PmailPack cannot index a virtual byte archive");
    require(reader.entries().size() == 1, "Wrong virtual pack entry count");
    require(reader.read("start.obj") == object,
            "PmailPack cannot extract an object without a physical path");
    std::cout << "Pmail VFS byte pack test passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Pmail VFS byte pack test failed: " << error.what() << '\n';
    return 1;
  }
}
