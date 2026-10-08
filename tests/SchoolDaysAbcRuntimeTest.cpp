#include <Kotonoha/SchoolDaysAbcRuntime.hpp>
#include <Kotonoha/SchoolDaysVoicePcm.hpp>
#include <Kotonoha/components/Image.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
void Check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}

void TestPcmGeometry() {
    using namespace Kotonoha;
    Check(OriginalSchoolDaysActivityTime100ns(1) == 416667 &&
          OriginalSchoolDaysActivityTime100ns(2) == 833333 &&
          OriginalSchoolDaysActivityTime100ns(3) == 1250000,
          "original 100 ns activity timestamp rounding");
    std::vector<int16_t> pcm(22050, 0);
    pcm[0] = 60;
    pcm[1917] = 61;
    pcm[3834] = -60;
    pcm[5752] = -59;
    auto records = AnalyzeSchoolDaysPcm(pcm);
    Check(records.size() == 12, "full 44100-byte chunk record count");
    Check(!records[0].active && records[1].active && records[2].active &&
          !records[3].active, "signed s16 thresholds");
    Check(!SampleSchoolDaysPcmChunk(pcm, 0, 44100, 23),
          "index >= B must be inactive");
    Check(SchoolDaysActivityAt(records, 1) &&
          !SchoolDaysActivityAt(records, 12), "activity query after media EOF");

    pcm.resize(22050 + 10000, 0);
    records = AnalyzeSchoolDaysPcm(pcm);
    Check(records.size() == 17, "partial final chunk record count");
    for (size_t n = 12; n < records.size(); ++n)
        Check(!records[n].active, "partial final chunk negative index");
    Check(!SampleSchoolDaysPcmChunk(pcm, 44100, 20000, 12),
          "negative local sample index must be inactive");

    std::vector<int16_t> fullTwoChunks(44100, 0);
    fullTwoChunks[22050 + 958] = 61;
    records = AnalyzeSchoolDaysPcm(fullTwoChunks);
    Check(records.size() == 24 && records[12].active,
          "P/2 offset in second full chunk");
    /* AVFrame boundaries must not alter the normalized sample stream. */
    std::vector<int16_t> split;
    for (size_t pos = 0; pos < fullTwoChunks.size(); pos += 1073)
        split.insert(split.end(), fullTwoChunks.begin() + pos,
                     fullTwoChunks.begin() + std::min(pos + 1073, fullTwoChunks.size()));
    const auto repeated = AnalyzeSchoolDaysPcm(split);
    Check(repeated.size() == records.size(), "AVFrame-size independence count");
    for (size_t n = 0; n < records.size(); ++n)
        Check(repeated[n].active == records[n].active, "AVFrame-size independence value");
}

void TestScheduler() {
    using namespace Kotonoha;
    std::vector<SchoolDaysActivityRecord> active;
    for (unsigned n = 0; n < 16; ++n) active.push_back({n, true});
    auto at2 = ComputeSchoolDaysAbcStateAt(active, 1, 2);
    auto at3 = ComputeSchoolDaysAbcStateAt(active, 1, 3);
    auto at6 = ComputeSchoolDaysAbcStateAt(active, 1, 6);
    auto at9 = ComputeSchoolDaysAbcStateAt(active, 1, 9);
    Check(at2.activeIndex == 0 && at3.activeIndex == 1 &&
          at6.activeIndex == 2 && at9.activeIndex == 0,
          "24 Hz graphic tick / 3: A B C A");
    active[3].active = false; // scene tick 4
    auto idle = ComputeSchoolDaysAbcStateAt(active, 1, 4);
    auto resumed = ComputeSchoolDaysAbcStateAt(active, 1, 6);
    Check(idle.activeIndex == 0 && idle.counter == 1 &&
          resumed.activeIndex == 2, "idle convergence and retained counter");
    Check(ComputeSchoolDaysAbcStateAt(active, 1, 6).counter == resumed.counter,
          "seek rebuild must be deterministic");
    SchoolDaysAbcState mak, tai;
    AdvanceSchoolDaysAbcState(mak, active, 1, 3);
    AdvanceSchoolDaysAbcState(tai, active, 10, 10);
    Check(mak.counter == 1 && tai.counter == 0,
          "different keys must keep independent counters");
}

void TestDiscovery(const fs::path& assets) {
    const fs::path folder = fs::temp_directory_path() /
        "kotonoha_g2_4_x01_fixture";
    fs::create_directories(folder);
    const fs::path base = folder / "04-KA-F07-002B.PNG";
    const fs::path a = folder / "04-KA-F07-002BX01.A.PNG";
    const fs::path b = folder / "04-KA-F07-002BX01.B.PNG";
    { std::ofstream file(base); Check(bool(file), "base fixture create"); }
    { std::ofstream file(a); Check(bool(file), "A fixture create"); }
    { std::ofstream file(b); Check(bool(file), "B fixture create"); }
    {
        Kotonoha::Image image(nullptr);
        const Uint64 baseId = image.Register(base.string().c_str(), 0, 1000, 0);
        Check(image.ActivateBase(baseId), "activate x01 fixture base");
        auto* x01 = image.FindAbcGroup("x01", 100);
        Check(x01 != nullptr && !x01->resources.Complete(),
              "generic x01 or partial-group discovery");
        image.SelectAbcState(x01, 1);
        Check(x01->activeIndex == 1, "valid partial state selection");
        image.SelectAbcState(x01, 2);
        Check(x01->activeIndex == 1, "missing C must not fabricate a frame");
        Check(image.FindAbcGroup("", 100) == nullptr &&
              image.FindAbcGroup("mak", 100) == nullptr,
              "empty or absent key fallback");
    }
    std::error_code error;
    fs::remove(a, error); fs::remove(b, error); fs::remove(base, error);
    fs::remove(folder, error);

    const fs::path a01 = assets / "Event00" / "00-00" / "00-00-A01";
    if (!fs::exists(a01 / "00-00-A01-004.PNG")) {
        std::cout << "A01 asset fixture unavailable; real-asset check skipped\n";
        return;
    }
    Kotonoha::Image image(nullptr);
    const Uint64 imageBase = image.Register(
        (a01 / "00-00-A01-004.PNG").string().c_str(), 0, 5000, 0);
    Check(image.ActivateBase(imageBase),
          "activate A01 base");
    auto* mak = image.FindAbcGroup("mak", 1000);
    auto* tai = image.FindAbcGroup("tai", 1000);
    Check(mak && tai && mak != tai && mak->resources.Complete() &&
          tai->resources.Complete(), "A01 MAK/TAI real triplets");
    image.SelectAbcState(mak, 2);
    Check(mak->activeIndex == 2 && tai->activeIndex == 0,
          "MAK selection changed TAI");
    SDL_Surface* surface = Kotonoha_imageCreateSurface(
        mak->resources.paths[0].c_str(), -1, -1);
    Check(surface && surface->w == 800 && surface->h == 452,
          "A01 overlay dimensions");
    SDL_DestroySurface(surface);

    Kotonoha_time* clock = Kotonoha_timeNew(false);
    SDL_Surface* canvas = SDL_CreateSurface(800, 452, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = canvas ? SDL_CreateSoftwareRenderer(canvas) : nullptr;
    Check(clock && canvas && renderer, "software overlay render fixture");
    Kotonoha::Image drawn(clock);
    const Uint64 drawnBase = drawn.Register(
        (a01 / "00-00-A01-004.PNG").string().c_str(), 0, 5000, 0);
    Check(drawn.ActivateBase(drawnBase),
          "activate A01 render base");
    auto* drawnMak = drawn.FindAbcGroup("mak", 1000);
    Check(drawnMak != nullptr &&
          drawn.BindAbcGroup(drawnMak, drawnBase, 0, 5000),
          "render group discovery and voice bind");
    const auto frameHash = [&]() -> uint64_t {
        Check(Kotonoha::Image::Render(nullptr, renderer, nullptr, &drawn, nullptr) ==
              KOTONOHA_SCENE_DRAW, "render base plus ABC");
        Check(SDL_RenderPresent(renderer), "software render present");
        uint64_t hash = 1469598103934665603ULL;
        Check(SDL_LockSurface(canvas), "software canvas lock");
        const auto* pixels = static_cast<const uint8_t*>(canvas->pixels);
        for (int y = 0; y < canvas->h; ++y)
            for (int x = 0; x < canvas->pitch; ++x) {
                hash ^= pixels[y * canvas->pitch + x];
                hash *= 1099511628211ULL;
            }
        SDL_UnlockSurface(canvas);
        return hash;
    };
    const uint64_t imageA = frameHash();
    drawn.SelectAbcState(drawnMak, 1);
    const uint64_t imageB = frameHash();
    drawn.SelectAbcState(drawnMak, 2);
    const uint64_t imageC = frameHash();
    Check(imageA != imageB && imageB != imageC && imageA != imageC,
          "A/B/C selection did not change rendered pixels");
    drawn.Reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(canvas);
    Kotonoha_timeDestroy(clock);

    const fs::path kc = assets / "Event05" / "05-KC" / "05-KC-A02";
    if (fs::exists(kc / "05-KC-A02-009.PNG")) {
        Kotonoha::Image other(nullptr);
        const Uint64 otherBase = other.Register(
            (kc / "05-KC-A02-009.PNG").string().c_str(), 0, 5000, 0);
        Check(other.ActivateBase(otherBase),
              "activate KC base");
        auto* kot = other.FindAbcGroup("kot", 1000);
        Check(kot && kot->resources.paths[0].find("009KOT.A.PNG") !=
              std::string::npos, "009 must bind KOT, not uncoded ABC");
    }
}

void TestVoiceDecode(const fs::path& assets) {
    const fs::path voice = assets / "Voice00" / "00-00" / "00-00-A01" /
        "00-00-A01-0230.OGG";
    if (!fs::exists(voice)) return;
    std::vector<int16_t> pcm;
    Check(Kotonoha::DecodeSchoolDaysVoicePcm(voice.string(), pcm) &&
          pcm.size() > 44100, "real MAK voice mono s16 decode");
    std::cout << "A01 MAK PCM samples: " << pcm.size() << '\n';
    Check(pcm.size() == 59943, "MAK PCM length differs from G1.4.1 trace");
    const auto makActivity = Kotonoha::AnalyzeSchoolDaysPcm(pcm);
    std::string makBits;
    for (const auto& record : makActivity) makBits += record.active ? '1' : '0';
    Check(makBits == "00001101111001111111111000000000",
          "MAK activity differs from corrected G1.4 runtime trace");
    Check(!makActivity.empty(),
          "real MAK voice activity records");
    const fs::path tai = voice.parent_path() / "00-00-A01-0240.OGG";
    if (fs::exists(tai)) {
        Check(Kotonoha::DecodeSchoolDaysVoicePcm(tai.string(), pcm),
              "real TAI voice mono s16 decode");
        std::cout << "A01 TAI PCM samples: " << pcm.size() << '\n';
        const auto taiActivity = Kotonoha::AnalyzeSchoolDaysPcm(pcm);
        std::string taiBits;
        for (const auto& record : taiActivity) taiBits += record.active ? '1' : '0';
        Check(taiBits == "000111011111101111111110111111111100000000",
              "TAI activity differs from corrected G1.4 runtime trace");
        Check(pcm.size() == 78693, "TAI PCM length differs from G1.4.1 trace");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL init");
        TestPcmGeometry();
        TestScheduler();
        TestDiscovery(argc > 1 ? argv[1] : "assets");
        TestVoiceDecode(argc > 1 ? argv[1] : "assets");
        SDL_Quit();
        std::cout << "SchoolDaysAbcRuntimeTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysAbcRuntimeTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
