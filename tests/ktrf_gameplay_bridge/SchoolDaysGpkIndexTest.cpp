#include <Kotonoha/SchoolDaysGpkIndex.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysGpkIndexTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const std::string& message) {
    if (!value) Fail(message);
}
void U16(std::vector<uint8_t>* b, uint16_t v) {
    b->push_back(static_cast<uint8_t>(v));
    b->push_back(static_cast<uint8_t>(v >> 8u));
}
void U32(std::vector<uint8_t>* b, uint32_t v) {
    b->push_back(static_cast<uint8_t>(v));
    b->push_back(static_cast<uint8_t>(v >> 8u));
    b->push_back(static_cast<uint8_t>(v >> 16u));
    b->push_back(static_cast<uint8_t>(v >> 24u));
}
void Entry(std::vector<uint8_t>* b, const char16_t* name, uint32_t offset,
           uint32_t stored, const char magic[4], uint32_t uncompressed,
           std::initializer_list<uint8_t> prefix) {
    size_t count = 0u;
    while (name[count] != 0) ++count;
    U16(b, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) U16(b, static_cast<uint16_t>(name[i]));
    U16(b, 1); U16(b, 2); U16(b, 0); U32(b, offset); U32(b, stored);
    for (int i = 0; i < 4; ++i) b->push_back(static_cast<uint8_t>(magic[i]));
    U32(b, uncompressed);
    b->push_back(static_cast<uint8_t>(prefix.size()));
    b->insert(b->end(), prefix.begin(), prefix.end());
}

} // namespace

int main() {
    std::vector<uint8_t> decoded;
    const char raw[4] = {' ',' ',' ',' '};
    const char dflt[4] = {'D','F','L','T'};
    Entry(&decoded, u"A.PNG", 0x100u, 5u, raw, 5u, {0x89,0x50});
    // Physical body length is 5-2=3, so the next offset is 0x103.
    Entry(&decoded, u"B.INI", 0x103u, 4u, dflt, 9u, {0x78,0xDA});
    U16(&decoded, 0); U16(&decoded, 0);

    Kotonoha::SchoolDaysGpkIndex index;
    std::string error;
    Check(index.ParseDecoded(decoded.data(), decoded.size(), &error), error);
    Check(index.Entries().size() == 2u, "decoded PIDX entry count drifted");
    const auto& first = index.Entries()[0];
    const auto& second = index.Entries()[1];
    Check(first.filename == u"A.PNG" && first.IsRaw() &&
              first.PhysicalBodyLength() == 3u && first.inlinePrefix.size() == 2u,
          "raw PIDX entry framing drifted");
    Check(second.filename == u"B.INI" && second.IsDeflate() &&
              second.PhysicalBodyLength() == 2u,
          "DFLT PIDX entry framing drifted");

    const uint8_t physical[] = {1,2,3};
    std::vector<uint8_t> logical;
    Check(Kotonoha::SchoolDaysGpkIndex::ComposeStoredStream(
              first, physical, sizeof(physical), &logical, &error), error);
    Check(logical == std::vector<uint8_t>({0x89,0x50,1,2,3}),
          "inline PIDX prefix was not prepended to physical body");
    Check(!Kotonoha::SchoolDaysGpkIndex::ComposeStoredStream(
              first, physical, 2u, &logical, &error),
          "wrong physical body length was accepted");

    std::vector<uint8_t> badChain = decoded;
    const size_t firstRecordBytes = 2u + 10u + 23u + 2u;
    const size_t secondOffsetField = firstRecordBytes + 2u + 10u + 6u;
    badChain[secondOffsetField + 0u] = 0x05;
    badChain[secondOffsetField + 1u] = 0x01;
    badChain[secondOffsetField + 2u] = 0x00;
    badChain[secondOffsetField + 3u] = 0x00;
    Check(!index.ParseDecoded(badChain.data(), badChain.size(), &error),
          "overlapping PIDX offset chain was accepted");

    std::vector<uint8_t> badTerm = decoded;
    badTerm.back() = 1u;
    Check(!index.ParseDecoded(badTerm.data(), badTerm.size(), &error),
          "malformed retail four-zero terminator was accepted");

    std::cout << "SCHOOL DAYS GPK INDEX GATE PASS\n"
              << "uint16_utf16_filename=PASS\n"
              << "packed_23_byte_header=PASS\n"
              << "inline_prefix_body_split=PASS\n"
              << "physical_offset_chain=PASS\n"
              << "four_zero_terminator=PASS\n"
              << "no_zlib_dependency=PASS\n";
    return 0;
}
