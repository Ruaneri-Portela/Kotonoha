#include <Kotonoha/routing/SchoolDaysCmap.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysCmapTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

} // namespace

int main(int argc, char** argv) {
    std::string error;
    Kotonoha::SchoolDaysCmap map;

    // width=3, height=2, direct native byte ids (including values whose high
    // bit is set), plus arbitrary trailing bytes. The authenticated parser
    // consumes only the six map bytes and zero-extends each byte to uint16.
    const std::vector<uint8_t> fixture = {
        3, 0, 0, 0, 2, 0, 0, 0,
        0, 1, 2, 0x7f, 0x80, 0xff,
        'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    Check(map.Parse(fixture.data(), fixture.size(), &error),
          "synthetic CMAP parse failed");
    Check(map.Width() == 3 && map.Height() == 2,
          "synthetic CMAP dimensions mismatch");
    Check(map.Get(0, 0) == 0 && map.Get(2, 0) == 2 &&
              map.Get(0, 1) == 0x7f && map.Get(1, 1) == 0x80 &&
              map.Get(2, 1) == 0xff,
          "native row-major region lookup mismatch");
    Check(map.Get(-1, 0) == -1 && map.Get(0, -1) == -1 &&
              map.Get(3, 1) == -1 && map.Get(0, 2) == -1,
          "out-of-range CMAP lookup did not return -1");

    // An exact header+payload with no suffix is valid too.
    std::vector<uint8_t> exact(fixture.begin(), fixture.begin() + 14);
    Check(map.Parse(exact.data(), exact.size(), &error),
          "exact header+payload CMAP failed to parse");
    Check(map.Get(2, 1) == 0xff,
          "exact payload lookup changed after dropping trailing bytes");

    std::vector<uint8_t> truncated(fixture.begin(), fixture.begin() + 13);
    Check(!map.Parse(truncated.data(), truncated.size(), &error),
          "truncated CMAP unexpectedly parsed");
    Check(map.Empty() && map.Width() == 0 && map.Height() == 0,
          "failed parse retained stale CMAP state");
    const std::vector<uint8_t> shortHeader = {1, 0, 0, 0, 1, 0, 0};
    Check(!map.Parse(shortHeader.data(), shortHeader.size(), &error),
          "short CMAP dimension header unexpectedly parsed");

    const std::vector<uint8_t> zeroWidth = {0, 0, 0, 0, 1, 0, 0, 0};
    Check(!map.Parse(zeroWidth.data(), zeroWidth.size(), &error),
          "zero-width CMAP unexpectedly parsed");
    const std::vector<uint8_t> negativeWidth = {
        0xff, 0xff, 0xff, 0xff, 1, 0, 0, 0
    };
    Check(!map.Parse(negativeWidth.data(), negativeWidth.size(), &error),
          "negative-width int32LE CMAP unexpectedly parsed");
    const std::vector<uint8_t> negativeHeight = {
        1, 0, 0, 0, 0x00, 0x00, 0x00, 0x80
    };
    Check(!map.Parse(negativeHeight.data(), negativeHeight.size(), &error),
          "negative-height int32LE CMAP unexpectedly parsed");
    const std::vector<uint8_t> hugeDimensions = {
        0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f
    };
    Check(!map.Parse(hugeDimensions.data(), hugeDimensions.size(), &error),
          "overflow/truncated huge CMAP unexpectedly parsed");

    if (argc == 2) {
        const std::string root = argv[1];
        const std::string slash = !root.empty() &&
                                         (root.back() == '/' || root.back() == '\\')
                                     ? ""
                                     : "/";
        const std::string path =
            root + slash + "System/SaveLoad/Pop_Save_Feel.cmap";
        if (!map.LoadFile(path, &error)) Fail(error);
        Check(map.Width() == 800 && map.Height() == 600,
              "authenticated SaveFeel CMAP dimensions drifted");
        const auto [minimum, maximum] =
            std::minmax_element(map.Regions().begin(), map.Regions().end());
        Check(minimum != map.Regions().end() && *minimum == 0u && *maximum == 2u,
              "authenticated SaveFeel CMAP region-id domain drifted");
        bool sawOne = false;
        bool sawTwo = false;
        for (uint16_t region : map.Regions()) {
            sawOne = sawOne || region == 1u;
            sawTwo = sawTwo || region == 2u;
        }
        Check(sawOne && sawTwo,
              "authenticated SaveFeel CMAP lacks one of its two choices");
        std::cout << "authenticated_savefeel_cmap=PASS\n";
    }

    std::cout << "SCHOOL DAYS CMAP GATE PASS\n"
              << "header_width_height=PASS\n"
              << "byte_to_u16_region_map=PASS\n"
              << "row_major_hit_test=PASS\n"
              << "trailing_bytes_accepted=PASS\n"
              << "nonpositive_dimensions_rejected=PASS\n"
              << "overflow_or_truncation_rejected=PASS\n"
              << "truncation_rejected=PASS\n";
    return 0;
}
