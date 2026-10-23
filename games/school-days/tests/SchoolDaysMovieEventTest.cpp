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

void TestPlaybackRateClock() {
    Kotonoha_time* clock = Kotonoha_timeNew(false);
    Check(clock != nullptr, "rate test clock");

    Kotonoha_timePause(clock);
    Kotonoha_timeSet(clock, 1000);
    const Uint64 initialSerial = Kotonoha_timeGetDiscontinuitySerial(clock);
    const double nativeRates[] = {1.0, 2.0, 4.0, 12.0, 24.0};
    for (double rate : nativeRates) {
        const Uint64 before = Kotonoha_timeGet(clock);
        const Uint64 serial = Kotonoha_timeGetDiscontinuitySerial(clock);
        Check(Kotonoha_timeSetRate(clock, rate), "authenticated rate rejected");
        Check(Kotonoha_timeGetRate(clock) == rate, "rate getter mismatch");
        Check(Kotonoha_timeGet(clock) == before,
              "paused SetRate changed logical position");
        Check(Kotonoha_timeGetDiscontinuitySerial(clock) == serial,
              "SetRate incorrectly marked a timeline discontinuity");
    }
    Check(initialSerial != 0, "clock discontinuity serial was not initialized");

    const double retained = Kotonoha_timeGetRate(clock);
    Check(!Kotonoha_timeSetRate(clock, 3.0) &&
              !Kotonoha_timeSetRate(clock, 0.0) &&
              Kotonoha_timeGetRate(clock) == retained,
          "unauthenticated rate was accepted or changed state");

    const Uint64 beforeForward = Kotonoha_timeGet(clock);
    const Uint64 serialBeforeForward =
        Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeSeekForward(clock, 125);
    Check(Kotonoha_timeGet(clock) == beforeForward + 125,
          "paused forward seek lost logical position");
    Check(Kotonoha_timeGetDiscontinuitySerial(clock) != serialBeforeForward,
          "forward seek did not mark discontinuity");

    const Uint64 serialBeforeBackward =
        Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeSeekBackward(clock, 25);
    Check(Kotonoha_timeGet(clock) == beforeForward + 100,
          "paused backward seek lost logical position");
    Check(Kotonoha_timeGetDiscontinuitySerial(clock) != serialBeforeBackward,
          "backward seek did not mark discontinuity");

    const Uint64 serialBeforeSet = Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeSet(clock, 777);
    Check(Kotonoha_timeGet(clock) == 777,
          "paused absolute set changed requested logical position");
    Check(Kotonoha_timeGetDiscontinuitySerial(clock) != serialBeforeSet,
          "absolute set did not mark discontinuity");

    Check(Kotonoha_timeSetRate(clock, 24.0), "24x restore failed");
    const Uint64 beforeResume = Kotonoha_timeGet(clock);
    const Uint64 serialBeforeResume =
        Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeResume(clock);
    SDL_Delay(5);
    Kotonoha_timePause(clock);
    const Uint64 afterResume = Kotonoha_timeGet(clock);
    Check(afterResume >= beforeResume + 24,
          "24x clock did not advance faster than one logical millisecond");
    Check(Kotonoha_timeGetDiscontinuitySerial(clock) == serialBeforeResume,
          "pause/resume incorrectly marked timeline discontinuity");

    const Uint64 serialBeforeReset = Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeReset(clock, false);
    Check(Kotonoha_timeGetRate(clock) == 24.0,
          "reset discarded selected playback rate");
    Check(Kotonoha_timeGetDiscontinuitySerial(clock) != serialBeforeReset,
          "reset did not mark timeline discontinuity");
    Kotonoha_timeDestroy(clock);
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
    Kotonoha_timePause(clock);
    Kotonoha_timeSet(clock, 0);
    Check(Kotonoha_timeSetRate(clock, 24.0), "movie test 24x clock");
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
        SDL_Delay(10); // Let the demux queue fill without advancing logical time.
        Kotonoha_timeResume(clock);
        for (int sample = 0; sample < 30; ++sample) {
            SDL_Delay(4);
            const auto status = Video::Render(nullptr, renderer, nullptr,
                                              &video, nullptr);
            sawFrame |= status == KOTONOHA_SCENE_DRAW;
            sawEof |= video.IsEof(&event);
            if (Kotonoha_timeGet(clock) >= 2000 && sawEof) break;
        }
        Kotonoha_timePause(clock);
        Check(sawFrame, "WMV decoded no visible frame");
        Check(sawEof, "synthetic WMV EOF not observed");
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

void TestHighRateVideoDiscontinuity(const fs::path& file) {
    Kotonoha_time* clock = Kotonoha_timeNew(false);
    Check(clock != nullptr, "high-rate video clock");
    Kotonoha_timePause(clock);
    Kotonoha_timeSet(clock, 0);
    Check(Kotonoha_timeSetRate(clock, 24.0), "high-rate video 24x");

    SDL_Surface* surface = SDL_CreateSurface(128, 96, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Check(renderer != nullptr, "high-rate software renderer");

    Kotonoha_videoData* video = Kotonoha_VideoRenderInit(
        file.string().c_str(), clock, 0, 5000, 120);
    Check(video != nullptr, "high-rate decoder init");
    KotonohaVideoRenderPrime(video, 50);
    SDL_Delay(10); // Prefill while timeline remains frozen at zero.

    Kotonoha_timeResume(clock);
    SDL_Delay(5);
    Kotonoha_timePause(clock);
    const Uint64 firstTarget = Kotonoha_timeGet(clock);
    const auto firstStatus = Kotonoha_VideoRenderProcess(video, renderer);
    Check(firstStatus == KOTONOHA_SCENE_DRAW ||
              firstStatus == KOTONOHA_SCENE_WAITING,
          "high-rate first video process failed");
    Check(video->stream->seekTargetMs == 0,
          "normal 24x progression was mistaken for a seek");
    Check(video->videoTime <= firstTarget,
          "video presented a frame newer than logical target");

    const Uint64 firstVideoTime = video->videoTime;
    Kotonoha_timeResume(clock);
    SDL_Delay(5);
    Kotonoha_timePause(clock);
    const Uint64 secondTarget = Kotonoha_timeGet(clock);
    const auto secondStatus = Kotonoha_VideoRenderProcess(video, renderer);
    Check(secondStatus == KOTONOHA_SCENE_DRAW ||
              secondStatus == KOTONOHA_SCENE_WAITING,
          "high-rate second video process failed");
    Check(video->stream->seekTargetMs == 0,
          "24x catch-up issued a repeated demux seek");
    Check(video->videoTime >= firstVideoTime && video->videoTime <= secondTarget,
          "high-rate catch-up did not retain latest due frame");

    const Uint64 serialBeforeSeek =
        Kotonoha_timeGetDiscontinuitySerial(clock);
    Kotonoha_timeSet(clock, 50);
    const Uint64 serialAfterSeek =
        Kotonoha_timeGetDiscontinuitySerial(clock);
    Check(serialAfterSeek != serialBeforeSeek,
          "explicit video seek did not mark clock discontinuity");
    (void)Kotonoha_VideoRenderProcess(video, renderer);
    Check(video->stream->seekTargetMs == 50,
          "explicit discontinuity did not seek video decoder");
    Check(video->timeDiscontinuitySerial == serialAfterSeek,
          "video decoder did not consume discontinuity exactly once");

    Kotonoha_VideoRenderShutdown(&video);
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
        TestPlaybackRateClock();
        TestLogicalContract();
        TestResolver(assets);
        const fs::path fixture = fs::temp_directory_path() /
            "kotonoha_g2_5_synthetic.wmv";
        if (MakeSyntheticWmv(fixture)) {
            TestPhysicalVideo(fixture);
            TestHighRateVideoDiscontinuity(fixture);
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
