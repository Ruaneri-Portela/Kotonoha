#include <Kotonoha/SchoolDaysAssetMount.hpp>

#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/VoicePcm.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Uri(const char* logical) {
    Check(Kotonoha::LookupMountedSchoolDaysAsset(logical) ==
              Kotonoha::SchoolDaysGpkLookupStatus::Present,
          std::string("retail witness missing: ") + logical);
    return Kotonoha::MakeSchoolDaysGpkUri(logical);
}

void TestDflt(const std::filesystem::path& packs) {
    constexpr const char* logical = "Ini/DEBUGINFO.INI";
    Kotonoha::SchoolDaysGpkAssetStore store;
    std::string error;
    Check(store.OpenRetailDirectory(packs, &error), error);
    size_t archiveIndex = 0u;
    size_t entryIndex = 0u;
    Check(store.Lookup(logical, &archiveIndex, &entryIndex) ==
              Kotonoha::SchoolDaysGpkLookupStatus::Present,
          "DFLT witness lookup failed");
    const auto* archive = store.Archive(archiveIndex);
    Check(archive != nullptr && entryIndex < archive->Index().Entries().size(),
          "DFLT witness entry index is invalid");
    Check(archive->Index().Entries()[entryIndex].IsDeflate(),
          "Ini/DEBUGINFO.INI is not a DFLT entry");

    std::vector<uint8_t> bytes;
    const std::string uri = Uri(logical);
    Check(Kotonoha::ReadSchoolDaysAssetRef(uri, &bytes, &error), error);
    const std::string expected =
        "[NoCheck]=\"1\"\r\n\r\n"
        "[CheckInterval]=\"10\";\r\n\r\n"
        "[LogCreate]=\"0\";\r\n\r\n"
        "[SerialInput]=\"0\"\r\n\r\n";
    Check(bytes == std::vector<uint8_t>(expected.begin(), expected.end()),
          "DFLT witness decoded bytes drifted");
    std::cout << "dflt=" << logical << " bytes=" << bytes.size() << " PASS\n";
}

void TestImage() {
    for (const char* logical : {"System/MenuBar/MenuBar.PNG",
                                "System/SCREEN/TELOP/TELOP02.PNG"}) {
        const std::string uri = Uri(logical);
        Uint8* pixels = nullptr;
        AVFrame* frame = KotonohaImageDecodeFrame(uri.c_str(), -1, -1, &pixels, false);
        Check(frame != nullptr && pixels != nullptr && frame->width > 0 &&
                  frame->height > 0,
              std::string("retail PNG did not decode through mounted URI: ") +
                  logical);
        if (std::string(logical).find("TELOP") != std::string::npos) {
            Check(frame->width == 800 && frame->height == 452,
                  "retail Episode TELOP geometry drifted");
        }
        std::cout << "image=" << logical << " " << frame->width << "x"
                  << frame->height << " PASS\n";
        KotonohaImageReleaseFrame(&frame, &pixels);
    }
}

void TestBgm() {
    constexpr const char* logical = "BGM/SD_BGM/sdbgm07_int.ogg";
    const std::string uri = Uri(logical);
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = 44100;
    Kotonoha_audioDecode* decode = Kotonoha_AudioInit(uri.c_str(), spec);
    Check(decode != nullptr, "retail BGM decoder init failed through mounted URI");

    Uint8* samples = nullptr;
    int bytes = 0;
    int result = 0;
    const Uint64 deadline = SDL_GetTicks() + 5000u;
    do {
        result = Kotonoha_AudioRender(decode, &samples, &bytes);
        if (result == 2 && samples != nullptr && bytes > 0) break;
        if (samples != nullptr) {
            SDL_free(samples);
            samples = nullptr;
        }
        SDL_Delay(1);
    } while (result >= 0 && SDL_GetTicks() < deadline);
    Check(result == 2 && samples != nullptr && bytes > 0,
          "retail BGM produced no decoded audio through mounted URI");
    std::cout << "bgm=" << logical << " decoded_bytes=" << bytes << " PASS\n";
    SDL_free(samples);
    Kotonoha_AudioFree(decode);
}

void TestVoice() {
    constexpr const char* logical =
        "Voice00/00-00/00-00-A01/00-00-A01-0230.OGG";
    const std::string uri = Uri(logical);
    Sint16* samples = nullptr;
    size_t count = 0u;
    Check(KotonohaPcmDecodeVoice(uri.c_str(), &samples, &count) &&
              samples != nullptr && count != 0u,
          "retail voice PCM decode failed through mounted URI");
    std::cout << "voice=" << logical << " pcm_samples=" << count << " PASS\n";
    SDL_free(samples);
}

void TestMovie() {
    constexpr const char* logical =
        "Movie00/00-00/00-00-A00/00-00-A00-000.wmv";
    const std::string uri = Uri(logical);
    Kotonoha_time* time = Kotonoha_timeNew(false);
    Check(time != nullptr, "movie test clock allocation failed");
    Kotonoha_videoData* video = Kotonoha_VideoRenderInit(
        uri.c_str(), time, 0u, std::numeric_limits<Uint64>::max(),
        std::numeric_limits<Kotonoha_SceneTick>::max());
    Check(video != nullptr, "retail movie decoder init failed through mounted URI");
    Check(KotonohaVideoRenderPrime(video, 5000u) && video->currentFrame != nullptr &&
              video->currentFrame->width > 0 && video->currentFrame->height > 0,
          "retail movie produced no decoded frame through mounted URI");
    std::cout << "movie=" << logical << " " << video->currentFrame->width << "x"
              << video->currentFrame->height << " pts_ms=" << video->currentPtsMs
              << " PASS\n";
    Kotonoha_VideoRenderShutdown(&video);
    Kotonoha_timeDestroy(time);
}

void ExtractAbcFixture(const std::filesystem::path& output) {
    struct Witness {
        const char* logical;
    };
    constexpr Witness witnesses[] = {
        {"Event00/00-00/00-00-A01/00-00-A01-004.PNG"},
        {"Event00/00-00/00-00-A01/00-00-A01-004MAK.A.PNG"},
        {"Event00/00-00/00-00-A01/00-00-A01-004MAK.B.PNG"},
        {"Event00/00-00/00-00-A01/00-00-A01-004MAK.C.PNG"},
        {"Event00/00-00/00-00-A01/00-00-A01-004TAI.B.PNG"},
        {"Event00/00-00/00-00-A02/00-00-A02-001B.PNG"},
        {"Event00/00-00/00-00-A02/00-00-A02-001BMAK.A.PNG"},
        {"Event00/00-00/00-00-A02/00-00-A02-001BMAK.B.PNG"},
        {"Event00/00-00/00-00-A02/00-00-A02-001BMAK.C.PNG"},
        {"Event04/04-SB/04-SB-E00/04-SB-E00-020.PNG"},
        {"Event04/04-SB/04-SB-E00/04-SB-E00-020X03.A.PNG"},
        {"Event04/04-SB/04-SB-E00/04-SB-E00-020X03.B.PNG"},
        {"Event04/04-SB/04-SB-E00/04-SB-E00-020X03.C.PNG"},
    };
    for (const auto& witness : witnesses) {
        std::vector<uint8_t> bytes;
        std::string error;
        const std::string uri = Uri(witness.logical);
        Check(Kotonoha::ReadSchoolDaysAssetRef(uri, &bytes, &error), error);
        const auto path = output / std::filesystem::path(witness.logical);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        Check(static_cast<bool>(file), "ABC fixture output could not be created");
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        Check(static_cast<bool>(file), "ABC fixture output write failed");
    }
    std::cout << "abc_fixture=" << output.string() << " files="
              << std::size(witnesses) << " PASS\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 2 || argc == 3,
              "usage: SchoolDaysGpkDirectRuntimeTest <Packs-folder> [ABC-fixture-output]");
        Check(SDL_Init(0), std::string("SDL_Init failed: ") + SDL_GetError());
        std::string error;
        const std::filesystem::path packs(argv[1]);
        Check(Kotonoha::MountSchoolDaysRetailGpks(packs, &error), error);
        Kotonoha::InstallMountedSchoolDaysAssetResolver();
        Check(Kotonoha::MountedSchoolDaysGpkEntryCount() == 69936u,
              "retail mounted entry census drifted");

        TestDflt(packs);
        TestImage();
        TestBgm();
        TestVoice();
        TestMovie();
        if (argc == 3) ExtractAbcFixture(argv[2]);

        Kotonoha::UninstallMountedSchoolDaysAssetResolver();
        Kotonoha::UnmountSchoolDaysRetailGpks();
        SDL_Quit();
        std::cout << "SCHOOL DAYS DIRECT GPK RUNTIME GATE PASS\n";
        return 0;
    } catch (const std::exception& e) {
        Kotonoha::UninstallMountedSchoolDaysAssetResolver();
        Kotonoha::UnmountSchoolDaysRetailGpks();
        SDL_Quit();
        std::cerr << "SchoolDaysGpkDirectRuntimeTest: " << e.what() << '\n';
        return 2;
    }
}
