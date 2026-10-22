#include "Kotonoha/parsers/Ktrf.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int fail(const char *message) {
  std::fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

static bool expect_count(const Kotonoha_KtrfDocument &doc, const char type[4],
                         uint32_t expected) {
  const Kotonoha_KtrfSection *section = Kotonoha_KtrfFindSection(&doc, type);
  if (section == nullptr || section->item_count != expected) {
    std::fprintf(stderr, "FAIL: %.4s count expected=%u actual=%u\n", type,
                 expected, section ? section->item_count : 0u);
    return false;
  }
  return true;
}

static std::vector<uint8_t> read_all(const char *path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

int main(int argc, char **argv) {
  if (argc != 2)
    return fail("usage: KtrfNativeReaderTest <school-days-hq.ktnroute>");

  Kotonoha_KtrfDocument doc{};
  Kotonoha_KtrfError error{};
  Kotonoha_KtrfInit(&doc);
  if (!Kotonoha_KtrfLoadFile(argv[1], &doc, &error)) {
    std::fprintf(stderr, "FAIL: load: %s\n", error.message);
    return 1;
  }
  if (!Kotonoha_KtrfValidateFullCore(&doc, &error)) {
    std::fprintf(stderr, "FAIL: full-core: %s\n", error.message);
    Kotonoha_KtrfClean(&doc);
    return 1;
  }

  const struct Expected {
    const char type[5];
    uint32_t count;
  } expected[] = {{"META", 1},    {"NSPC", 1},    {"FEAT", 8},
                  {"ENTR", 1},    {"VARS", 497},  {"RSRC", 1855},
                  {"HOOK", 3},    {"ENDG", 22},   {"EXPR", 1892},
                  {"EFFT", 11356},{"CHOI", 287},  {"NODE", 1857},
                  {"TRAN", 2458}};
  for (const auto &item : expected) {
    if (!expect_count(doc, item.type, item.count)) {
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
  }

  const char *s = nullptr;
  size_t n = 0;
  if (!Kotonoha_KtrfGetString(&doc, 0, &s, &n, &error) || n != 0u) {
    Kotonoha_KtrfClean(&doc);
    return fail("STRS[0] is not accessible empty string");
  }
  for (uint32_t i = 0; i < doc.string_count; ++i) {
    if (!Kotonoha_KtrfGetString(&doc, i, &s, &n, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("failed to enumerate STRS");
    }
  }

  std::vector<uint8_t> bytes = read_all(argv[1]);
  if (bytes.size() != doc.size) {
    Kotonoha_KtrfClean(&doc);
    return fail("fixture re-read size mismatch");
  }
  Kotonoha_KtrfClean(&doc);

  if (bytes.size() <= KOTONOHA_KTRF_HEADER_SIZE)
    return fail("fixture unexpectedly small");
  bytes.back() ^= 0x01u;
  Kotonoha_KtrfDocument corrupted{};
  Kotonoha_KtrfInit(&corrupted);
  if (Kotonoha_KtrfLoadMemoryCopy(bytes.data(), bytes.size(), &corrupted,
                                  &error)) {
    Kotonoha_KtrfClean(&corrupted);
    return fail("payload corruption was accepted");
  }
  if (error.code != KOTONOHA_KTRF_ERROR_INTEGRITY)
    return fail("payload corruption did not produce integrity error");

  std::printf("KTRF NATIVE READER GATE 1 PASS\n");
  std::printf("fixture=%s\n", argv[1]);
  std::printf("sections=14 core (+ optional if present)\n");
  std::printf("school_days_counts=PASS\n");
  std::printf("corruption_rejection=PASS\n");
  return 0;
}
