#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/SchoolDaysRemainingOrs.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/components/Fade.hpp>
#include <Kotonoha/components/Image.hpp>

#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path path;
    Kotonoha_orsData events{};
    explicit Fixture(const char* source) {
        const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
            ("kotonoha_g2_6_" + std::to_string(serial) + ".ors");
        { std::ofstream out(path, std::ios::binary); Check(bool(out), "fixture write"); out << source; }
        events = Kotonoha_OrsParser(path.string().c_str());
    }
    ~Fixture() {
        Kotonoha_OrsClean(&events);
        std::error_code error;
        fs::remove(path, error);
    }
};

void TestEventClassesAndWindows() {
    using namespace Kotonoha;
    Check(ClassifySchoolDaysOrs(CREATE_BG) == SchoolDaysOrsClass::VisualWindow &&
          ClassifySchoolDaysOrs(PRINT_TEXT) == SchoolDaysOrsClass::TextWindow &&
          ClassifySchoolDaysOrs(SetSELECT) == SchoolDaysOrsClass::Interaction &&
          ClassifySchoolDaysOrs(BLACK_FADE) == SchoolDaysOrsClass::FadeWindow &&
          ClassifySchoolDaysOrs(WHITE_FADE) == SchoolDaysOrsClass::FadeWindow &&
          ClassifySchoolDaysOrs(MOVE_SOM) == SchoolDaysOrsClass::LegacyNoOpWindow &&
          ClassifySchoolDaysOrs(SkipFRAME) == SchoolDaysOrsClass::Marker &&
          ClassifySchoolDaysOrs(Next) == SchoolDaysOrsClass::SceneEndMarker,
          "remaining ORS classification");
    Fixture f("[CreateBG]=00:00:10\tBGS\tEvent00/base\t00:00:20;\n"
              "[PrintText]=00:00:10\tMAK\tExact text\t00:00:20;\n"
              "[PlayVoice]=00:00:10\tVoice00/line\t1\tmak\t00:00:20;\n"
              "[Next]=00:00:22;\n");
    Check(f.events.size == 4, "fixture parse");
    auto* bg = f.events.data;
    auto* text = bg->next;
    auto* voice = text->next;
    Check(std::string(bg->data.create_bg->a) == "BGS" &&
          std::string(text->data.print_text->character) == "MAK" &&
          std::string(text->data.print_text->text) == "Exact text",
          "CreateBG structural field or PrintText content changed");
    Check(!SchoolDaysOrsWindowContains(bg, 9) &&
          SchoolDaysOrsWindowContains(bg, 10) &&
          SchoolDaysOrsWindowContains(text, 19) &&
          !SchoolDaysOrsWindowContains(text, 20),
          "24 Hz half-open visual/text window");
    Check(Kotonoha_SceneTickToMillisecondsCeil(10) == 417 &&
          Kotonoha_SceneTickToMillisecondsCeil(20) == 834,
          "text/visual clock conversion");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&f.events);
    Check(timeline.AdvanceTo(9).empty(), "prepared visual activated early");
    auto at10 = timeline.AdvanceTo(10);
    Check(at10.size() == 3 && at10[0] == bg && at10[1] == text &&
          at10[2] == voice, "same-tick source order");
    Check(timeline.AdvanceTo(10).empty(), "same-tick event duplicated");
    timeline.RebuildActiveAt(15);
    Check(SchoolDaysOrsWindowContains(text, 15), "seek inside text window");
    timeline.RebuildActiveAt(20);
    Check(!SchoolDaysOrsWindowContains(text, 20), "seek after text end");
    timeline.Restart();
    Check(timeline.AdvanceTo(10).size() == 3, "reset did not rearm events");
}

void TestChoiceAndMarkers() {
    using namespace Kotonoha;
    Fixture f("[SkipFRAME]=01:02:09;\n"
              "[SetSELECT]=01:02:09\t'First'\tnull\t01:08:00;\n"
              "[PlayVoice]=01:05:07\tVoice/path\t1\tmak\t01:07:00;\n"
              "[Next]=01:09:00;\n");
    Check(f.events.size == 4, "choice fixture parse");
    auto* choice = f.events.data->next;
    Check(choice->data.set_select->size == 1, "null option became visible");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&f.events);
    Check(timeline.AdvanceTo(1496).empty(), "early choice");
    const auto at1497 = timeline.AdvanceTo(1497);
    Check(at1497.size() == 2 && at1497[0]->command == SkipFRAME &&
          at1497[1] == choice && !timeline.ReadyToEnd(1497),
          "choice / SkipFRAME collision");
    Check(!SchoolDaysChoiceShouldSubmit(false, -2) &&
          SchoolDaysChoiceShouldSubmit(false, 0) &&
          !SchoolDaysChoiceShouldSubmit(true, 0),
          "choice pending/resolved/idempotent submit");
    Check(timeline.AdvanceTo(1567).size() == 1 &&
          !timeline.ReadyToEnd(1567), "post-choice event lost");
    Check(timeline.AdvanceTo(1656).size() == 1 &&
          timeline.ReadyToEnd(1656), "Next authority lost");
    timeline.Restart();
    Check(timeline.AdvanceTo(1497).size() == 2,
          "choice not rearmed by restart");
}

void TestFades() {
    using namespace Kotonoha;
    Check(Fade::AlphaAtTick(100, 100, 104, FadeDirection::Out) == 0 &&
          Fade::AlphaAtTick(101, 100, 104, FadeDirection::Out) == 63 &&
          Fade::AlphaAtTick(104, 100, 104, FadeDirection::Out) == 255 &&
          Fade::AlphaAtTick(100, 100, 104, FadeDirection::In) == 255 &&
          Fade::AlphaAtTick(104, 100, 104, FadeDirection::In) == 0,
          "F1.0 alpha endpoints or floor rule changed");
    Fixture f("[BlackFade]=00:04:04\t0\t00:04:08;\n"
              "[WhiteFade]=00:04:04\t1\t00:04:08;\n"
              "[Next]=00:05:00;\n");
    Check(f.events.size == 3 && f.events.data->startTick == 100 &&
          f.events.data->endTick == 104,
          "Fade SceneTick bridge parse");
    Check(Kotonoha_MillisecondsToSceneTick(
              Kotonoha_SceneTickToMillisecondsCeil(104)) == 104,
          "Fade SceneTick bridge conversion");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&f.events);
    Check(timeline.AdvanceTo(100).size() == 2 &&
          !timeline.ReadyToEnd(104), "fade must not end scene");
}

void TestMoveSom() {
    using namespace Kotonoha;
    Fixture f("[MoveSom]=00:00:01\t1\t00:00:02;\n"
              "[MoveSom]=00:00:03\t2\t00:00:04;\n"
              "[MoveSom]=00:00:05\t3\t00:00:06;\n"
              "[MoveSom]=00:00:07\t4\t00:00:08;\n"
              "[MoveSom]=00:00:09\t5\t00:00:10;\n"
              "[Next]=00:00:11;\n");
    Check(f.events.size == 6, "MoveSom values 1..5 parse");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&f.events);
    SchoolDaysMoveSomNoOp noOp;
    unsigned value = 1;
    for (auto* event = f.events.data; event && event->command == MOVE_SOM;
         event = event->next, ++value) {
        Check(event->data.move_som->numeric == value,
              "MoveSom numeric altered");
        auto dispatched = timeline.AdvanceTo(event->startTick);
        Check(dispatched.size() == 1 && dispatched[0] == event,
              "MoveSom dispatch missed tick jump");
        noOp.Start(event);
        Check(noOp.Active(event), "MoveSom no-op cursor not marked");
        noOp.AdvanceTo(event->endTick);
        Check(!noOp.Active(event), "MoveSom no-op window not ended");
    }
    Check(!timeline.ReadyToEnd(10), "MoveSom affected Next");
    const auto next = timeline.AdvanceTo(11);
    Check(next.size() == 1 && next[0]->command == Next &&
          timeline.ReadyToEnd(11), "MoveSom affected Next");
    noOp.Start(f.events.data);
    noOp.Reset();
    Check(!noOp.Active(f.events.data), "MoveSom reset leaked state");
}

void TestImages(const fs::path& assets) {
    using namespace Kotonoha;
    const fs::path a01 = assets / "Event00" / "00-00" / "00-00-A01";
    const auto resolved = ResolveSchoolDaysImageAsset(assets.string().c_str(),
        "event00/00-00/00-00-a01/00-00-a01-004");
    if (!fs::exists(a01 / "00-00-A01-004.PNG")) {
        std::cout << "A01 assets unavailable; real image check skipped\n";
        return;
    }
    Check(resolved.present && !resolved.collision &&
          fs::exists(resolved.physical), "component-wise case resolution");
    Check(!ResolveSchoolDaysImageAsset(assets.string().c_str(),
          "Event00/00-00/00-00-A01/DOES-NOT-EXIST").present,
          "missing PNG fabricated");
    const std::string first = resolved.physical;
    const std::string second = (a01 / "00-00-A01-003.PNG").string();
    Image image(nullptr);
    image.Register(first.c_str(), 100, 1000, 0);
    image.Register(second.c_str(), 500, 1500, 0);
    Check(image.FindAbcGroup("mak", 600) == nullptr,
          "prepared base was presented before activation");
    Check(image.ActivateBase(first), "activate first base");
    auto* mak = image.FindAbcGroup("mak", 600);
    Check(mak != nullptr && image.IsActiveGroup(mak, 600),
          "first base ABC discovery");
    Check(image.ActivateBase(second) && !image.IsActiveGroup(mak, 600) &&
          image.FindAbcGroup("mak", 600) == nullptr,
          "old base ABC survived replacement");
    image.Reset();
    Check(image.FindAbcGroup("mak", 600) == nullptr,
          "reset retained old base");

    Kotonoha_time* clock = Kotonoha_timeNew(false);
    SDL_Surface* canvas = SDL_CreateSurface(800, 452, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = canvas ? SDL_CreateSoftwareRenderer(canvas) : nullptr;
    Check(clock && canvas && renderer, "software CreateBG fixture");
    Image drawn(clock);
    drawn.Register(first.c_str(), 100, 1000, 0);
    drawn.Register(second.c_str(), 500, 1500, 0);
    Check(drawn.ActivateBase(first), "activate render base");
    SDL_SetRenderDrawColor(renderer, 23, 45, 67, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    const auto hash = [&]() {
        uint64_t result = 1469598103934665603ULL;
        Check(SDL_LockSurface(canvas), "surface lock");
        const auto* bytes = static_cast<const uint8_t*>(canvas->pixels);
        for (int y = 0; y < canvas->h; ++y)
            for (int x = 0; x < canvas->pitch; ++x) {
                result ^= bytes[y * canvas->pitch + x];
                result *= 1099511628211ULL;
            }
        SDL_UnlockSurface(canvas);
        return result;
    };
    const uint64_t beforeStart = hash();
    Kotonoha_timeSet(clock, 50);
    Check(Image::Render(nullptr, renderer, nullptr, &drawn, nullptr) ==
          KOTONOHA_SCENE_WAITING, "prepared CreateBG drew before START");
    SDL_RenderPresent(renderer);
    Check(hash() == beforeStart, "prepared CreateBG changed presented pixels");
    Kotonoha_timeSet(clock, 200);
    Check(Image::Render(nullptr, renderer, nullptr, &drawn, nullptr) ==
          KOTONOHA_SCENE_DRAW, "CreateBG not drawn at START");
    SDL_RenderPresent(renderer);
    const uint64_t firstFrame = hash();
    Check(firstFrame != beforeStart, "CreateBG draw did not change pixels");
    uint8_t r = 0, g = 0, b = 0, a = 0;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    Check(r == 23 && g == 45 && b == 67 && a == 255,
          "CreateBG leaked clear color to compositor");
    Check(drawn.ActivateBase(second), "activate replacement base");
    Kotonoha_timeSet(clock, 600);
    Check(Image::Render(nullptr, renderer, nullptr, &drawn, nullptr) ==
          KOTONOHA_SCENE_DRAW, "replacement base did not draw");
    SDL_RenderPresent(renderer);
    Check(hash() != firstFrame, "replacement base reused previous pixels");
    drawn.Reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(canvas);
    Kotonoha_timeDestroy(clock);
}
} // namespace

int main(int argc, char** argv) {
    try {
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL init");
        TestEventClassesAndWindows();
        TestChoiceAndMarkers();
        TestFades();
        TestMoveSom();
        TestImages(argc > 1 ? argv[1] : "assets");
        SDL_Quit();
        std::cout << "SchoolDaysRemainingOrsTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysRemainingOrsTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
