#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Model = Kotonoha::SchoolDaysConfigMenuModel;
using Plan = Kotonoha::SchoolDaysConfigMenuRenderPlan;
namespace fs = std::filesystem;

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "SchoolDaysConfigMenuDisplayTest: " << message << '\n';
  std::exit(2);
}

void Check(bool value, const std::string &message) {
  if (!value)
    Fail(message);
}

bool Near(float a, float b) { return std::abs(a - b) <= 0.02f; }

// 800x450 CMAP rectangles from the user's corrected Display package.
struct HitRect {
  int left, top, right, bottom;
};
constexpr std::array<HitRect, 22> kDisplayHits{{
    {10, 55, 202, 90},    {204, 55, 396, 90},   {398, 55, 592, 90},
    {707, 9, 799, 43},    {245, 115, 753, 140}, {245, 149, 360, 174},
    {371, 149, 486, 174}, {245, 181, 360, 206}, {371, 181, 486, 206},
    {245, 214, 753, 240}, {245, 247, 753, 274}, {245, 281, 640, 306},
    {649, 281, 754, 306}, {245, 317, 313, 344}, {318, 317, 385, 344},
    {391, 317, 458, 344}, {466, 317, 532, 344}, {539, 317, 607, 344},
    {612, 317, 681, 344}, {689, 317, 756, 344}, {441, 380, 599, 411},
    {613, 380, 774, 411},
}};

struct VariantCase {
  Model::Variant variant;
  const char *suffix;
  int width;
  int height;
  float scale;
  float yOffset;
};

constexpr std::array<VariantCase, 4> kVariants{{
    {Model::Variant::Standard, "", 800, 600, 1.0f, 75.0f},
    {Model::Variant::Wide, "_Wide", 800, 450, 1.0f, 0.0f},
    {Model::Variant::WideFull, "_Wide_Full", 1280, 720, 1.6f, 0.0f},
    {Model::Variant::WideNote, "_Wide_Note", 1024, 576, 1.28f, 0.0f},
}};

std::pair<int, int> Center(const HitRect &r, const VariantCase &variant) {
  return {
      static_cast<int>(std::lround((r.left + r.right) / 2.0f * variant.scale)),
      static_cast<int>(std::lround(
          ((r.top + r.bottom) / 2.0f + variant.yOffset) * variant.scale))};
}

void PutLe32(std::vector<uint8_t> &bytes, size_t offset, int value) {
  for (int shift = 0; shift < 32; shift += 8)
    bytes[offset + shift / 8] = static_cast<uint8_t>((value >> shift) & 0xff);
}

void SyntheticCmap(const fs::path &root, const VariantCase &variant) {
  std::vector<uint8_t> contents(8u + static_cast<size_t>(variant.width) *
                                         static_cast<size_t>(variant.height));
  PutLe32(contents, 0, variant.width);
  PutLe32(contents, 4, variant.height);
  for (size_t control = 0; control < kDisplayHits.size(); ++control) {
    const auto [x, y] = Center(kDisplayHits[control], variant);
    Check(x >= 0 && x < variant.width && y >= 0 && y < variant.height,
          "synthetic CMAP control center is outside the authoring canvas");
    contents[8u + static_cast<size_t>(y) * variant.width + x] =
        static_cast<uint8_t>(control + 1u);
  }
  const auto relative =
      Model::CmapRelativePath(Model::Page::Display, variant.variant);
  const auto path = root / fs::path(relative);
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(contents.data()),
             static_cast<std::streamsize>(contents.size()));
  Check(static_cast<bool>(file), "cannot create synthetic CMAP fixture");
}

void SyntheticRetailDisplayFallbackCmap(const fs::path &root,
                                        const VariantCase &variant) {
  // Emulate the original Android GPK: no Option_Display.* resources exist.
  // General's CMAP owns the shared tab/Close regions and its own control 4.
  std::vector<uint8_t> contents(8u + static_cast<size_t>(variant.width) *
                                         static_cast<size_t>(variant.height));
  PutLe32(contents, 0, variant.width);
  PutLe32(contents, 4, variant.height);
  for (int control = 0; control <= 4; ++control) {
    const auto [x, y] =
        Center(kDisplayHits[static_cast<size_t>(control)], variant);
    contents[8u + static_cast<size_t>(y) * variant.width + x] =
        static_cast<uint8_t>(control + 1);
  }
  const auto path = root / fs::path(Model::CmapRelativePath(Model::Page::Def,
                                                            variant.variant));
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(contents.data()),
             static_cast<std::streamsize>(contents.size()));
  Check(static_cast<bool>(file), "cannot create retail fallback CMAP fixture");
}

void VerifyAction(int control, const Model::Action &action) {
  if (control <= 2) {
    Check(action.kind == Model::ActionKind::SelectPage &&
              action.page == static_cast<Model::Page>(control),
          "Display tab action did not select the matching page");
  } else if (control == 3) {
    Check(action.kind == Model::ActionKind::Close,
          "Display Close action was not recognized");
  } else {
    Check(action.kind == Model::ActionKind::DisplayControl &&
              action.value == control,
          "Display control was not forwarded with its original CMAP ID");
  }
}

const Plan::Sprite *Find(const std::vector<Plan::Sprite> &sprites,
                         Plan::Role role, int control) {
  for (const auto &sprite : sprites)
    if (sprite.role == role && sprite.control == control)
      return &sprite;
  return nullptr;
}

void VerifySource(const Plan::Sprite &sprite, const VariantCase &variant) {
  Check(sprite.texture == Plan::Texture::Chip && sprite.hasSource &&
            sprite.source.x >= 0.0f && sprite.source.y >= 0.0f &&
            sprite.source.w > 0.0f && sprite.source.h > 0.0f &&
            sprite.source.x + sprite.source.w <= 684.0f &&
            sprite.source.y + sprite.source.h <= 1212.0f,
        "CHIP crop escaped the user-supplied 684x1212 atlas");
  Check(
      sprite.destination.x >= -1.0f && sprite.destination.y >= -1.0f &&
          sprite.destination.x + sprite.destination.w <= variant.width + 2.0f &&
          sprite.destination.y + sprite.destination.h <= variant.height + 2.0f,
      "Display sprite destination escaped the variant CMAP canvas");
}

void VerifyDisplayPage(Model &menu, const VariantCase &variant) {
  const auto layout = Plan::LayoutFor(variant.variant);
  Check(Near(layout.scale, variant.scale) &&
            Near(layout.yOffset, variant.yOffset) &&
            layout.logicalWidth == variant.width &&
            layout.logicalHeight == variant.height,
        "variant layout differs from corrected CMAP dimensions");
  Check(menu.CurrentPage() == Model::Page::Display &&
            menu.Cmap().Width() == static_cast<unsigned>(variant.width) &&
            menu.Cmap().Height() == static_cast<unsigned>(variant.height),
        "Display page loaded the wrong CMAP geometry");

  Plan::VisualState state;
  state.displayResizable = 5;
  state.displayVsync = 8;
  state.displayFactor = 16;
  auto sprites = Plan::Build(menu, state);
  Check(
      sprites.size() == 4 && sprites.front().texture == Plan::Texture::Base &&
          sprites.front().role == Plan::Role::Base &&
          !sprites.front().hasSource &&
          Near(sprites.front().destination.y, variant.yOffset * variant.scale),
      "Display should begin with the unmodified official 800x450 base");
  for (const auto &sprite : sprites) {
    Check(
        sprite.role != Plan::Role::ActiveTab,
        "Display's silver tab must not have a permanent blue selected overlay");
    if (sprite.texture == Plan::Texture::Chip)
      VerifySource(sprite, variant);
  }
  Check(Find(sprites, Plan::Role::CurrentState, 3) == nullptr &&
            Find(sprites, Plan::Role::CurrentState, 5) != nullptr &&
            Find(sprites, Plan::Role::CurrentState, 8) != nullptr &&
            Find(sprites, Plan::Role::CurrentState, 16) != nullptr,
        "Display must preserve the base Close and selected switch/factor "
        "highlights");
  Check(
      Near(Find(sprites, Plan::Role::CurrentState, 5)->source.y, 494.0f) &&
          Near(Find(sprites, Plan::Role::CurrentState, 8)->source.y, 536.0f) &&
          Near(Find(sprites, Plan::Role::CurrentState, 16)->source.y, 704.0f),
      "selected states do not use their corresponding CHIP atlas bands");

  for (int control = 0; control < static_cast<int>(kDisplayHits.size());
       ++control) {
    const auto [x, y] = Center(kDisplayHits[control], variant);
    Check(menu.HitControl(x, y) == control,
          "CMAP region did not decode to control ID 0..21");
    menu.SetSelectionFromPointer(x, y);
    Check(menu.Selection() == control,
          "pointer selection lost the Display control ID");
    Model::Action action;
    Check(menu.Activate(&action), "Display control was not actionable");
    VerifyAction(control, action);

    const auto hovered = Plan::Build(menu, state);
    if (control == 3) {
      Check(Find(hovered, Plan::Role::Hover, control) == nullptr,
            "Close must not crop the absent selected/hover atlas variants");
    } else {
      const auto *sprite = Find(hovered, Plan::Role::Hover, control);
      Check(sprite != nullptr,
            "CHIP hover overlay missing for Display control");
      VerifySource(*sprite, variant);
      Check(sprite->source.y >= 798.0f && sprite->source.y <= 1183.0f,
            "Display hover came from normal/selected instead of hover band");
      if (control == 15)
        Check(Near(sprite->source.y, 1108.0f),
              "2x hover source is vertically offset: duplicate glyph appears");
    }
  }
  menu.SetSelectionFromPointer(-1, -1);
  Check(menu.Selection() == -1 && Plan::Build(menu, state).size() == 4,
        "pointer leaving Display should clear hover without losing state");

  state.displayResizable = state.displayVsync = state.displayFactor = -1;
  const auto noState = Plan::Build(menu, state);
  Check(noState.size() == 1 &&
            Find(noState, Plan::Role::CurrentState, 3) == nullptr,
        "unknown runtime state should not manufacture enabled values");
}

void VerifyDisplayPaths() {
  Check(std::string(Model::PageToken(Model::Page::Display)) == "Display" &&
            Model::BaseRelativePath(Model::Page::Display) ==
                "System/Option/Option_Display.png" &&
            Model::ChipRelativePath(Model::Page::Display) ==
                "System/Option/Option_Display_Chip.png",
        "third tab still resolves old Def/SOMCON artwork");
  for (const auto &variant : kVariants) {
    Check(Model::CmapRelativePath(Model::Page::Display, variant.variant) ==
              std::string("System/Option/Option_Display") + variant.suffix +
                  ".cmap",
          "Display variant does not resolve corrected CMAP file");
  }
  Check(Model::BaseRelativePath(Model::Page::Def) ==
                "System/Option/Option_Def.png" &&
            Model::ChipRelativePath(Model::Page::Sound) ==
                "System/Option/Option_Sound_Chip.png" &&
            Model::CmapRelativePath(Model::Page::Sound, Model::Variant::Wide) ==
                "System/Option/Option_Sound_Wide.cmap",
        "General/Sound asset paths were altered");
}

void VerifyExistingPages(const fs::path &root) {
  const auto option = root / "System/Option";
  for (const char *page : {"Def", "Sound"}) {
    const auto path = option / (std::string("Option_") + page + ".cmap");
    const std::array<uint8_t, 10> cmap{{2, 0, 0, 0, 1, 0, 0, 0, 7, 8}};
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(cmap.data()), cmap.size());
  }
  Model menu;
  Check(menu.Open(root.string(), Model::Variant::Standard, Model::Page::Def),
        "General fixture did not open");
  const auto def = Plan::Build(menu, {});
  Check(Find(def, Plan::Role::ActiveTab, 0) != nullptr &&
            Near(Find(def, Plan::Role::ActiveTab, 0)->source.y, 149.0f),
        "authenticated General chip plan unexpectedly changed");
  Check(menu.Open(root.string(), Model::Variant::Standard, Model::Page::Sound),
        "Sound fixture did not open");
  Plan::VisualState sound;
  sound.soundLevels = {{0, 0, 0}};
  const auto sprites = Plan::Build(menu, sound);
  int empty = 0;
  for (const auto &sprite : sprites)
    if (sprite.role == Plan::Role::SoundLevel) {
      ++empty;
      Check(sprite.source.w == 0.0f,
            "Sound zero gain should preserve original zero-width source");
    }
  Check(empty == 3 && Find(sprites, Plan::Role::ActiveTab, 1) != nullptr,
        "authenticated Sound chip plan unexpectedly changed");
}

struct Fixture {
  fs::path root;
  Fixture() {
    root = fs::temp_directory_path() /
           ("schooldays-display-model-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "System/Option");
  }
  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

void VerifyAndroidWithoutCustomDisplayAssets() {
  Fixture fixture;
  for (const auto &variant : kVariants)
    SyntheticRetailDisplayFallbackCmap(fixture.root, variant);
  Check(!fs::exists(fixture.root / "System/Option/Option_Display.png") &&
            !fs::exists(fixture.root / "System/Option/Option_Display.cmap"),
        "Android fixture must have no Windows-only display assets");
  for (const auto &variant : kVariants) {
    Model model;
    Check(model.Open(fixture.root.string(), variant.variant,
                     Model::Page::Display, nullptr, true),
          "Android Display should open from the original General CMAP");
    Check(model.CurrentPage() == Model::Page::Display &&
              model.ArtworkPage() == Model::Page::Def &&
              model.Cmap().Width() == static_cast<unsigned>(variant.width) &&
              model.Cmap().Height() == static_cast<unsigned>(variant.height),
          "Android fallback must preserve logical Display and CMAP geometry");
    Check(Plan::BaseRelativePath(model) == "System/Option/Option_Def.png" &&
              Model::ChipRelativePath(model.ArtworkPage()) ==
                  "System/Option/Option_Def_Chip.png",
          "Android Display fallback must only reference original artwork");
    const auto plan = Plan::Build(model, {});
    Check(plan.size() == 1 && plan.front().role == Plan::Role::Base,
          "Android fallback must not sample Windows-only CHIP sprites");
    for (int control = 0; control <= 3; ++control) {
      const auto [x, y] =
          Center(kDisplayHits[static_cast<size_t>(control)], variant);
      Check(model.HitControl(x, y) == control,
            "Android shared Config navigation/Close hotspot changed");
      model.SetSelectionFromPointer(x, y);
      Model::Action action;
      Check(model.Activate(&action), "Android navigation/Close must activate");
      VerifyAction(control, action);
    }
    const auto [x, y] = Center(kDisplayHits[4], variant);
    Check(model.HitControl(x, y) == -1,
          "Android fallback must ignore Def-only settings under Display");
  }
}

void VerifyRealOverlay(const fs::path &root) {
  Check(fs::is_regular_file(root /
                            Model::BaseRelativePath(Model::Page::Display)) &&
            fs::is_regular_file(root /
                                Model::ChipRelativePath(Model::Page::Display)),
        "corrected overlay lacks official Display base/CHIP PNGs");
  Model model;
  for (const auto &variant : kVariants) {
    Check(model.Open(root.string(), variant.variant, Model::Page::Display),
          "corrected real overlay variant CMAP did not load");
    VerifyDisplayPage(model, variant);
  }
  std::cout << "Display REAL 4 CMAP overlay PASS\n";
}
} // namespace

int main(int argc, char **argv) {
  VerifyDisplayPaths();
  Fixture fixture;
  for (const auto &variant : kVariants)
    SyntheticCmap(fixture.root, variant);
  Model model;
  for (const auto &variant : kVariants) {
    Check(model.Open(fixture.root.string(), variant.variant,
                     Model::Page::Display),
          "synthetic Display variant CMAP did not open");
    VerifyDisplayPage(model, variant);
  }
  VerifyExistingPages(fixture.root);
  VerifyAndroidWithoutCustomDisplayAssets();
  if (argc > 1)
    VerifyRealOverlay(argv[1]);
  std::cout << "SchoolDaysConfigMenuDisplayTest PASS\n";
}
