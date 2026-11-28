#include <Amagami/VitaCpk.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::string;
void check(bool ok, const char *description) {
  if (!ok)
    throw std::runtime_error(description);
}
template <typename F> void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Expected malformed CPK to be rejected");
}
void be16(Bytes &b, uint16_t x) {
  b += char(x >> 8);
  b += char(x);
}
void be32(Bytes &b, uint32_t x) {
  be16(b, uint16_t(x >> 16));
  be16(b, uint16_t(x));
}
void be64(Bytes &b, uint64_t x) {
  be32(b, uint32_t(x >> 32));
  be32(b, uint32_t(x));
}
void le64(Bytes &b, uint64_t x) {
  for (unsigned i = 0; i < 8; ++i)
    b += char(x >> (8 * i));
}
Bytes utf(Bytes table) {
  Bytes output = "@UTF";
  be32(output, uint32_t(table.size()));
  output += table;
  return output;
}
Bytes simpleUtf() {
  Bytes table;
  be32(table, 29);     // rows offset: 24-byte header + 5-byte schema
  be32(table, 33);     // strings offset: one four-byte row
  be32(table, 40);     // data offset
  be32(table, 0);      // table name offset
  be16(table, 1);      // one named column
  be16(table, 4);      // four bytes per row
  be32(table, 1);      // one row
  table += char(0x54); // named + per-row + unsigned 32-bit integer
  be32(table, 0);
  be32(table, 42);
  table += Bytes("Number\0", 7);
  return utf(table);
}
Bytes headerUtf() {
  Bytes table;
  be32(table, 50); // two 13-byte default-value column schemas
  be32(table, 50); // no per-row payload
  be32(table, 74); // data section after strings
  be32(table, 0);
  be16(table, 2);
  be16(table, 0);
  be32(table, 1);
  table += char(0x36); // named + default + u64
  be32(table, 0);
  be64(table, 128); // TocOffset
  table += char(0x36);
  be32(table, 10);
  be64(table, 256); // ContentOffset
  table += Bytes("TocOffset\0ContentOffset\0", 24);
  return utf(table);
}
Bytes tocUtf() {
  Bytes table;
  be32(table, 39); // 24-byte header + 3 named-column schemas
  be32(table, 55); // 16-byte row
  be32(table, 93); // after string area
  be32(table, 0);
  be16(table, 3);
  be16(table, 16);
  be32(table, 1);
  table += char(0x5a); // named, per-row, string
  be32(table, 0);
  table += char(0x56); // named, per-row, u64
  be32(table, 9);
  table += char(0x54); // named, per-row, u32
  be32(table, 20);
  be32(table, 29);  // FileName string offset
  be64(table, 128); // FileOffset relative to min(TocOffset, ContentOffset)
  be32(table, 4);   // FileSize
  table += Bytes("FileName\0FileOffset\0FileSize\0test.scf\0", 38);
  return utf(table);
}
Bytes packet(const char *magic, const Bytes &data) {
  Bytes bytes(magic, 4);
  bytes += Bytes(4, '\0');
  le64(bytes, data.size());
  bytes += data;
  return bytes;
}
} // namespace

int main() {
  try {
    using namespace Amagami::Vita;
    auto table = cpk_parse_utf(simpleUtf());
    check(table.rows.size() == 1 && table.rows[0]["Number"].i == 42,
          "Reading a valid UTF row");
    auto masked = simpleUtf();
    uint32_t mask = 0x655F;
    for (char &byte : masked) {
      byte = char(uint8_t(byte) ^ uint8_t(mask));
      mask *= 0x4115;
    }
    check(cpk_parse_utf(masked).rows[0]["Number"].i == 42,
          "Reading a masked UTF row");
    rejects([] { cpk_parse_utf(""); });
    auto malformed = simpleUtf();
    malformed[8] = 0;
    malformed[9] = 0;
    malformed[10] = 0;
    malformed[11] = 28; // wrong schema boundary
    rejects([&] { cpk_parse_utf(malformed); });
    auto badRow = simpleUtf();
    badRow[8 + 19] = 8; // advertised row length differs from actual columns
    rejects([&] { cpk_parse_utf(badRow); });
    auto tooManyRows = simpleUtf();
    tooManyRows[8 + 20] = char(255);
    tooManyRows[8 + 21] = char(255);
    tooManyRows[8 + 22] = char(255);
    tooManyRows[8 + 23] = char(255);
    rejects([&] { cpk_parse_utf(tooManyRows); });
    check(cpk_decompress_crilayla("uncompressed") == "uncompressed",
          "Uncompressed CPK entries pass through");
    Bytes truncated = "CRILAYLA";
    truncated.resize(16, '\0');
    rejects([&] { cpk_decompress_crilayla(truncated); });

    auto filePath =
        std::filesystem::temp_directory_path() /
        ("kotonoha-vita-cpk-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".cpk");
    const auto header = packet("CPK ", headerUtf());
    const auto toc = packet("TOC ", tocUtf());
    check(header.size() <= 128 && toc.size() <= 128,
          "Synthetic metadata fits aligned CPK layout");
    Bytes cpk = header + Bytes(128 - header.size(), '\0') + toc;
    cpk += Bytes(256 - cpk.size(), '\0');
    cpk += "SCF\x1a";
    {
      std::ofstream out(filePath, std::ios::binary);
      check(bool(out), "Create synthetic CPK");
      out.write(cpk.data(), std::streamsize(cpk.size()));
    }
    const auto index = cpk_read_index(filePath.string());
    check(index.size() == 1 && index.at("test.scf").offset == 256 &&
              index.at("test.scf").size == 4,
          "Resolve Vita CPK TOC relative offsets");
    const auto entries = cpk_read_entries(filePath.string());
    check(entries.size() == 1 && entries[0].offset == 256,
          "Enumerate CPK entries");
    Bytes raw;
    check(cpk_read(filePath.string(), index, "test.scf", raw) &&
              raw == Bytes("SCF\x1a", 4),
          "Read CPK source entry");
    check(!cpk_read(filePath.string(), index, "not-found", raw),
          "Missing CPK member should not throw");
    auto wrongExpanded = index;
    wrongExpanded.at("test.scf").extract_size = 5;
    rejects(
        [&] { cpk_read(filePath.string(), wrongExpanded, "test.scf", raw); });
    auto wrongOffset = index;
    wrongOffset.at("test.scf").offset = UINT64_MAX;
    rejects(
        [&] { cpk_read(filePath.string(), wrongOffset, "test.scf", raw); });
    {
      auto truncated = filePath;
      truncated += ".truncated";
      std::ofstream out(truncated, std::ios::binary);
      out.write(cpk.data(), 256); // only headers; no graphic/script payload
      out.close();
      rejects([&] { cpk_read_index(truncated.string()); });
      rejects([&] { cpk_read_entries(truncated.string()); });
      std::filesystem::remove(truncated);
    }
    {
      auto badOffset = filePath;
      badOffset += ".bad-offset";
      auto corrupt = cpk;
      // TOC row: 128-byte packet base, 16-byte packet prefix, 8-byte
      // UTF prefix, 39-byte table header/schema, 4-byte filename field.
      const size_t offsetField = 128 + 16 + 8 + 39 + 4;
      for (size_t k = 0; k < 8; ++k)
        corrupt[offsetField + k] = char(255);
      std::ofstream out(badOffset, std::ios::binary);
      out.write(corrupt.data(), std::streamsize(corrupt.size()));
      out.close();
      rejects([&] { cpk_read_index(badOffset.string()); });
      std::filesystem::remove(badOffset);
    }
    std::filesystem::remove(filePath);
    std::cout << "Vita CPK provider tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
