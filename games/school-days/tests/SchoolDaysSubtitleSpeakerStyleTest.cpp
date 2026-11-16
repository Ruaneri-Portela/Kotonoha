#include "SchoolDaysScenes.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

// Standalone School Days fixture: no retail files, save data or real GPU.
constexpr const char *kStyles =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
    "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,Noto Sans JP,46,&H000000FF,&H000000FF,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Makoto,Noto Sans JP,46,&H0000FF00,&H0000FF00,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Setsuna,Noto Sans JP,46,&H00FF0000,&H00FF0000,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Sekai's Mother,Noto Sans JP,46,&H00FFFF00,&H00FFFF00,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Sekai&Makoto,Noto Sans JP,46,&H00FF00FF,&H00FF00FF,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Makoto Makoto\xE3\x83\xBB"
    "Sekai Sekai,Noto Sans JP,46,&H0000FFFF,&H0000FFFF,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: M\xC3\xA3"
    "e da Sekai,Noto Sans JP,46,&H000080FF,&H000080FF,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
    "Style: Ni\xC3\xB1"
    "o,Noto Sans JP,46,&H00FF0080,&H00FF0080,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n";

struct Fixture {
  fs::path directory;
  fs::path style;

  explicit Fixture(const fs::path &root)
      : directory(root / ("speaker-" +
                          std::to_string(SDL_GetPerformanceCounter()))),
        style(directory / "styles.ass") {
    fs::create_directories(directory);
    std::ofstream output(style, std::ios::binary);
    output << kStyles;
    Require(bool(output), "Cannot create synthetic styles.ass");
  }

  ~Fixture() {
    std::error_code error;
    fs::remove_all(directory, error);
  }
};

constexpr std::array<std::array<int, 3>, 8> kColors{{
    {255, 0, 0},     // Default
    {0, 255, 0},     // Makoto
    {0, 0, 255},     // Setsuna
    {0, 255, 255},   // Sekai's Mother
    {255, 0, 255},   // Sekai&Makoto
    {255, 255, 0},   // Makoto Makoto (interpunct) Sekai Sekai
    {255, 128, 0},   // Mae da Sekai (accented)
    {128, 0, 255},   // Nino (Spanish enye)
}};

std::array<int, kColors.size()> Colors(Kotonoha::RendererRef renderer) {
  Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
  Require(bool(pixels), "Cannot read SDL software-rendered subtitle pixels");
  std::array<int, kColors.size()> counts{};
  for (int y = 0; y < pixels->h; ++y)
    for (int x = 0; x < pixels->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      Require(SDL_ReadSurfacePixel(pixels.get(), x, y, &r, &g, &b, &a),
              "Cannot inspect synthetic subtitle pixel");
      for (std::size_t i = 0; i < kColors.size(); ++i)
        if (std::abs(int(r) - kColors[i][0]) <= 15 &&
            std::abs(int(g) - kColors[i][1]) <= 15 &&
            std::abs(int(b) - kColors[i][2]) <= 15)
          ++counts[i];
    }
  return counts;
}

void ExpectColor(SchoolDays::Subtitles &scene, Kotonoha::RendererRef renderer,
                 double second, std::size_t index) {
  Require(SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(&renderer),
          "Cannot clear the SDL software renderer");
  scene.setPosition(second);
  scene.render(renderer, 1);
  const auto counts = Colors(renderer);
  Require(counts[index] > 100, "The speaker's ASS style color was not drawn");
  for (std::size_t i = 0; i < counts.size(); ++i)
    if (i != index)
      Require(counts[i] == 0, "A different ASS speaker style was selected");
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Usage: SchoolDaysSubtitleSpeakerStyleTest build-fixtures");
    Fixture fixture(argv[1]);
    Kotonoha::EngineOptions options;
    options.rendererOverride = "software";
    options.persistSettings = false;
    options.subtitleStylePath = fixture.style.string();
    Kotonoha::Runtime runtime("School Days synthetic ASS style", "1",
                              "org.kotonoha.schooldays.ass-test");
    Kotonoha::Engine engine(runtime, options);
    Require(engine.GetSubtitleStyle().has_value(),
            "Engine did not load synthetic styles.ass");
    const auto &names = engine.GetSubtitleStyle()->names;
    const std::vector<std::string> expectedNames{
        "Default", "Makoto", "Setsuna", "Sekai's Mother",
        "Sekai&Makoto", "Makoto Makoto\xE3\x83\xBB" "Sekai Sekai",
        "M\xC3\xA3" "e da Sekai", "Ni\xC3\xB1" "o"};
    Require(names == expectedNames,
            "ASS fixture style names were not recognized");

    const std::vector<Kotonoha::SubtitleCue> original{
        {0, 1, "COLOR", "Makoto", true},
        {1, 2, "COLOR", "SpeakerNotInStyles", true},
        {2, 3, "COLOR", "Setsuna", true},
        {3, 4, "COLOR", "Sekai's Mother", true},
        {4, 5, "COLOR", "Sekai&Makoto", true},
        {5, 6, "COLOR", "Makoto Makoto\xE3\x83\xBB" "Sekai Sekai", true},
        {6, 7, "COLOR", "M\xC3\xA3" "e da Sekai", true},
        {7, 8, "COLOR", "Ni\xC3\xB1" "o", true}};
    Kotonoha::Translation translation;
    auto localized = SchoolDays::Subtitles::TranslateCues(
        original, {0, 1, 2, 3, 4, 5, 6, 7}, 1, translation);
    Require(localized.size() == original.size(), "Cue count changed");
    for (std::size_t i = 0; i < original.size(); ++i)
      Require(localized[i].speaker == original[i].speaker,
              "School Days translation changed a canonical ASS speaker ID");

    SchoolDays::Subtitles scene(engine);
    scene.setCues(localized, 46);
    auto &renderer = engine.GetRenderer();
    ExpectColor(scene, renderer, 0.5, 1); // Matching Makoto.
    ExpectColor(scene, renderer, 1.5, 0); // Unknown -> Default.
    ExpectColor(scene, renderer, 2.5, 2); // Matching Setsuna.
    ExpectColor(scene, renderer, 3.5, 3); // Apostrophe and space.
    ExpectColor(scene, renderer, 4.5, 4); // Ampersand.
    ExpectColor(scene, renderer, 5.5, 5); // Interpunct and spaces.
    ExpectColor(scene, renderer, 6.5, 6); // Accented a.
    ExpectColor(scene, renderer, 7.5, 7); // Spanish enye.
    std::cout << "School Days synthetic ASS speaker styles PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysSubtitleSpeakerStyleTest: " << error.what()
              << '\n';
    return 1;
  }
}
