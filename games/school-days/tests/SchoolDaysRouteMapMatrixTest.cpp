#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Model = Kotonoha::SchoolDaysRouteMapModel;
using MapPlan = Kotonoha::SchoolDaysRouteMapRenderPlan;
using SaveModel = Kotonoha::SchoolDaysSaveLoadModel;
using SavePlan = Kotonoha::SchoolDaysSaveLoadRenderPlan;

namespace {

void Require(bool condition, const std::string &reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

struct Dimensions {
  int width;
  int height;
};

Dimensions ForVariant(Model::Variant variant) {
  switch (variant) {
  case Model::Variant::Standard: return {800, 600};
  case Model::Variant::Wide: return {800, 450};
  case Model::Variant::WideFull: return {1280, 720};
  case Model::Variant::WideNote: return {1024, 576};
  }
  throw std::runtime_error("Unknown Route Map variant");
}

// The retail maps have been independently inventoried. This test does not
// reproduce or copy them: write an intentionally minimal synthetic CMAP with
// one pixel for every control, so all page/selector state transitions can be
// tested in DEV and DIST without running the commercial executable.
void WriteSyntheticMap(const fs::path &root, Model::Variant variant,
                       int major, int subpage) {
  const auto dims = ForVariant(variant);
  const auto page = Model::Page(major, subpage);
  const auto path = root / Model::CmapRelativePath(major, subpage, variant);
  Require(!path.empty(), "Route Map CMAP path missing");
  fs::create_directories(path.parent_path());
  std::vector<uint8_t> bytes(8u + static_cast<size_t>(dims.width) *
                                     static_cast<size_t>(dims.height), 0u);
  for (int i = 0; i < 4; ++i) {
    bytes[static_cast<size_t>(i)] =
        static_cast<uint8_t>(dims.width >> (8 * i));
    bytes[static_cast<size_t>(i + 4)] =
        static_cast<uint8_t>(dims.height >> (8 * i));
  }
  auto stamp = [&](int x, int control) {
    bytes[8u + 2u * static_cast<size_t>(dims.width) +
          static_cast<size_t>(x)] = static_cast<uint8_t>(control + 1);
  };
  for (int control = 0; control <= 10; ++control)
    stamp(control + 1, control);
  for (int local = 0; local < page.nodeCount; ++local)
    stamp(30 + local, 11 + local);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  Require(bool(file), "Could not write synthetic Route Map CMAP");
}

void CheckLoadButtonHover(const fs::path &root, Model::Variant variant) {
  const auto saveVariant = static_cast<SaveModel::Variant>(variant);
  const auto dims = ForVariant(variant);
  const auto path = root / "hover-cmap" /
                    (std::to_string(static_cast<int>(variant)) + ".cmap");
  fs::create_directories(path.parent_path());
  std::vector<uint8_t> bytes(8u + static_cast<size_t>(dims.width) *
                                     static_cast<size_t>(dims.height), 0u);
  for (int i = 0; i < 4; ++i) {
    bytes[static_cast<size_t>(i)] =
        static_cast<uint8_t>(dims.width >> (8 * i));
    bytes[static_cast<size_t>(i + 4)] =
        static_cast<uint8_t>(dims.height >> (8 * i));
  }
  bytes[8u + 60u * static_cast<size_t>(dims.width) + 12u] = 22u;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(bool(out), "Could not write synthetic SaveLoad CMAP");
  out.close();

  auto hasBlueMapButton = [](const SaveModel &model) {
    const auto plan = SavePlan::Build(model);
    return std::any_of(plan.begin(), plan.end(), [](const auto &sprite) {
      return sprite.role == SavePlan::Role::RouteMap;
    });
  };
  std::string error;
  SaveModel model;
  Require(model.Open(path.string(), saveVariant, SaveModel::Mode::Load,
                     false, &error), "LOAD CMAP could not open: " + error);
  Require(model.Selection() == -1 && !hasBlueMapButton(model),
          "LOAD Open Route Map has a permanent blue overlay at rest");
  model.RestoreSelection(21); // Original route->LOAD flow saves control 21.
  Require(model.Selection() == -1 && !hasBlueMapButton(model),
          "Returning from Route Map restored a stale blue LOAD button");
  Require(model.SetSelectionFromPointer(12, 60) &&
              model.Selection() == 21 && hasBlueMapButton(model),
          "LOAD Open Route Map fails to highlight when hovered");
  Require(model.SetSelectionFromPointer(-1, -1) &&
              model.Selection() == -1 && !hasBlueMapButton(model),
          "LOAD Open Route Map blue hover did not clear on pointer exit");
  model.Close();
  Require(model.Open(path.string(), saveVariant, SaveModel::Mode::Save,
                     false, &error), "SAVE CMAP could not open: " + error);
  (void)model.SetSelectionFromPointer(12, 60);
  Require(model.Selection() == -1 && !hasBlueMapButton(model),
          "SAVE mode must never expose Open Route Map");
}

void CheckPage(const fs::path &root, Model::Variant variant, int major,
               int subpage, std::array<int, 6> &episodeNodes) {
  Model model;
  std::string error;
  Require(model.Open(root.string(), variant, major, subpage, false, &error),
          "Synthetic Route Map did not open: " + error);
  const auto dims = ForVariant(variant);
  Require(model.Cmap().Width() == dims.width &&
              model.Cmap().Height() == dims.height,
          "Route Map variant dimensions changed");
  Require(model.Major() == major && model.Subpage() == subpage,
          "Route Map opened wrong episode/subpage");
  const auto hasBlueClose = [&] {
    const auto plan = MapPlan::Build(model);
    return std::any_of(plan.begin(), plan.end(), [](const auto &sprite) {
      return sprite.role == MapPlan::Role::CommonControl && sprite.index == 6;
    });
  };
  Require(model.Selection() == -1 && !hasBlueClose(),
          "Route Map Close button has a permanent blue overlay at rest");
  Require(model.SetSelectionFromPointer(7, 2) &&
              model.Selection() == 6 && hasBlueClose(),
          "Route Map Close button fails to highlight on hover");
  Require(model.SetSelectionFromPointer(-1, -1) &&
              model.Selection() == -1 && !hasBlueClose(),
          "Route Map Close blue hover did not clear on pointer exit");
  const auto page = Model::Page(major, subpage);
  episodeNodes[static_cast<size_t>(major)] += page.nodeCount;
  for (int local = 0; local < page.nodeCount; ++local) {
    const int selector = (major + 1) * 100 + page.nodeOffset + local;
    const int x = 30 + local;
    Require(Model::NodeSelector(major, subpage, local) == selector,
            "Route Map selector sequence changed");
    char expected[16]{};
    std::snprintf(expected, sizeof(expected), "SP%03d", selector);
    Require(Model::NodeSpKey(major, subpage, local) == expected,
            "Route Map SP key mismatches hotspot");
    Require(model.HitControl(x, 2) == 11 + local,
            "Route Map CMAP slot does not address its node");
    model.SetSelectionFromPointer(x, 2);
    Model::Action action;
    Require(!model.IsControlAvailable(11 + local) &&
                !model.Activate(&action, &error),
            "Locked Route Map node unexpectedly activates");
    model.SetNodeAvailable(local, true);
    model.SetNodeHistory(local, true);
    Require(model.NodeHistory(local) && model.IsControlAvailable(11 + local) &&
                model.Activate(&action, &error) &&
                action.kind == Model::ActionKind::NodeRequested &&
                action.selector == selector,
            "Unlocked Route Map node requested wrong checkpoint");
  }
  Require(!model.NodeAvailable(page.nodeCount) &&
              Model::NodeSelector(major, subpage, page.nodeCount) == -1,
          "Route Map exposed node beyond the page boundary");
  Require(model.HitControl(-1, 2) == -1 &&
              model.HitControl(dims.width, 2) == -1,
          "Route Map accepted pointer outside its CMAP");

  // Verify all four navigation classes. Current chapter and subpage
  // availability are independent of the node GlobalFlag projection.
  for (int control = 6; control <= 10; ++control) {
    const bool expected =
        control == 6 ||
        (control == 7 && major > 0) ||
        (control == 8 && major < 5) ||
        (control == 9 && subpage > 0) ||
        (control == 10 &&
         subpage + 1 < Model::SubpageCountForMajor(major));
    Require(model.IsControlAvailable(control) == expected,
            "Route Map navigation availability regressed");
  }
  model.SetSelectionFromPointer(7, 2); // control 6: ReturnToLoad
  Model::Action action;
  Require(model.Activate(&action, &error) &&
              action.kind == Model::ActionKind::ReturnToLoad,
          "Route Map ReturnToLoad action regressed");
  model.Close();
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Expected isolated synthetic Route Map root");
    const fs::path root(argv[1]);
    constexpr std::array<int, 6> pages{{1, 1, 2, 4, 4, 4}};
    constexpr std::array<int, 6> hotspots{{4, 9, 11, 35, 57, 45}};
    constexpr std::array<Model::Variant, 4> variants{{
        Model::Variant::Standard, Model::Variant::Wide,
        Model::Variant::WideFull, Model::Variant::WideNote}};
    for (const auto variant : variants) {
      CheckLoadButtonHover(root, variant);
      std::array<int, 6> nodes{};
      for (int major = 0; major < 6; ++major) {
        Require(Model::SubpageCountForMajor(major) ==
                    pages[static_cast<size_t>(major)],
                "Route Map chapter page count differs from retail inventory");
        for (int subpage = 0; subpage < pages[static_cast<size_t>(major)];
             ++subpage)
          WriteSyntheticMap(root, variant, major, subpage);
      }
      for (int major = 0; major < 6; ++major) {
        for (int subpage = 0; subpage < pages[static_cast<size_t>(major)];
             ++subpage)
          CheckPage(root, variant, major, subpage, nodes);
        Require(nodes[static_cast<size_t>(major)] ==
                    hotspots[static_cast<size_t>(major)],
                "Route Map hotspot count differs from retail inventory");
      }
    }
    std::cout << "School Days Route Map: 16 pages, 161 selectors, four "
                 "CMAP variants, lock/unlock, navigation and both Route Map "
                 "button hover states PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysRouteMapMatrixTest: " << error.what() << '\n';
    return 1;
  }
}
