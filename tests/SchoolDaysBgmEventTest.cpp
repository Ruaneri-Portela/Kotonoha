#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/components/Audio.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Put16(std::ofstream& out, unsigned value) {
    out.put(char(value & 255)); out.put(char((value >> 8) & 255));
}
void Put32(std::ofstream& out, unsigned value) {
    Put16(out, value); Put16(out, value >> 16);
}
void WriteWave(const fs::path& path) {
    constexpr unsigned samples = 960;
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); Put32(out, 36 + samples * 2);
    out.write("WAVEfmt ", 8); Put32(out, 16); Put16(out, 1);
    Put16(out, 1); Put32(out, 24000); Put32(out, 48000);
    Put16(out, 2); Put16(out, 16);
    out.write("data", 4); Put32(out, samples * 2);
    for (unsigned i = 0; i < samples; ++i)
        Put16(out, (i % 80 < 40) ? 2000 : 0);
    Require(static_cast<bool>(out), "write WAV fixture");
}
void TestResourcesAndSlots() {
    using namespace Kotonoha;
    const auto normal = SchoolDaysBgmResources::Normal("BGM/SD_BGM/sdbgm07");
    const auto ending = SchoolDaysBgmResources::Ending("Se03/03-A1/example");
    Require(normal.intro == "BGM/SD_BGM/sdbgm07_int.ogg" &&
            normal.loop == "BGM/SD_BGM/sdbgm07_loop.ogg" &&
            normal.oneShot.empty(), "normal BGM pair resolution");
    Require(ending.oneShot == "Se03/03-A1/example.ogg" &&
            ending.intro.empty() && ending.loop.empty(),
            "EndBGM must be an opaque one-shot path");
    Kotonoha_orsEvent a{}, b{}, c{}, d{};
    SchoolDaysBgmSlots slots;
    Require(slots.ReplaceNormal(&a, normal).event == nullptr,
            "normal starts empty");
    Require(slots.ReplaceEnding(&c, ending).event == nullptr &&
            slots.Normal().event == &a, "ending changed normal slot");
    Require(slots.ReplaceNormal(&b, normal).event == &a &&
            slots.Ending().event == &c, "normal replacement changed ending");
    Require(slots.ReplaceEnding(&d, ending).event == &c &&
            slots.Normal().event == &b, "ending replacement changed normal");
    slots.ClearEnding();
    Require(slots.Ending().event == nullptr && slots.Normal().event == &b,
            "EndBGM cleanup changed normal");
    slots.Clear();
    Require(slots.Normal().event == nullptr, "reset retained normal slot");

    a.command = PLAY_BGM; a.startTick = 10; a.endTick = 100; a.next = &b;
    b.command = PLAY_BGM; b.startTick = 20; b.endTick = 30; b.next = &c;
    c.command = END_BGM; c.startTick = 25; c.endTick = 40; c.next = &d;
    d.command = END_BGM; d.startTick = 35; d.endTick = 50;
    Kotonoha_orsData source{}; source.data = &a;
    Require(SchoolDaysBgmSlots::ActiveAt(&source, 15, PLAY_BGM) == &a,
            "seek lost active normal");
    Require(SchoolDaysBgmSlots::ActiveAt(&source, 31, PLAY_BGM) == nullptr,
            "seek revived replaced normal");
    Require(SchoolDaysBgmSlots::ActiveAt(&source, 45, END_BGM) == &d &&
            SchoolDaysBgmSlots::ActiveAt(&source, 50, END_BGM) == nullptr,
            "seek revived ended EndBGM");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&source);
    auto due = timeline.AdvanceTo(35);
    Require(due.size() == 4 && due[0] == &a && due[3] == &d,
            "24 Hz scheduler lost BGM dispatch order");
    Require(Kotonoha_SceneTickToMillisecondsCeil(24) == 1000,
            "BGM window must use 24 Hz adapter");

    const fs::path folder = fs::temp_directory_path() /
        "kotonoha_g2_3b_resolver";
    fs::create_directories(folder / "BGM" / "SD_BGM");
    const fs::path file = folder / "BGM" / "SD_BGM" / "SDBGM07_INT.OGG";
    { std::ofstream out(file); out << "fixture"; }
    Require(fs::path(ResolveSchoolDaysBgmAsset(
                folder.string().c_str(), normal.intro)) == file,
            "case-sensitive asset spelling not resolved");
    std::error_code error;
    fs::remove(file, error);
    fs::remove(folder / "BGM" / "SD_BGM", error);
    fs::remove(folder / "BGM", error);
    fs::remove(folder, error);
}
void TestDecoderEof() {
    const fs::path path = fs::temp_directory_path() /
        "kotonoha_g2_3b_short_wave.wav";
    WriteWave(path);
    Kotonoha_time* time = Kotonoha_timeNew(false);
    Require(time != nullptr, "time fixture");
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 24000};
    Kotonoha_audioDecode* intro = Kotonoha_AudioInit(path.string().c_str(), spec);
    Kotonoha_audioDecode* loop = Kotonoha_AudioInit(path.string().c_str(), spec);
    Require(intro != nullptr && loop != nullptr, "short WAV decoder fixture");
    intro->loopMedia = loop;
    intro->tm = &time; loop->tm = &time;
    intro->start = loop->start = 0;
    intro->end = loop->end = 100000;
    SDL_SetAtomicInt(&intro->enabled, 1);
    bool enteredLoop = false, repeated = false;
    for (int i = 0; i < 32; ++i) {
        Uint8* bytes = nullptr; int size = 0;
        const int result = Kotonoha::Audio::RenderMedia(intro, &bytes, &size);
        SDL_free(bytes);
        Require(result >= 0, "decoder error");
        enteredLoop |= intro->playingLoop;
        repeated |= loop->executions > 1;
        if (enteredLoop && repeated) break;
    }
    Require(enteredLoop, "intro did not hand off at decoder EOF");
    Require(repeated, "loop did not restart at decoder EOF");
    Kotonoha_AudioFree(intro);
    Kotonoha_audioDecode* oneShot = Kotonoha_AudioInit(path.string().c_str(), spec);
    Require(oneShot != nullptr, "one-shot decoder fixture");
    oneShot->tm = &time;
    oneShot->end = 100000;
    SDL_SetAtomicInt(&oneShot->enabled, 1);
    bool ended = false;
    for (int i = 0; i < 16; ++i) {
        Uint8* bytes = nullptr; int size = 0;
        const int result = Kotonoha::Audio::RenderMedia(oneShot, &bytes, &size);
        SDL_free(bytes);
        if (result == 1) { ended = true; break; }
    }
    Require(ended && oneShot->executions == 1 && !oneShot->playingLoop,
            "EndBGM one-shot restarted after EOF");
    Kotonoha_AudioFree(oneShot);
    Kotonoha_timeDestroy(time);
    std::error_code error; fs::remove(path, error);
}
void TestPreparedPhysicalMedia() {
    const fs::path path = fs::temp_directory_path() /
        "kotonoha_g2_3b_prepared_wave.wav";
    WriteWave(path);
    Kotonoha_time* time = Kotonoha_timeNew(false);
    Require(time != nullptr, "prepared time fixture");
    {
        Kotonoha::Sound sound;
        Require(sound.CreateChannel(SDL_AUDIO_F32, 1, 24000, true,
                                    "BGM", nullptr) != nullptr,
                "dummy BGM channel");
        Kotonoha::Audio audio(&sound, time);
        auto* normal = audio.AddIntroLoopMedia(
            path.string().c_str(), path.string().c_str(), 0, 100000);
        Require(normal != nullptr && normal->loopMedia != nullptr &&
                SDL_GetAtomicInt(&normal->enabled) == 0,
                "BGM prepare activated early or omitted loop");
        audio.SetMediaEnabled(normal, true);
        Require(SDL_GetAtomicInt(&normal->enabled) == 1,
                "BGM activation failed");
        auto* ending = audio.AddMedia(path.string().c_str(), 0, 100000,
                                      false, "BGM", false);
        Require(ending != nullptr && !ending->inLoop &&
                SDL_GetAtomicInt(&ending->enabled) == 0,
                "EndBGM must prepare one-shot without activation");
        audio.SetMediaEnabled(ending, true);
        audio.RemoveMedia(ending);
        auto* missing = audio.AddIntroLoopMedia(
            path.string().c_str(), "missing-loop.ogg", 0, 100000);
        Require(missing == nullptr, "missing loop got undocumented fallback");
        audio.RemoveMedia(nullptr); /* reset removes both decoder handles */
    }
    Kotonoha_timeDestroy(time);
    std::error_code error; fs::remove(path, error);
}
} // namespace

int main() {
    try {
        SDL_SetHint("SDL_AUDIO_DRIVER", "dummy");
        Require(SDL_Init(SDL_INIT_AUDIO), "SDL init");
        TestResourcesAndSlots();
        TestDecoderEof();
        TestPreparedPhysicalMedia();
        SDL_Quit();
        std::cout << "SchoolDaysBgmEventTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysBgmEventTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
