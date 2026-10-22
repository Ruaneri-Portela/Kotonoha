#include <Kotonoha/routing/SchoolDaysTitleMovieClock.hpp>
#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

extern "C" {
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Time.h>
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}

bool PumpTo(Kotonoha_videoData* video, Kotonoha_time* clock,
            SDL_Renderer* renderer, Uint64 elapsedMs, Uint64 deadlineMs) {
    const Uint64 frame = Kotonoha::SchoolDaysTitleMovieClock::FrameIndex(elapsedMs);
    const Uint64 target =
        Kotonoha::SchoolDaysTitleMovieClock::TargetMilliseconds(frame);
    Kotonoha_timeSet(clock, target);
    const Uint64 deadline = SDL_GetTicks() + deadlineMs;
    for (;;) {
        const Uint64 oldVideoTime = video->videoTime;
        SDL_Texture* const oldTexture = video->texture;
        const auto status = Kotonoha_VideoRenderProcess(video, renderer);
        if (status == KOTONOHA_SCENE_FATAL_ERROR ||
            status == KOTONOHA_SCENE_COMPLETE ||
            status == KOTONOHA_SCENE_NULL) {
            return false;
        }
        if (video->decoderEof ||
            (video->texture != nullptr && video->videoTime >= target) ||
            (video->currentFrame != nullptr && video->currentPtsMs > target)) {
            return true;
        }
        const bool progressed = video->videoTime != oldVideoTime ||
                                video->texture != oldTexture;
        if (!progressed) {
            if (SDL_GetTicks() >= deadline) return video->texture != nullptr;
            SDL_Delay(1);
        }
    }
}

bool RunFfmpeg(const std::string& arguments) {
    const std::string command =
        "ffmpeg -hide_banner -loglevel error -y " + arguments;
    return std::system(command.c_str()) == 0;
}

void WriteTitleCmap(const fs::path& path) {
    constexpr uint32_t width = 800;
    constexpr uint32_t height = 600;
    std::vector<unsigned char> bytes(8u + width * height, 0u);
    const auto put32 = [&](size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<unsigned char>(value & 0xffu);
        bytes[offset + 1] = static_cast<unsigned char>((value >> 8u) & 0xffu);
        bytes[offset + 2] = static_cast<unsigned char>((value >> 16u) & 0xffu);
        bytes[offset + 3] = static_cast<unsigned char>((value >> 24u) & 0xffu);
    };
    put32(0u, width);
    put32(4u, height);
    // Region 1 is a small deterministic hit target for the public
    // HandleEvent -> PointerToVirtual -> TitleModel input path.
    for (uint32_t y = 100; y < 200; ++y) {
        for (uint32_t x = 100; x < 200; ++x) {
            bytes[8u + y * width + x] = 1u;
        }
    }
    std::ofstream out(path, std::ios::binary);
    Check(static_cast<bool>(out), "Title CMAP fixture open failed");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(out), "Title CMAP fixture write failed");
}

void WriteSaveLoadCmap(const fs::path& path) {
    constexpr uint32_t width = 800;
    constexpr uint32_t height = 600;
    std::vector<unsigned char> bytes(8u + width * height, 0u);
    const auto put32 = [&](size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<unsigned char>(value & 0xffu);
        bytes[offset + 1] = static_cast<unsigned char>((value >> 8u) & 0xffu);
        bytes[offset + 2] = static_cast<unsigned char>((value >> 16u) & 0xffu);
        bytes[offset + 3] = static_cast<unsigned char>((value >> 24u) & 0xffu);
    };
    put32(0u, width);
    put32(4u, height);
    // Region ID 1 -> SaveLoad control/row 0.
    for (uint32_t y = 100; y < 200; ++y) {
        for (uint32_t x = 100; x < 200; ++x) {
            bytes[8u + y * width + x] = 1u;
        }
    }
    std::ofstream out(path, std::ios::binary);
    Check(static_cast<bool>(out), "SaveLoad CMAP fixture open failed");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(out), "SaveLoad CMAP fixture write failed");
}

void WriteEmptyFontData(const fs::path& path) {
    // A complete 65,536-entry offset table with every glyph absent is a valid
    // FONTDATA document and is sufficient for presenter state/input tests.
    std::vector<unsigned char> bytes(65536u * 4u, 0u);
    std::ofstream out(path, std::ios::binary);
    Check(static_cast<bool>(out), "FONTDATA fixture open failed");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(out), "FONTDATA fixture write failed");
}

fs::path MakeSystemUiFixture() {
    const fs::path root =
        fs::temp_directory_path() / "kotonoha_title_wmv_system_ui";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "System" / "Title");
    fs::create_directories(root / "System" / "EndTitle");
    fs::create_directories(root / "System" / "SaveLoad");
    fs::create_directories(root / "System" / "MenuBar");
    fs::create_directories(root / "System" / "System");
    WriteTitleCmap(root / "System" / "Title" / "Title.cmap");
    WriteSaveLoadCmap(root / "System" / "SaveLoad" / "SaveLoad.cmap");
    WriteEmptyFontData(root / "System" / "System" / "FONTDATA.DAT");

    const auto quote = [](const fs::path& path) {
        return std::string("\"") + path.string() + "\"";
    };
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=red:s=800x450\" -frames:v 1 " +
              quote(root / "System" / "Title" / "TitleBase.PNG")),
          "BaseFile PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=green:s=800x450\" -frames:v 1 " +
              quote(root / "System" / "Title" / "Title.PNG")),
          "Title overlay PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=black@0.0:s=600x200,format=rgba\" "
              "-frames:v 1 " +
              quote(root / "System" / "Title" / "Title_Chip.PNG")),
          "Title CHIP PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=black:s=8x8\" -frames:v 1 " +
              quote(root / "System" / "SaveLoad" / "Save.PNG")),
          "SaveLoad base PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=black@0.0:s=8x8,format=rgba\" -frames:v 1 " +
              quote(root / "System" / "SaveLoad" / "SaveLoad_Chip.PNG")),
          "SaveLoad CHIP PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=red@0.5:s=800x75,format=rgba\" -frames:v 1 " +
              quote(root / "System" / "MenuBar" / "MenuBar.PNG")),
          "MenuBar base PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=black@0.0:s=799x408,format=rgba\" -frames:v 1 " +
              quote(root / "System" / "MenuBar" / "MenuBar_Chip.PNG")),
          "MenuBar CHIP PNG fixture generation failed");
    Check(RunFfmpeg(
              "-f lavfi -i \"color=c=blue:s=800x452:r=24:d=0.5\" "
              "-an -c:v wmv2 -f asf " +
              quote(root / "System" / "EndTitle" / "END-SYNTH.WMV")),
          "Title WMV fixture generation failed");
    {
        std::ofstream broken(root / "System" / "EndTitle" / "BROKEN.WMV",
                             std::ios::binary);
        broken << "not an ASF/WMV stream";
        Check(static_cast<bool>(broken), "broken Title WMV fixture write failed");
    }
    return root;
}

struct Pixel {
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
};

Pixel ReadPixel(SDL_Surface* surface, int x, int y) {
    Pixel pixel;
    Check(SDL_ReadSurfacePixel(surface, x, y, &pixel.r, &pixel.g, &pixel.b,
                               &pixel.a),
          "software surface pixel read failed");
    return pixel;
}

bool MostlyBlue(const Pixel& pixel) {
    return pixel.b > 120 && pixel.b > pixel.r * 2 && pixel.b > pixel.g * 2;
}

bool MostlyGreen(const Pixel& pixel) {
    return pixel.g > 120 && pixel.g > pixel.r * 2 && pixel.g > pixel.b * 2;
}

bool MostlyBlack(const Pixel& pixel) {
    return pixel.r < 20 && pixel.g < 20 && pixel.b < 20;
}

struct ExpectedViewport {
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
};

ExpectedViewport ExpectedStandardViewport(int width, int height) {
    const float sx = static_cast<float>(width) / 800.0f;
    const float sy = static_cast<float>(height) / 600.0f;
    const float scale = std::min(sx, sy);
    return {(static_cast<float>(width) - 800.0f * scale) * 0.5f,
            (static_cast<float>(height) - 600.0f * scale) * 0.5f,
            scale};
}

int PixelAt(float value) {
    return static_cast<int>(std::floor(value));
}

void ClearAndRender(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer) {
    Check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255),
          "software renderer clear color failed");
    Check(SDL_RenderClear(renderer), "software renderer clear failed");
    std::string error;
    Check(ui.Render(renderer, &error), error.c_str());
    SDL_RenderPresent(renderer);
}

void TestSystemUiNullOutputSafety(const fs::path& root) {
    using Ui = Kotonoha::SchoolDaysSystemUi;
    using SaveLoad = Kotonoha::SchoolDaysSaveLoadModel;

    SDL_Surface* surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Check(renderer != nullptr, "SaveLoad null-output renderer creation failed");

    auto motion = []() {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = 150.0f;
        event.motion.y = 150.0f;
        return event;
    };
    auto click = []() {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = 150.0f;
        event.button.y = 150.0f;
        return event;
    };

    Ui ui;
    std::string error;
    Check(ui.OpenSaveLoad(renderer, root.string(), SaveLoad::Variant::Standard,
                          false, false, "System/System/FONTDATA.DAT", &error),
          error.c_str());
    SDL_Event event = motion();
    Ui::Output output{};
    Check(ui.HandleEvent(renderer, &event, &output) &&
              ui.SaveLoad().Selection() == 0,
          "SaveLoad row0 hover fixture failed");

    event = click();
    Check(!ui.HandleEvent(renderer, &event, nullptr) &&
              !ui.SaveLoad().SavePending(),
          "null event output consumed/armed SaveLoad row activation");
    Check(ui.HandleEvent(renderer, &event, &output) &&
              ui.SaveLoad().SavePending(),
          "SaveLoad row activation did not arm deferred save");
    ui.Update(0u, 0, 0, nullptr);
    Check(ui.SaveLoad().SavePending(),
          "null update output consumed deferred SaveLoad request");
    ui.Update(0u, 0, 0, &output);
    Check(output.hasSaveLoadOutput &&
              output.saveLoadOutput.action == SaveLoad::Action::SaveSlotRequested &&
              output.saveLoadOutput.slot == 0 && !ui.SaveLoad().SavePending(),
          "non-null update did not publish deferred SaveLoad request");
    ui.Close();

    error.clear();
    Check(ui.OpenSaveLoad(renderer, root.string(), SaveLoad::Variant::Standard,
                          true, false, "System/System/FONTDATA.DAT", &error),
          error.c_str());
    event = motion();
    Check(ui.HandleEvent(renderer, &event, &output),
          "TextInput SaveLoad hover fixture failed");
    event = click();
    Check(!ui.HandleEvent(renderer, &event, nullptr) &&
              !ui.SaveLoad().CommentInputPending(),
          "null event output consumed/armed SaveLoad comment request");
    Check(ui.HandleEvent(renderer, &event, &output) &&
              output.hasSaveLoadOutput &&
              output.saveLoadOutput.action == SaveLoad::Action::CommentInputRequested &&
              ui.SaveLoad().CommentInputPending(),
          "non-null event did not publish SaveLoad comment request");
    ui.Close();

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void TestReplayPresenterCorpus(const fs::path& root) {
    using Ui = Kotonoha::SchoolDaysSystemUi;
    using Replay = Kotonoha::SchoolDaysReplayModel;

    const fs::path replayRoot = root / "System" / "Replay";
    if (!fs::exists(replayRoot / "REPLAY_HSCENE.PNG") &&
        !fs::exists(replayRoot / "Replay_HScene.png")) {
        return;
    }

    struct Probe {
        Replay::Variant variant;
        int width;
        int height;
    };
    const Probe probes[] = {
        {Replay::Variant::Standard, 1920, 1080},
        {Replay::Variant::Wide, 1920, 1080},
        {Replay::Variant::WideFull, 2560, 1440},
        {Replay::Variant::WideNote, 1920, 1080},
    };

    for (const Probe& probe : probes) {
        SDL_Surface* surface =
            SDL_CreateSurface(probe.width, probe.height, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        Check(renderer != nullptr, "Replay corpus renderer creation failed");

        Ui ui;
        std::string error;
        Check(ui.OpenReplay(renderer, root.string(), probe.variant, &error),
              error.c_str());
        ui.SetReplayHSceneAvailable(0, true);
        ClearAndRender(ui, renderer);
        Check(ui.IsReplayOpen() &&
                  ui.Replay().CurrentView() == Replay::View::HScene &&
                  ui.Replay().Page() == 0,
              "Replay corpus presenter state drifted");

        ui.Close();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
}

void TestSystemUiViewportScaling(const fs::path& root) {
    using Ui = Kotonoha::SchoolDaysSystemUi;
    using Background = Kotonoha::SchoolDaysEndBackgroundResolver;
    using Title = Kotonoha::SchoolDaysTitleModel;

    struct Size { int width; int height; };
    const Size sizes[] = {
        {1280, 720},
        {1920, 1080},
        {2560, 1440},
        {3840, 2160},
        {3440, 1440},
        {1366, 768},
    };

    Background::Result still;
    still.kind = Background::Kind::Base;
    still.asset = "System/Title/TitleBase.PNG";
    Title::HostState host{};

    for (const Size size : sizes) {
        SDL_Surface* surface = SDL_CreateSurface(
            size.width, size.height, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        Check(renderer != nullptr, "multi-resolution software renderer creation failed");

        Ui ui;
        std::string error;
        Check(ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                           still, "System/Title/TitleBase.PNG", &error),
              error.c_str());
        ClearAndRender(ui, renderer);

        int outputWidth = 0;
        int outputHeight = 0;
        Check(SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight),
              "multi-resolution render output query failed");
        Check(outputWidth == size.width && outputHeight == size.height,
              "software renderer output size drifted from requested surface");

        const ExpectedViewport viewport =
            ExpectedStandardViewport(outputWidth, outputHeight);
        const auto sampleVirtual = [&](float x, float y) {
            return ReadPixel(surface,
                             PixelAt(viewport.x + x * viewport.scale),
                             PixelAt(viewport.y + y * viewport.scale));
        };

        // Title.PNG is opaque green and occupies native x=[0,800),
        // y=[75,525). These probes verify the exact transformed content band.
        Check(MostlyGreen(sampleVirtual(5.0f, 300.0f)),
              "scaled Title left content edge missing");
        Check(MostlyGreen(sampleVirtual(400.0f, 300.0f)),
              "scaled Title center missing");
        Check(MostlyGreen(sampleVirtual(795.0f, 300.0f)),
              "scaled Title right content edge missing");
        Check(MostlyBlack(sampleVirtual(400.0f, 30.0f)),
              "scaled Title ignored authenticated top offset");
        Check(MostlyBlack(sampleVirtual(400.0f, 570.0f)),
              "scaled Title filled below authenticated 450-line surface");

        // Every requested output is wider than the native 4:3 CMAP, so the
        // aspect-preserving viewport must leave horizontal letterbox bars.
        Check(viewport.x > 0.0f && viewport.y == 0.0f,
              "representative output did not produce expected horizontal letterbox");
        const int leftBarX = PixelAt(viewport.x * 0.5f);
        const int rightBarX = PixelAt(
            viewport.x + 800.0f * viewport.scale + viewport.x * 0.5f);
        const int contentY = PixelAt(viewport.y + 300.0f * viewport.scale);
        Check(MostlyBlack(ReadPixel(surface, leftBarX, contentY)),
              "left letterbox bar was stretched over");
        Check(MostlyBlack(ReadPixel(surface, rightBarX, contentY)),
              "right letterbox bar was stretched over");

        ui.Close();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
}

void TestMenuBarViewportScaling(const fs::path& root) {
    using Presenter = Kotonoha::SchoolDaysMenuBarPresenter;
    using Variant = Presenter::Variant;

    const auto run = [&](Variant variant, int width, int height,
                         int insideX, int insideY, int outsideX, int outsideY) {
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        Check(renderer != nullptr, "MenuBar software renderer creation failed");

        Presenter presenter;
        std::string error;
        Check(presenter.Open(renderer, root.string(), variant, 0, 0, &error),
              error.c_str());
        Check(!presenter.FeelingLatch(),
              "MenuBar Initialize did not clear feeling latch");

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        Check(SDL_RenderClear(renderer), "MenuBar clear failed");
        Check(presenter.Render(renderer, &error), error.c_str());
        SDL_RenderPresent(renderer);

        const Pixel inside = ReadPixel(surface, insideX, insideY);
        const Pixel outside = ReadPixel(surface, outsideX, outsideY);
        Check(inside.r > 70 && inside.g < 40 && inside.b < 40,
              "MenuBar logical viewport did not render at expected position");
        Check(outside.r < 20 && outside.g < 20 && outside.b < 20,
              "MenuBar escaped its aspect-fitted logical viewport");

        presenter.RequestFeelingRefresh();
        Check(presenter.FeelingLatch(), "MenuBar callback_30(1) latch was not raised");
        presenter.Update(100u, 10, 0);   // Capture
        presenter.Update(101u, 10, 0);   // Detect
        presenter.Update(1601u, 10, 0);  // Interpolate -> PostTarget
        presenter.Update(3601u, 10, 0);  // PostTarget -> Finalize
        Check(presenter.FeelingLatch(),
              "MenuBar latch cleared before native Finalize state");
        presenter.Update(3602u, 10, 0);  // Finalize -> callback_30(0)
        Check(!presenter.FeelingLatch(),
              "MenuBar Finalize did not clear callback_30 latch");

        presenter.Close();
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    };

    // Standard keeps its authored 4:3 full-profile canvas on a 16:9 output:
    // 800x600 -> 960x720 centered at x=160, MenuBar height 90.
    run(Variant::Standard, 1280, 720, 200, 20, 50, 20);
    // Wide is an authored 16:9 profile and therefore fills 1280x720.
    run(Variant::Wide, 1280, 720, 20, 20, 20, 150);
    // Ultrawide preserves the 16:9 logical profile instead of stretching it:
    // 800x450 -> 2560x1440 centered at x=440.
    run(Variant::Wide, 3440, 1440, 500, 20, 100, 20);
}

void TestSystemUiPointerInverseMapping(const fs::path& root) {
    using Ui = Kotonoha::SchoolDaysSystemUi;
    using Background = Kotonoha::SchoolDaysEndBackgroundResolver;
    using Title = Kotonoha::SchoolDaysTitleModel;

    SDL_Window* window = SDL_CreateWindow(
        "SchoolDaysTitleInputTest", 1280, 720,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    Check(window != nullptr, "hidden Title input window creation failed");
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
    Check(renderer != nullptr, "hidden Title input renderer creation failed");

    Ui ui;
    Title::HostState host{};
    Background::Result still;
    still.kind = Background::Kind::Base;
    still.asset = "System/Title/TitleBase.PNG";
    std::string error;
    Check(ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                       still, "System/Title/TitleBase.PNG", &error),
          error.c_str());

    int outputWidth = 0;
    int outputHeight = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    Check(SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight),
          "hidden Title render output query failed");
    Check(SDL_GetWindowSize(window, &windowWidth, &windowHeight),
          "hidden Title window size query failed");
    const ExpectedViewport viewport =
        ExpectedStandardViewport(outputWidth, outputHeight);

    const auto sendMotionAtRenderPoint = [&](float renderX, float renderY) {
        float windowX = 0.0f;
        float windowY = 0.0f;
        Check(SDL_RenderCoordinatesToWindow(renderer, renderX, renderY,
                                            &windowX, &windowY),
              "render-to-window coordinate conversion failed");
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = windowX;
        event.motion.y = windowY;
        Ui::Output output{};
        Check(ui.HandleEvent(renderer, &event, &output),
              "Title mouse motion was not handled");
    };

    // CMAP region 1 occupies virtual [100,200)x[100,200). Generate the event
    // in render pixels, convert it to window coordinates through SDL, then let
    // SchoolDaysSystemUi perform its production FromWindow + viewport inverse.
    sendMotionAtRenderPoint(viewport.x + 150.0f * viewport.scale,
                            viewport.y + 150.0f * viewport.scale);
    Check(ui.Title().Selection() == 0,
          "window-to-render inverse mapping missed CMAP region 1");

    // A pointer in the horizontal letterbox must map outside the CMAP and clear
    // selection instead of selecting an edge control.
    Check(viewport.x > 1.0f, "hidden input output lacks a letterbox probe area");
    sendMotionAtRenderPoint(viewport.x * 0.5f,
                            viewport.y + 150.0f * viewport.scale);
    Check(ui.Title().Selection() == -1,
          "letterbox pointer did not map outside virtual Title coordinates");

    // On platforms where the hidden high-density window actually has a pixel
    // ratio other than 1:1, the same assertions above exercise that mismatch.
    // The dummy backend used by this self-contained gate commonly reports 1:1;
    // log both dimensions so the exercised environment is explicit.
    std::cout << "title_input_window=" << windowWidth << "x" << windowHeight
              << " render_output=" << outputWidth << "x" << outputHeight
              << " pixel_density=" << SDL_GetWindowPixelDensity(window) << '\n';

    ui.Close();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

void TestSystemUiOwnershipAndFallback(const fs::path& root) {
    using Ui = Kotonoha::SchoolDaysSystemUi;
    using Background = Kotonoha::SchoolDaysEndBackgroundResolver;
    using Title = Kotonoha::SchoolDaysTitleModel;

    SDL_Surface* surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Check(renderer != nullptr, "Title SystemUi software renderer creation failed");

    Ui ui;
    Title::HostState host{};
    Background::Result movie;
    movie.kind = Background::Kind::Ending;
    movie.asset = "System/EndTitle/END-SYNTH.WMV";
    movie.endingIndex = 0;
    std::string error;
    Check(ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                       movie, "System/Title/TitleBase.PNG", &error),
          error.c_str());

    bool sawMovie = false;
    const Uint64 movieDeadline = SDL_GetTicks() + 2000u;
    while (SDL_GetTicks() < movieDeadline) {
        ClearAndRender(ui, renderer);
        // The authenticated overlay is 450 pixels high while EndBG WMV is 452;
        // native row 451 is therefore an unambiguous movie-only probe.
        if (MostlyBlue(ReadPixel(surface, 200, 526))) {
            sawMovie = true;
            break;
        }
        SDL_Delay(5);
    }
    Check(sawMovie, "Title SystemUi WMV produced no visible frame");
    const Pixel overlayPixel = ReadPixel(surface, 50, 100);
    if (!MostlyGreen(overlayPixel)) {
        throw std::runtime_error(
            "Title overlay was not composited after EndBG movie (rgb=" +
            std::to_string(overlayPixel.r) + "," +
            std::to_string(overlayPixel.g) + "," +
            std::to_string(overlayPixel.b) + ")");
    }

    // Let the half-second one-shot exhaust, then drive enough render calls to
    // drain decoder EOF. The retained terminal movie frame must remain the
    // background instead of restarting or switching to BaseFile.
    SDL_Delay(650);
    for (int i = 0; i < 30; ++i) {
        ClearAndRender(ui, renderer);
        SDL_Delay(2);
    }
    Check(MostlyBlue(ReadPixel(surface, 200, 526)),
          "Title SystemUi did not freeze the 800x452 terminal WMV frame");
    const Pixel afterMovie = ReadPixel(surface, 200, 527);
    Check(afterMovie.r < 20 && afterMovie.g < 20 && afterMovie.b < 20,
          "Title EndBG WMV exceeded authenticated 452-pixel height");

    ui.Close();
    Check(!ui.IsTitleOpen(), "Title Close retained open model state");
    error.clear();
    Check(!ui.Render(renderer, &error),
          "Title Render succeeded after Close/decoder teardown");

    // A missing WMV is an open failure, so TitleView stays usable with the
    // configured BaseFile PNG. Reopening after Close also proves the previous
    // standalone decoder/clock do not remain presentation owners.
    Background::Result brokenMovie = movie;
    brokenMovie.asset = "System/EndTitle/BROKEN.WMV";
    error.clear();
    Check(ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                       brokenMovie, "System/Title/TitleBase.PNG", &error),
          error.c_str());
    ClearAndRender(ui, renderer);
    // Render can only succeed here when OpenTitle installed BaseFile: the movie
    // path is absent, and Render rejects Title with neither background owner.
    const Pixel afterBase = ReadPixel(surface, 200, 525);
    Check(afterBase.r < 20 && afterBase.g < 20 && afterBase.b < 20,
          "Title WMV open fallback retained stale 452-pixel movie ownership");
    ui.Close();

    // A genuinely missing WMV follows the same authenticated open-failure
    // fallback to BaseFile.  By contrast, an invalid logical path is a modern
    // extracted-mount integrity failure and must not be silently masked.
    Background::Result missingMovie = movie;
    missingMovie.asset = "System/EndTitle/MISSING.WMV";
    error.clear();
    Check(ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                       missingMovie, "System/Title/TitleBase.PNG", &error),
          error.c_str());
    ClearAndRender(ui, renderer);
    Check(ReadPixel(surface, 200, 526).r < 20 &&
              ReadPixel(surface, 200, 526).g < 20 &&
              ReadPixel(surface, 200, 526).b < 20,
          "missing Title WMV did not fall back to 450-pixel BaseFile");
    ui.Close();

    Background::Result invalidMovie = movie;
    invalidMovie.asset = "System/EndTitle/../outside.WMV";
    error.clear();
    Check(!ui.OpenTitle(renderer, root.string(), Title::Variant::Standard, host,
                        invalidMovie, "System/Title/TitleBase.PNG", &error),
          "invalid Title WMV path was silently masked by BaseFile fallback");
    Check(!error.empty(), "invalid Title WMV path returned no diagnostic");
    ui.Close();

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void TestMovie(const fs::path& file) {
    Check(fs::exists(file), "Title WMV fixture missing");
    Kotonoha_time* clock = Kotonoha_timeNew(false);
    Check(clock != nullptr, "Title movie clock allocation failed");
    Kotonoha_timePause(clock);
    Kotonoha_timeSet(clock, 0);

    Kotonoha_videoData* video = Kotonoha_VideoRenderInit(
        file.string().c_str(), clock, 0,
        std::numeric_limits<Uint64>::max(),
        std::numeric_limits<Kotonoha_SceneTick>::max());
    Check(video != nullptr, "Title WMV open failed");
    Check(video->pCodecCtx != nullptr && video->pCodecCtx->width == 800 &&
              video->pCodecCtx->height == 452,
          "Title WMV dimensions are not 800x452");
    Check(video->stream != nullptr && video->stream->requestedType == AVMEDIA_TYPE_VIDEO,
          "Title WMV subscribed to a non-video stream");

    SDL_Surface* surface = SDL_CreateSurface(800, 452, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Check(renderer != nullptr, "Title WMV software renderer creation failed");

    bool sawTexture = false;
    // Drive the same monotonic absolute 24 Hz schedule used by TitleView.
    // VideoRender is intentionally incremental; jumping straight to 25s is
    // not a request to synchronously drain every intermediate packet.
    for (Uint64 elapsed = 0; elapsed <= 30000 && !video->decoderEof;
         elapsed += 42) {
        Check(PumpTo(video, clock, renderer, elapsed, 1000),
              "Title WMV failed while following 24Hz owner clock");
        sawTexture |= video->texture != nullptr;
    }
    Check(sawTexture, "Title WMV produced no texture");
    Check(video->decoderEof, "Title WMV did not reach decoder EOF");
    SDL_Texture* const terminalTexture = video->texture;
    Check(terminalTexture != nullptr, "Title WMV discarded terminal texture");

    Kotonoha_timeSet(clock, 30000);
    const auto terminalStatus = Kotonoha_VideoRenderProcess(video, renderer);
    Check(terminalStatus == KOTONOHA_SCENE_DRAW && video->decoderEof &&
              video->texture == terminalTexture,
          "Title WMV EOF did not retain the last texture");

    // Rewinding the owner clock after EOF must not implicitly restart/loop.
    Kotonoha_timeSet(clock, 0);
    const auto rewindStatus = Kotonoha_VideoRenderProcess(video, renderer);
    Check(rewindStatus == KOTONOHA_SCENE_DRAW && video->decoderEof &&
              video->texture == terminalTexture,
          "Title WMV rewound/restarted after EOF");

    Kotonoha_VideoRenderShutdown(&video);
    Kotonoha_timeDestroy(clock);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}
} // namespace

int main(int argc, char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        const fs::path fixture = MakeSystemUiFixture();
        TestMovie(fixture / "System" / "EndTitle" / "END-SYNTH.WMV");
        TestSystemUiOwnershipAndFallback(fixture);
        TestSystemUiViewportScaling(fixture);
        TestMenuBarViewportScaling(fixture);
        TestSystemUiPointerInverseMapping(fixture);
        TestSystemUiNullOutputSafety(fixture);
        std::error_code cleanupError;
        fs::remove_all(fixture, cleanupError);

        // If a caller supplies an extracted corpus, retain the two authenticated
        // 800x452 probes as an optional local parity check. The synthetic gate
        // above is the self-contained regression test and never needs the game.
        if (argc == 2) {
            const fs::path root(argv[1]);
            const fs::path sb = root / "System" / "EndTitle" / "END-05-SB-E01.WMV";
            const fs::path se = root / "System" / "EndTitle" / "END-05-SE-S00.WMV";
            if (fs::exists(sb)) TestMovie(sb);
            if (fs::exists(se)) TestMovie(se);
            TestReplayPresenterCorpus(root);
        }
        SDL_Quit();
        std::cout << "SCHOOL DAYS TITLE MOVIE RUNTIME GATE PASS\n"
                  << "wmv_800x452=PASS\n"
                  << "video_only_backend=PASS\n"
                  << "manual_24hz_targets=PASS\n"
                  << "eof_retains_terminal_texture=PASS\n"
                  << "no_implicit_loop=PASS\n"
                  << "system_ui_overlay_order=PASS\n"
                  << "system_ui_wmv_800x452=PASS\n"
                  << "system_ui_open_failure_basefile_fallback=PASS\n"
                  << "system_ui_missing_wmv_basefile_fallback=PASS\n"
                  << "system_ui_invalid_wmv_fail_closed=PASS\n"
                  << "system_ui_close_teardown=PASS\n"
                  << "system_ui_multires_viewport=PASS\n"
                  << "system_ui_aspect_letterbox=PASS\n"
                  << "system_ui_pointer_inverse_mapping=PASS\n"
                  << "system_ui_null_output_nonconsuming=PASS\n";
        std::cout << "menubar_multires_runtime=PASS\n"
                  << "menubar_latch_lifecycle=PASS\n";
        if (argc == 2) std::cout << "replay_real_assets_multires=PASS\n";
        return 0;
    } catch (const std::exception& e) {
        SDL_Quit();
        std::cerr << "SchoolDaysTitleMovieRuntimeTest FAIL: " << e.what() << '\n';
        return 1;
    }
}
