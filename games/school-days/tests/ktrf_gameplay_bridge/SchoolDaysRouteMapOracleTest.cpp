#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfProfile.hpp>

extern "C" {
#include <Kotonoha/parsers/KtrfTyped.h>
}

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Oracle = Kotonoha::SchoolDaysRouteMapOracle;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SCHOOL DAYS ROUTEMAP ORACLE GATE FAIL: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

void Put32(std::vector<uint8_t>* bytes, size_t offset, uint32_t value) {
    (*bytes)[offset + 0u] = static_cast<uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<uint8_t>(value >> 8u);
    (*bytes)[offset + 2u] = static_cast<uint8_t>(value >> 16u);
    (*bytes)[offset + 3u] = static_cast<uint8_t>(value >> 24u);
}

class Fixture {
public:
    Fixture() {
        const std::array<const char*, 4> strings = {
            "sdhq:var:session:ROUTE",
            "sdhq:var:session:SCENE",
            "ktrf:int32",
            "sdhq:scope:session",
        };

        stringOffsets_.resize((strings.size() + 1u) * 4u);
        uint32_t cursor = 0u;
        for (size_t i = 0; i < strings.size(); ++i) {
            Put32(&stringOffsets_, i * 4u, cursor);
            const char* text = strings[i];
            const size_t length = std::strlen(text);
            stringBlob_.insert(stringBlob_.end(), text, text + length);
            cursor += static_cast<uint32_t>(length);
        }
        Put32(&stringOffsets_, strings.size() * 4u, cursor);

        vars_.assign(16u + 2u * 32u, 0u);
        Put32(&vars_, 0u, 2u);   // count
        Put32(&vars_, 4u, 32u);  // record size
        Put32(&vars_, 8u, static_cast<uint32_t>(vars_.size()));
        Put32(&vars_, 12u, 0u);  // bytes blob size
        WriteVariable(0u, 0u);
        WriteVariable(1u, 1u);

        std::memcpy(section_.type, "VARS", 4u);
        section_.type[4] = '\0';
        section_.stored_size = vars_.size();
        section_.decoded_size = vars_.size();
        section_.item_count = 2u;
        section_.payload = vars_.data();

        document_.section_count = 1u;
        document_.sections = &section_;
        document_.string_count = static_cast<uint32_t>(strings.size());
        document_.string_offsets = stringOffsets_.data();
        document_.string_blob = stringBlob_.data();

        runtimeValues_[0].kind = KOTONOHA_KTRF_RUNTIME_INT64;
        runtimeValues_[1].kind = KOTONOHA_KTRF_RUNTIME_INT64;
        adapter_.document = &document_;
        adapter_.profile = &Kotonoha::SchoolDaysKtrfProfile();
        adapter_.router.document = &document_;
        adapter_.router.runtime.document = &document_;
        adapter_.router.runtime.variables = runtimeValues_.data();
        adapter_.router.runtime.variable_count =
            static_cast<uint32_t>(runtimeValues_.size());

        Kotonoha_KtrfError error{};
        Check(state.Bind(&adapter_, &error), error.message);
    }

    ~Fixture() { state.Unbind(); }

    void Coordinate(int route, int scene) {
        Kotonoha_KtrfError error{};
        Check(state.WriteSessionInt("ROUTE", route, &error), error.message);
        Check(state.WriteSessionInt("SCENE", scene, &error), error.message);
    }

    void Session(const std::string& key, int64_t value) {
        Kotonoha_KtrfError error{};
        Check(state.WriteSessionInt(key, value, &error), error.message);
    }

    void Global(const std::string& key, int64_t value) {
        Kotonoha_KtrfError error{};
        Check(state.WriteGlobalInt(key, value, &error), error.message);
    }

    Kotonoha::KtrfStateStore state;

private:
    void WriteVariable(uint32_t index, uint32_t idString) {
        const size_t offset = 16u + static_cast<size_t>(index) * 32u;
        Put32(&vars_, offset + 0u, idString);
        Put32(&vars_, offset + 4u, 2u); // ktrf:int32
        Put32(&vars_, offset + 8u, 3u); // scope string
        vars_[offset + 12u] = KOTONOHA_KTRF_VAR_INT32;
    }

    std::vector<uint8_t> stringOffsets_;
    std::vector<uint8_t> stringBlob_;
    std::vector<uint8_t> vars_;
    Kotonoha_KtrfSection section_{};
    Kotonoha_KtrfDocument document_{};
    Kotonoha_KtrfAdapter adapter_{};
    std::array<Kotonoha_KtrfRuntimeValue, 2> runtimeValues_{};
};

int CheckScript(Fixture& fixture, int major) {
    Kotonoha_KtrfError error{};
    const int result = Oracle::CheckScript(fixture.state, major, &error);
    Check(error.code == KOTONOHA_KTRF_OK, error.message);
    return result;
}

int Page(Fixture& fixture, int major) {
    Kotonoha_KtrfError error{};
    const int result = Oracle::GetRouteMapPage(fixture.state, major, &error);
    Check(error.code == KOTONOHA_KTRF_OK, error.message);
    return result;
}

void CheckPageCases(Fixture& f) {
    // One witness for every jump-table arm in _GetRouteMapPage@8.
    f.Coordinate(0, 1);   Check(Page(f, 0) == 0, "page case0 failed");
    f.Coordinate(4, 0);   Check(Page(f, 2) == 0, "page case1 equality failed");
    f.Coordinate(4, 1);   Check(Page(f, 2) == 1, "page case1 fallback failed");
    f.Coordinate(6, 1);   Check(Page(f, 3) == 1, "page case2 failed");
    f.Coordinate(10, 1);  Check(Page(f, 3) == 2, "page case3 failed");
    f.Coordinate(13, 1);  Check(Page(f, 3) == 3, "page case4 failed");
    f.Coordinate(15, 0);  Check(Page(f, 3) == 2, "page case5 equality failed");
    f.Coordinate(15, 1);  Check(Page(f, 3) == 3, "page case5 fallback failed");

    f.Coordinate(16, 1);
    f.Session("SP408", 1);
    Check(Page(f, 3) == 1, "page case6 equality failed");
    f.Session("SP408", 0);
    Check(Page(f, 3) == 0, "page case6 fallback failed");

    f.Coordinate(21, 1);
    f.Session("SP530", 1);
    Check(Page(f, 4) == 2, "page case7 equality failed");
    f.Session("SP530", 0);
    Check(Page(f, 4) == 3, "page case7 fallback failed");

    f.Coordinate(29, 35); Check(Page(f, 4) == 2, "page case8 equality failed");
    f.Coordinate(29, 1);  Check(Page(f, 4) == 3, "page case8 fallback failed");

    f.Coordinate(40, 1);
    f.Session("SP601", 1); Check(Page(f, 5) == 0, "page case9 value12 failed");
    f.Session("SP601", 0); f.Session("SP612", 1);
    Check(Page(f, 5) == 1, "page case9 value23 failed");
    f.Session("SP612", 0); f.Session("SP638", 1);
    Check(Page(f, 5) == 3, "page case9 value49 failed");
    f.Session("SP638", 0); Check(Page(f, 5) == 2, "page case9 default failed");

    f.Coordinate(49, 13); Check(Page(f, 5) == 3, "page case10 equality failed");
    f.Coordinate(49, 1);  Check(Page(f, 5) == 2, "page case10 fallback failed");
    f.Coordinate(51, 0);  Check(Page(f, 5) == 0, "page case11 equality failed");
    f.Coordinate(51, 2);  Check(Page(f, 5) == 1, "page case11 fallback failed");
}

void CheckStoryGateAndBounds(Fixture& f) {
    f.Coordinate(0, 0);
    Check(CheckScript(f, 0) == 11, "current-story CheckScript witness failed");
    Check(CheckScript(f, 1) == 0, "Story gate accepted wrong major");

    f.Coordinate(-1, 0);
    f.Global("EndClear", 0);
    Check(CheckScript(f, -1) == 0 && Page(f, -1) == 0,
          "negative ROUTE bound failed");
    f.Global("EndClear", 1);
    Check(CheckScript(f, 5) == 0, "EndClear out-of-range Story gate failed");

    f.Coordinate(55, 0);
    Check(CheckScript(f, 5) == 0 && Page(f, 5) == 0,
          "high ROUTE bound failed");
}

void CheckSceneOverride(Fixture& f) {
    f.Coordinate(26, 94);
    Check(CheckScript(f, 4) == 39, "ROUTE26/SCENE94 override failed");
    f.Coordinate(26, 91);
    Check(CheckScript(f, 4) == 0, "ROUTE26 non-override did not fall back");
}

void CheckHighestSpWins(Fixture& f) {
    struct Witness {
        int route;
        int scene;
        int major;
        const char* low;
        const char* high;
        int expected;
    };
    const Witness witnesses[] = {
        {0, 1, 0, "SP100", "SP103", 14},
        {1, 1, 1, "SP200", "SP208", 19},
        {4, 1, 2, "SP300", "SP310", 21},
        {6, 1, 3, "SP400", "SP434", 45},
        {19, 1, 4, "SP500", "SP556", 67},
        {32, 1, 5, "SP600", "SP644", 55},
    };
    for (const Witness& witness : witnesses) {
        f.Coordinate(witness.route, witness.scene);
        f.Session(witness.low, 1);
        f.Session(witness.high, 1);
        Check(CheckScript(f, witness.major) == witness.expected,
              std::string("highest-SP-wins failed for ") + witness.high);
        f.Session(witness.low, 0);
        f.Session(witness.high, 0);
    }
}

void CheckCompletionMathAndDomain() {
    Check(Oracle::CompletionPercentForReadCount(0) == 0,
          "completion 0/1825 did not truncate to 0");
    Check(Oracle::CompletionPercentForReadCount(912) == 49,
          "completion 912/1825 did not truncate to 49");
    Check(Oracle::CompletionPercentForReadCount(913) == 50,
          "completion 913/1825 did not truncate to 50");
    Check(Oracle::CompletionPercentForReadCount(1824) == 99,
          "completion 1824/1825 did not truncate to 99");
    Check(Oracle::CompletionPercentForReadCount(1825) == 100,
          "completion 1825/1825 did not reach 100");
    Check(Oracle::IsCompletionSceneKey("01/01-00-A00", 12u),
          "ordinary SceneKey was excluded from completion domain");
    Check(!Oracle::IsCompletionSceneKey("01/01-00-OP1", 12u),
          "OP1 SceneKey was included in completion domain");
}

} // namespace

int main() {
    Fixture fixture;
    CheckPageCases(fixture);
    CheckStoryGateAndBounds(fixture);
    CheckSceneOverride(fixture);
    CheckHighestSpWins(fixture);
    CheckCompletionMathAndDomain();

    std::cout << "SCHOOL DAYS ROUTEMAP ORACLE GATE PASS\n"
              << "page_cases_12=PASS\n"
              << "story_gate=PASS\n"
              << "route_bounds=PASS\n"
              << "scene_override=PASS\n"
              << "highest_sp_wins_6_groups=PASS\n";
    std::cout << "completion_0_912_913_1824_1825=PASS\n"
              << "completion_op1_excluded=PASS\n";
    return 0;
}
