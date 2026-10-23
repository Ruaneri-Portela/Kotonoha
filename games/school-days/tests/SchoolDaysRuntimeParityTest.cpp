#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/SchoolDaysRemainingOrs.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Fixture {
    fs::path path;
    Kotonoha_orsData events{};
    explicit Fixture(const char* text) {
        path = fs::temp_directory_path() /
            ("kotonoha_g2_7_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".ors");
        { std::ofstream file(path, std::ios::binary); Check(bool(file), "fixture file"); file << text; }
        events = Kotonoha_OrsParser(path.string().c_str());
    }
    ~Fixture() {
        Kotonoha_OrsClean(&events);
        std::error_code error;
        fs::remove(path, error);
    }
};

constexpr const char* kMixedScene =
    "[CreateBG]=00:00:00\tBGS\tEvent00/base\t00:03:07;\n"
    "[PlayBgm]=00:00:05\tBGM/normal\t00:03:07;\n"
    "[PrintText]=00:00:10\tMakoto\tLine\t00:00:15;\n"
    "[PlayVoice]=00:00:10\tVoice00/line\t1\tmak\t00:00:15;\n"
    "[PlaySe]=00:00:12\t2\tSe00/hit\t00:00:18;\n"
    "[PlayMovie]=00:00:20\tEvent00/clip\t0\t00:01:15;\n"
    "[BlackFade]=00:01:06\t0\t00:01:10;\n"
    "[WhiteFade]=00:01:11\t1\t00:01:15;\n"
    "[SkipFRAME]=00:01:16;\n"
    "[SetSELECT]=00:01:16\t'First'\tnull\t00:03:03;\n"
    "[MoveSom]=00:01:21\t3\t00:02:01;\n"
    "[EndBGM]=00:02:02\tBGM/ending\t00:02:07;\n"
    "[EndRoll]=00:02:12\tEvent00/credits\t00:02:22;\n"
    "[Next]=00:03:08;\n";

void TestMixedDispatch() {
    using namespace Kotonoha;
    Fixture fixture(kMixedScene);
    Check(fixture.events.size == 14, "mixed fixture must cover all 14 headers");
    std::vector<Kotonoha_orsEvent*> textOrder;
    for (auto* event = fixture.events.data; event; event = event->next) {
        Check(ClassifySchoolDaysOrs(event->command) != SchoolDaysOrsClass::Unknown,
              "accepted header has no runtime class");
        textOrder.push_back(event);
    }
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&fixture.events);
    Check(timeline.SkipFrameTick() == 40 && timeline.NextTick() == 80,
          "mixed marker separation");
    SchoolDaysMovieEventState movie = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::PlayMovie, "Event00/clip", 20, 39);
    movie.Prepare(true);
    Check(movie.prepared && !movie.active, "movie prepare activated early");
    Check(!movie.Activate(19) && !movie.active, "movie activated before START");
    std::vector<Kotonoha_orsEvent*> dense;
    SchoolDaysMoveSomNoOp moveSom;
    for (Kotonoha_SceneTick tick = 0; tick <= 80; ++tick) {
        for (auto* event : timeline.AdvanceTo(tick)) {
            dense.push_back(event);
            if (event->command == MOVE_SOM) moveSom.Start(event);
            if (event->command == PLAY_MOVIE) Check(movie.Activate(tick), "movie START");
        }
        moveSom.AdvanceTo(tick);
        if (tick == 40) {
            Check(!timeline.ReadyToEnd(tick) &&
                  !SchoolDaysChoiceShouldSubmit(false, -2),
                  "SkipFRAME / pending choice ended scene or invented answer");
        }
        if (tick == 39) {
            movie.MarkEof();
            movie.AdvanceTo(tick);
            Check(!timeline.ReadyToEnd(tick), "movie EOF or END caused routing");
        }
        if (tick == 70) Check(!timeline.ReadyToEnd(tick), "EndRoll END caused routing");
    }
    Check(dense == textOrder && dense[2]->command == PRINT_TEXT &&
          dense[3]->command == PLAY_VOICE, "dense dispatch lost source order");
    Check(timeline.ReadyToEnd(80) &&
          !moveSom.Active(dense[10]), "Next or MoveSom cleanup failed");

    timeline.Restart();
    auto jumped = timeline.AdvanceTo(80);
    Check(jumped == dense, "0-to-80 jump differs from dense dispatch");
    timeline.Restart();
    auto at0 = timeline.AdvanceTo(0);
    auto rest = timeline.AdvanceTo(80);
    Check(at0.size() == 1 && at0[0] == dense[0] &&
          rest.size() == 13 && rest.front() == dense[1] &&
          rest.back() == dense.back(), "stepped/jumped scheduler difference");
}

void TestWindowsResetAndSeek() {
    using namespace Kotonoha;
    Fixture fixture(kMixedScene);
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&fixture.events);
    for (auto* event = fixture.events.data; event; event = event->next) {
        if (event->command == Next || event->command == SkipFRAME) continue;
        Check(!SchoolDaysOrsWindowContains(event, event->startTick ? event->startTick - 1 : 0) ||
              event->startTick == 0, "window visible before START");
        Check(SchoolDaysOrsWindowContains(event, event->startTick),
              "window missing at START");
        Check(!SchoolDaysOrsWindowContains(event, event->endTick),
              "window survived END");
    }
    timeline.AdvanceTo(80);
    for (int repeat = 0; repeat < 64; ++repeat) {
        timeline.Restart();
        Check(timeline.AdvanceTo(0).size() == 1,
              "restart registered duplicate first event");
        timeline.RebuildActiveAt(32);
        const auto inside = timeline.AdvanceTo(32);
        Check(inside.size() == 4 &&
              inside[0]->command == CREATE_BG &&
              inside[1]->command == PLAY_BGM &&
              inside[2]->command == PLAY_MOVIE &&
              inside[3]->command == BLACK_FADE,
              "seek inside rebuilt wrong windows");
        timeline.RebuildActiveAt(81);
        Check(timeline.AdvanceTo(81).empty() && timeline.ReadyToEnd(81),
              "seek after END revived events");
        timeline.Restart();
        Check(timeline.AdvanceTo(80).size() == 14,
              "restart after seek lost events");
    }
    /* Choice history belongs to G3: a rebuilt SetSELECT is pending, not answered. */
    timeline.RebuildActiveAt(45);
    auto pending = timeline.AdvanceTo(45);
    bool hasChoice = false;
    for (auto* event : pending) hasChoice |= event->command == SetSELECT;
    Check(hasChoice && !SchoolDaysChoiceShouldSubmit(false, -2),
          "seek fabricated resolved choice");
}

void TestCaseResolution(const fs::path& assets) {
    using Kotonoha::ResolveSchoolDaysBgmAsset;
    const std::string root = assets.string();
    const auto se = ResolveSchoolDaysBgmAsset(root.c_str(),
        "SysSe/SE_all/school/chime.OGG");
    const auto voice = ResolveSchoolDaysBgmAsset(root.c_str(),
        "Voice05/05-SD/05-SD-A01/05-SD-A01-0740hutaba.OGG");
    Check(fs::is_regular_file(se) && fs::is_regular_file(voice),
          "Voice/SE case-safe resolver failed on distributed assets");
    Check(!fs::exists(ResolveSchoolDaysBgmAsset(root.c_str(),
          "BGM/SD_BGM/sdbgm20_int.ogg")),
          "missing BGM intro acquired invented fallback");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "usage: SchoolDaysRuntimeParityTest ASSETS_ROOT");
        TestMixedDispatch();
        TestWindowsResetAndSeek();
        TestCaseResolution(argv[1]);
        std::cout << "SchoolDaysRuntimeParityTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SchoolDaysRuntimeParityTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
