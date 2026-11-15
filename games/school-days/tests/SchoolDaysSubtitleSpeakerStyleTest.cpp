#include "SchoolDaysScenes.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>

#include <array>
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

std::array<int, 3> Colors(Kotonoha::RendererRef renderer) {
  Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
  Require(bool(pixels), "Cannot read SDL software-rendered subtitle pixels");
  std::array<int, 3> counts{}; // Default red, Makoto green, Setsuna blue.
  for (int y = 0; y < pixels->h; ++y)
    for (int x = 0; x < pixels->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      Require(SDL_ReadSurfacePixel(pixels.get(), x, y, &r, &g, &b, &a),
              "Cannot inspect synthetic subtitle pixel");
      if (r > 140 && g < 80 && b < 80)
        ++counts[0];
      if (g > 140 && r < 80 && b < 80)
        ++counts[1];
      if (b > 140 && r < 80 && g < 80)
        ++counts[2];
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
    Require(names.size() == 3 && names[0] == "Default" &&
                names[1] == "Makoto" && names[2] == "Setsuna",
            "ASS fixture style names were not recognized");

    const std::vector<Kotonoha::SubtitleCue> original{
        {0, 1, "COLOR", "Makoto", true},
        {1, 2, "COLOR", "SpeakerNotInStyles", true},
        {2, 3, "COLOR", "Setsuna", true}};
    Kotonoha::Translation translation;
    auto localized = SchoolDays::Subtitles::TranslateCues(
        original, {0, 1, 2}, 1, translation);
    Require(localized.size() == 3 && localized[0].speaker == "Makoto" &&
                localized[1].speaker == "SpeakerNotInStyles" &&
                localized[2].speaker == "Setsuna",
            "School Days translation changed a canonical ASS speaker ID");

    SchoolDays::Subtitles scene(engine);
    scene.setCues(localized, 46);
    auto &renderer = engine.GetRenderer();
    ExpectColor(scene, renderer, 0.5, 1); // Matching Makoto.
    ExpectColor(scene, renderer, 1.5, 0); // Unknown -> Default.
    ExpectColor(scene, renderer, 2.5, 2); // Matching Setsuna.
    std::cout << "School Days synthetic ASS speaker styles PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysSubtitleSpeakerStyleTest: " << error.what()
              << '\n';
    return 1;
  }
}
