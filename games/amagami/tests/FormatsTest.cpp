#include <Amagami/Formats.hpp>
#include <Kotonoha/Formats/Ps2Tim2.hpp>
#include <iostream>
#include <stdexcept>
using namespace Amagami;
void check(bool condition) {
  if (!condition)
    throw std::runtime_error("Check failed");
}
template <class F> void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
}
void u16(Bytes &b, uint16_t n) {
  b.push_back(n);
  b.push_back(n >> 8);
}
void u32(Bytes &b, uint32_t n) {
  u16(b, n);
  u16(b, n >> 16);
}
void str(Bytes &b, const std::string &s) {
  u16(b, s.size());
  b.insert(b.end(), s.begin(), s.end());
}
int main() {
  try {
    Bytes packed;
    u32(packed, 7);
    packed.insert(packed.end(), {7, 'A', 'B', 'C', 0xde, 0xe1});
    check(Unpack(packed) == Bytes({'A', 'B', 'C', 'A', 'B', 'C', 'A'}));
    rejects([&] { Unpack(Bytes{1, 0, 0, 0, 0}); });
    rejects([&] { Unpack(packed, 3); });
    Bytes arc;
    u32(arc, 1);
    u32(arc, 28);
    u16(arc, 1); // bucket 0 has the only entry
    u16(arc, 0); // first entry of bucket 0
    u32(arc, 1);
    u32(arc, 0x1234);
    u32(arc, 28);
    u32(arc, 4);
    auto a = ParseArc(arc, 32);
    check(a.size() == 1 && a[0].hash == 0x1234 && a[0].offset == 28);
    rejects([&] { ParseArc(arc, 31); });
    arc[4] = 29;
    rejects([&] { ParseArc(arc, 32); });

    // ARC buckets are grouped by hash % count, not payload order.
    Bytes grouped;
    u32(grouped, 3);
    u32(grouped, 68); // 8 + 20 * 3
    u16(grouped, 2);  // bucket 0: hashes 0 and 3
    u16(grouped, 1);  // bucket 1: hash 1
    u16(grouped, 0);  // bucket 2: empty
    u16(grouped, 0);  // first index for bucket 0
    u16(grouped, 2);  // first index for bucket 1
    u16(grouped, 0);  // empty buckets have first index zero
    for (uint32_t hash : {0u, 3u, 1u}) {
      u32(grouped, 1);
      u32(grouped, hash);
      u32(grouped, 68 + hash % 3);
      u32(grouped, 1);
    }
    check(ParseArc(grouped, 71).size() == 3);
    {
      auto bad = grouped;
      bad[8] = 1; // bucket count sum is now incomplete
      rejects([&] { ParseArc(bad, 71); });
    }
    {
      auto bad = grouped;
      bad[14] = 1; // bucket 0 must start at entry zero
      rejects([&] { ParseArc(bad, 71); });
    }
    {
      auto bad = grouped;
      bad[18] = 2; // empty bucket must use first index zero
      rejects([&] { ParseArc(bad, 71); });
    }
    {
      auto bad = grouped;
      bad[8 + 12 + 16 + 4] = 2; // second hash is in the wrong bucket
      rejects([&] { ParseArc(bad, 71); });
    }
    Bytes afs{'A', 'F', 'S', 0};
    u32(afs, 1);
    u32(afs, 16);
    u32(afs, 4);
    check(ParseAfs(afs, 20).at(0).size == 4);
    rejects([&] { ParseAfs(afs, 19); });
    Bytes scf{'S', 'C', 'F', 0x1a, 0, 4};
    str(scf, "Synthetic");
    str(scf, "Object");
    u16(scf, 0);
    u16(scf, 0);
    u16(scf, 1);
    str(scf, "scriptMain");
    u16(scf, 0);
    u16(scf, 1);
    scf.push_back(2);
    u16(scf, 0);
    u16(scf, 1);
    scf.push_back(Value::Array);
    u16(scf, 1);
    scf.push_back(Value::String);
    str(scf, "hello");
    auto s = ParseScf(scf);
    check(s.name == "Synthetic" && s.methods.size() == 1 &&
          s.texts() == std::vector<std::string>{"hello"});
    for (size_t i = 0; i < scf.size(); ++i)
      rejects([&] { ParseScf(Bytes(scf.begin(), scf.begin() + i)); });
    scf.push_back(0);
    rejects([&] { ParseScf(scf); });
    Kotonoha::Ps2Image image;
    check(!Kotonoha::DecodePs2Tim2(nullptr, 0, &image));
    Bytes tim(64);
    tim[0] = 'T';
    tim[1] = 'I';
    tim[2] = 'M';
    tim[3] = '2';
    tim[28] = 48;
    tim[30] = 0;
    tim[35] = 5;
    tim[36] = 1;
    tim[38] = 1;
    check(!Kotonoha::DecodePs2Tim2(tim.data(), tim.size(), &image));
    std::cout << "Amagami formats passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
