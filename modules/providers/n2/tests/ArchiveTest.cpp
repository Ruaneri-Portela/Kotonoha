// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/Archive.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool condition,
             const char *message = "archive verification failed") {
  if (!condition)
    throw std::runtime_error(message);
}
void put32(Bytes &d, std::uint32_t v) {
  for (int j = 0; j < 4; ++j)
    d.push_back(static_cast<std::uint8_t>(v >> (8 * j)));
}
void set32(Bytes &d, std::size_t p, std::uint32_t v) {
  for (int j = 0; j < 4; ++j)
    d.at(p + j) = static_cast<std::uint8_t>(v >> (8 * j));
}
void add(Bytes &d, const std::string &s) {
  d.insert(d.end(), s.begin(), s.end());
}
template <typename F> void rejects(F fn) {
  bool failed = false;
  try {
    fn();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed);
}
Bytes afs() {
  Bytes d = {'A', 'F', 'S', 0};
  put32(d, 2);
  put32(d, 124);
  put32(d, 3);
  put32(d, 127);
  put32(d, 4);
  put32(d, 28);
  d.resize(124);
  const std::string n1 = "first.dat", n2 = "SECOND.DAT";
  for (std::size_t i = 0; i < n1.size(); ++i)
    d[28 + i] = n1[i];
  for (std::size_t i = 0; i < n2.size(); ++i)
    d[76 + i] = n2[i];
  add(d, "oneTWO!");
  return d;
}
std::uint8_t crypt(std::uint32_t ch, std::uint32_t entry, std::uint32_t first,
                   std::uint32_t second) {
  const std::uint32_t keys = first * second;
  std::uint32_t v = 0xfcu * ch;
  for (int shift : {24, 16, 8, 0})
    v -= shift ? keys >> shift : keys & 255u;
  for (int shift : {24, 16, 8, 0})
    v -= shift ? entry >> shift : entry;
  return static_cast<std::uint8_t>(v);
}
Bytes npa() {
  Bytes d = {'N', 'P', 'A', 1, 0, 0, 0};
  const std::uint32_t first = 7, second = 13;
  put32(d, first);
  put32(d, second);
  d.push_back(0);
  d.push_back(0);
  put32(d, 1);
  put32(d, 0);
  put32(d, 1);
  for (int j = 0; j < 8; ++j)
    d.push_back(0);
  put32(d, 0);
  const std::string name = "DIR\\Sample.TXT";
  put32(d, static_cast<std::uint32_t>(name.size()));
  for (std::size_t i = 0; i < name.size(); ++i)
    d.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(name[i]) -
                                          crypt(i, 0, first, second)));
  d.push_back(2);
  put32(d, 9);
  put32(d, 0);
  put32(d, 4);
  put32(d, 4);
  set32(d, 37, static_cast<std::uint32_t>(d.size() - 41));
  add(d, "GOOD");
  return d;
}
} // namespace

int main() {
  const auto a = afs();
  N2System::Archive archive(a);
  require(archive.format() == N2System::ArchiveFormat::AFS);
  require(archive.entries().size() == 2);
  require(archive.contains("FIRST.DAT"));
  require(archive.read("first.dat") == Bytes({'o', 'n', 'e'}));
  require(archive.read("second.dat") == Bytes({'T', 'W', 'O', '!'}));
  std::uint8_t chunk[4] = {};
  const auto *first = archive.find("first.dat");
  require(first != nullptr);
  require(archive.readAt(*first, 1, chunk, sizeof(chunk)) == 2);
  require(chunk[0] == 'n' && chunk[1] == 'e');
  require(archive.readAt(*first, 3, chunk, sizeof(chunk)) == 0);
  require(archive.readAt(*first, 999, chunk, sizeof(chunk)) == 0);
  rejects([&] { (void)archive.readAt(*first, 0, nullptr, 1); });
  archive.addAfsNames("2\nscenes/first.ks,0\r\nvoice/track.ogg,1\n");
  require(archive.contains("SCENES/FIRST.KS"), "AFS alias lookup failed");
  require(archive.read("scenes/first.ks") == Bytes({'o', 'n', 'e'}),
          "AFS alias pointed to incorrect entry");
  require(archive.contains("VOICE/track.ogg"),
          "AFS second filename alias missing");
  require(archive.names().size() == 4,
          "AFS original and alias names not listed");
  rejects([&] { archive.addAfsNames("1\n../escape,0\n"); });
  rejects([&] { archive.addAfsNames("1\nscenes/../escape,0\n"); });
  rejects([&] { archive.addAfsNames("1\nscenes/./escape,0\n"); });
  rejects([&] { archive.addAfsNames("1\nscenes//escape,0\n"); });
  rejects([&] { archive.addAfsNames("1\nscenes\\\\escape,0\n"); });
  rejects([&] { archive.addAfsNames("1\nC:/escape,0\n"); });
  rejects([&] {
    archive.addAfsNames(std::string("1\nvoice/") + std::string(1, '\0') +
                        "track,0\n");
  });
  require(!archive.contains("scenes/../escape") &&
              !archive.contains("scenes//escape"),
          "unsafe AFS alias must never enter the archive index");
  rejects([&] { archive.addAfsNames("1\nscenes/first.ks,1\n"); });
  rejects([&] { archive.addAfsNames("1\ninvalid,999999\n"); });
  require(!archive.contains("invalid"), "bad AFS INI partially mutated names");
  auto badAfs = a;
  set32(badAfs, 8, 99999);
  rejects([&] { N2System::Archive invalid(badAfs); });
  badAfs = a;
  set32(badAfs, 24, 99999);
  rejects([&] { N2System::Archive invalid(badAfs); });
  badAfs = a;
  set32(badAfs, 4, 0xffffffff);
  rejects([&] { N2System::Archive invalid(badAfs); });

  // The same path validation also applies to names decoded from NPA data,
  // not just external AFS filename tables. The name offset begins at byte
  // 45, with the per-byte NPA encoding intact.
  const auto n = npa();
  N2System::Archive packed(n);
  require(packed.format() == N2System::ArchiveFormat::NPA);
  require(packed.entries().size() == 1);
  require(packed.contains("dir/sample.txt"));
  require(packed.read("DIR\\Sample.TXT") == Bytes({'G', 'O', 'O', 'D'}));
  auto badNpa = n;
  badNpa[15] = 1;
  rejects([&] { N2System::Archive invalid(badNpa); });
  badNpa = n;
  badNpa[16] = 1;
  rejects([&] { N2System::Archive invalid(badNpa); });
  badNpa = n;
  set32(badNpa, 25, 2);
  rejects([&] { N2System::Archive invalid(badNpa); });
  badNpa = n;
  set32(badNpa, 37, 999999);
  rejects([&] { N2System::Archive invalid(badNpa); });
  badNpa = n;
  badNpa.pop_back();
  rejects([&] { N2System::Archive invalid(badNpa); });
  for (const std::string bad :
       {"DIR/../FILE.TXT", "DIR/./FILE.TXT", "DIR//FILE.TXT", "C:/FILE.TXT"}) {
    auto broken = npa();
    // Keep the NPA record length constant; malformed name contents are
    // decoded with the same per-byte key as ordinary filenames.
    const std::size_t nameLength = broken[41];
    const std::string candidate =
        bad.size() <= nameLength
            ? bad + std::string(nameLength - bad.size(), 'x')
            : bad.substr(0, nameLength);
    for (std::size_t j = 0; j < nameLength; ++j)
      broken[45 + j] = static_cast<std::uint8_t>(
          static_cast<std::uint8_t>(candidate[j]) -
          crypt(static_cast<std::uint32_t>(j), 0, 7, 13));
    rejects([&] { N2System::Archive invalid(broken); });
  }
  std::cout << "N2System AFS/NPA archive tests passed\n";

  // Steins;Gate SG-NPA format test (UTF-16LE filenames + O(1) seekable XOR).
  {
    const std::uint8_t sgKey[8] = {0xBD, 0xAA, 0xBC, 0xB4,
                                   0xAB, 0xB6, 0xBC, 0xB4};
    auto put64 = [](Bytes &d, std::uint64_t v) {
      for (int j = 0; j < 8; ++j)
        d.push_back(static_cast<std::uint8_t>(v >> (8 * j)));
    };
    auto utf16le = [](std::string_view ascii) {
      Bytes out;
      for (unsigned char c : ascii) {
        out.push_back(c);
        out.push_back(0);
      }
      return out;
    };

    const Bytes nameBytes = utf16le("NSS\\0_Boot.nsb");
    const std::string payload = "Hello, Steins;Gate streaming XOR payload!";

    Bytes hdr;
    put32(hdr, 1);
    put32(hdr, static_cast<std::uint32_t>(nameBytes.size()));
    hdr.insert(hdr.end(), nameBytes.begin(), nameBytes.end());
    put32(hdr, static_cast<std::uint32_t>(payload.size()));
    const std::uint64_t dataOffset = 4 + hdr.size() + 8;
    put64(hdr, dataOffset);

    Bytes encryptedHdr = hdr;
    for (std::size_t i = 0; i < encryptedHdr.size(); ++i)
      encryptedHdr[i] ^= sgKey[i & 7u];

    Bytes sgArchive;
    put32(sgArchive, static_cast<std::uint32_t>(encryptedHdr.size()));
    sgArchive.insert(sgArchive.end(), encryptedHdr.begin(), encryptedHdr.end());
    for (std::size_t i = 0; i < payload.size(); ++i)
      sgArchive.push_back(static_cast<std::uint8_t>(payload[i]) ^
                          sgKey[i & 7u]);

    N2System::Archive sg(sgArchive);
    require(sg.format() == N2System::ArchiveFormat::SG_NPA);
    require(sg.entries().size() == 1);
    require(sg.contains("nss/0_boot.nsb"));
    const auto full = sg.read("NSS/0_BOOT.NSB");
    require(std::string(full.begin(), full.end()) == payload);

    // Verify O(1) seekable readAt starting at an unaligned offset (offset = 7).
    const auto *entry = sg.find("nss/0_boot.nsb");
    require(entry != nullptr);
    std::uint8_t slice[11] = {};
    const auto got = sg.readAt(*entry, 7, slice, sizeof(slice));
    require(got == sizeof(slice));
    require(std::string(reinterpret_cast<const char *>(slice), got) ==
            payload.substr(7, sizeof(slice)));
  }
  std::cout << "N2System SG-NPA archive tests passed\n";
}
