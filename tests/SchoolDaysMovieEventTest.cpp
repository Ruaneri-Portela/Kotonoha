#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/SchoolDaysMovieAssetResolver.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/components/Video.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
void Check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}

void TestLogicalContract() {
    using namespace Kotonoha;
    auto movie = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::PlayMovie, "Movie00/00-00/film", 100, 200, 0);
    auto roll = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::EndRoll, "Movie05/05-SB/credits", 100, 200);
    Check(movie.resource == "Movie00/00-00/film.wmv" &&
          roll.resource == "Movie05/05-SB/credits.wmv", "PATH.wmv");
    Check(movie.kind != roll.kind && movie.numeric == 0 && roll.numeric == 0,
          "distinct ORS kinds sharing movie resource contract");
    Check(!movie.active && !movie.Activate(100), "prepare != activate");
    movie.Prepare(true);
    Check(!movie.active && !movie.Activate(99), "premature presentation");
    Check(movie.Activate(100) && movie.active, "START activation");
    movie.MarkEof();
    Check(movie.eof && movie.active && !movie.completed,
          "EOF must not end movie window");
    Check(!movie.AdvanceTo(199) && movie.active,
          "window ended before END");
    Check(movie.AdvanceTo(200) && movie.completed && !movie.active,
          "END must clean movie state");
    movie.Reset();
    Check(!movie.prepared && !movie.active && !movie.completed && !movie.eof,
          "reset left decoder state logically active");

    auto unknown = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::PlayMovie, "test", 10, 20, 7);
    Check(unknown.numeric == 7 && !unknown.Supported(),
          "nonzero numeric acquired invented meaning");
    unknown.Prepare(false);
    Check(!unknown.Activate(10), "unknown numeric activated");
    auto missing = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::PlayMovie, "missing", 10, 20);
    missing.Prepare(false);
    Check(!missing.Activate(10) && missing.active,
          "missing WMV should preserve logical window without drawing");
    Check(missing.AdvanceTo(20), "missing WMV blocked timeline");

    // END of an EndRoll may precede Next by hundreds of ticks.
    Kotonoha_orsEvent rollEvent{}, nextEvent{};
    rollEvent.command = END_ROLL; rollEvent.startTick = 100;
    rollEvent.endTick = 200; rollEvent.next = &nextEvent;
    nextEvent.command = Next; nextEvent.startTick = 547;
    nextEvent.endTick = 547;
    Kotonoha_orsData source{}; source.data = &rollEvent;
    SchoolDaysSceneTimeline timeline; timeline.Bind(&source);
    auto due = timeline.AdvanceTo(100);
    Check(due.size() == 1 && due[0] == &rollEvent,
          "multi-tick scheduler lost EndRoll START");
    roll.Prepare(true); Check(roll.Activate(100), "EndRoll START");
    roll.AdvanceTo(200);
    timeline.AdvanceTo(200);
    Check(roll.completed && !timeline.ReadyToEnd(200),
          "EndRoll END caused scene swap");
    timeline.AdvanceTo(546);
    Check(!timeline.ReadyToEnd(546), "scene ended before Next");
    timeline.AdvanceTo(547);
    Check(timeline.ReadyToEnd(547), "Next did not end scene");

    // G1.9 observed EndRoll START=3336, END=Next=6668 in 05-SB-E01.
    roll = SchoolDaysMovieEventState::Create(
        SchoolDaysMovieKind::EndRoll, "Movie05/05-SB/05-SB-E01/05-SB-E01-021",
        3336, 6668);
    Check(!roll.InWindow(3335) && roll.InWindow(3336) &&
          roll.InWindow(6667) && !roll.InWindow(6668),
          "05-SB-E01 24 Hz event boundaries");
    Check(Kotonoha_SceneTickToMillisecondsCeil(3336) == 139000 &&
          Kotonoha_SceneTickToMillisecondsCeil(6668) == 277834,
          "event ms must come from absolute 24 Hz tick");
    Check(!roll.InWindow(7000), "seek after END revived movie");
    Check(roll.InWindow(5000), "backward seek failed to reconstruct window");
}

void TestResolver(const fs::path& assets) {
    using namespace Kotonoha;
    const fs::path folder = fs::temp_directory_path() / "kotonoha_g2_5_resolver";
    fs::create_directories(folder / "Movie00" / "Scene");
    const fs::path file = folder / "Movie00" / "Scene" / "FILM.WMV";
    { std::ofstream out(file); out << "fixture"; }
    Check(fs::path(ResolveSchoolDaysMovieAsset(folder.string().c_str(),
          "movie00/scene/film.wmv")) == file,
          "case-insensitive resolver failed on case-sensitive components");
    std::error_code error; fs::remove_all(folder, error);
    const fs::path real = ResolveSchoolDaysMovieAsset(assets.string().c_str(),
        "Movie00/00-00/00-00-A00/00-00-A00-000.wmv");
    if (fs::exists(assets / "Movie00"))
        Check(fs::exists(real), "real A00 WMV did not resolve");
}

bool MakeSyntheticWmv(const fs::path& file) {
    const std::string command = "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=red:s=64x48:r=24:d=0.5 "
        "-an -c:v wmv2 -f asf \"" + file.string() + "\"";
    return std::system(command.c_str()) == 0 && fs::exists(file);
}

void TestPhysicalVideo(const fs::path& file) {
    using namespace Kotonoha;
    Kotonoha_time* clock = Kotonoha_timeNew(false);
    Check(clock != nullptr, "movie test clock");
    SDL_Surface* surface = SDL_CreateSurface(128, 96, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Check(renderer != nullptr, "software movie renderer");
    {
        Video video(clock);
        Kotonoha_orsEvent event{};
        event.command = PLAY_MOVIE; event.startTick = 0; event.endTick = 96;
        Check(video.Prepare(&event, file.string().c_str()),
              "synthetic WMV prepare failed");
        Check(Video::Render(nullptr, renderer, nullptr, &video, nullptr) ==
              KOTONOHA_SCENE_NULL, "prepared WMV presented before activation");
        Check(video.Activate(&event), "WMV activation failed");
        bool sawFrame = false, sawEof = false;
        for (Uint64 ms = 0; ms <= 1250; ms += 41) {
            Kotonoha_timeSet(clock, ms);
            const auto status = Video::Render(nullptr, renderer, nullptr,
                                              &video, nullptr);
            sawFrame |= status == KOTONOHA_SCENE_DRAW;
            sawEof |= video.IsEof(&event);
        }
        Check(sawFrame, "WMV decoded no visible frame");
        Check(sawEof, "synthetic WMV EOF not observed");
        Kotonoha_timeSet(clock, 2000);
        Check(Video::Render(nullptr, renderer, nullptr, &video, nullptr) ==
              KOTONOHA_SCENE_DRAW, "EOF discarded terminal texture before END");
        Check(video.IsEof(&event), "EOF restarted the decoder before END");
        video.Remove(&event);
        Check(!video.Activate(&event), "END retained old video handle");
        Kotonoha_timeSet(clock, 200);
        Check(video.Prepare(&event, file.string().c_str()),
              "seek back into window failed to recreate decoder");
        video.Reset();
        Check(!video.Activate(&event), "reset retained decoder");
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    Kotonoha_timeDestroy(clock);
}
} // namespace

int main(int argc, char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL video init");
        const fs::path assets = argc > 1 ? argv[1] : "assets";
        TestLogicalContract();
        TestResolver(assets);
        const fs::path fixture = fs::temp_directory_path() /
            "kotonoha_g2_5_synthetic.wmv";
        if (MakeSyntheticWmv(fixture)) {
            TestPhysicalVideo(fixture);
            std::error_code error; fs::remove(fixture, error);
        } else {
            std::cout << "ffmpeg CLI unavailable: synthetic decoder test skipped\n";
        }
        const fs::path real = Kotonoha::ResolveSchoolDaysMovieAsset(
            assets.string().c_str(),
            "Movie00/00-00/00-00-A00/00-00-A00-000.wmv");
        if (fs::exists(real)) {
            Kotonoha_time* clock = Kotonoha_timeNew(false);
            Kotonoha_videoData* probe = Kotonoha_VideoRenderInit(
                real.string().c_str(), clock, 0, 5000, 120);
            Check(probe != nullptr, "real A00 WMV decode failed");
            Check(probe->pCodecCtx->width == 800 && probe->pCodecCtx->height == 452,
                  "real A00 WMV dimensions");
            Kotonoha_VideoRenderShutdown(&probe);
            Kotonoha_timeDestroy(clock);
        }
        SDL_Quit();
        std::cout << "SchoolDaysMovieEventTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysMovieEventTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
