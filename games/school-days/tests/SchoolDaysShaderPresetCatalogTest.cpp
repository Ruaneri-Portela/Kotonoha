#include <Kotonoha/routing/SchoolDaysShaderPresetCatalog.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>

using namespace Kotonoha;
namespace Catalog = SchoolDaysShaderPresetCatalog;

static void Check(bool ok, const char *message) {
  if (!ok) {
    std::cerr << "SchoolDaysShaderPresetCatalogTest: " << message << '\n';
    std::exit(1);
  }
}

int main() {
  const std::array<bool, 3> closed{false, false, false};
  auto rows = Catalog::VisibleRows(closed);
  Check(rows.size() == 3, "collapsed catalog must show exactly three folders");
  for (std::size_t i = 0; i < rows.size(); ++i)
    Check(rows[i].heading && Catalog::Index(rows[i].tier) == i,
          "folder order must be Low, Medium, High");

  const std::array<bool, 3> open{true, true, true};
  rows = Catalog::VisibleRows(open);
  Check(rows.size() == UpscalePresets().size() + 3,
        "all fifteen legacy presets must remain selectable");
  std::set<int> unique;
  std::array<int, 3> counts{0, 0, 0};
  for (const auto &row : rows) {
    if (row.heading) continue;
    Check(unique.insert(static_cast<int>(row.preset)).second,
          "preset duplicated between folders");
    ++counts[Catalog::Index(row.tier)];
    Check(Catalog::Group(row.preset) == row.tier,
          "preset in wrong quality folder");
    const int index = Catalog::VisibleIndexOf(row.preset, open);
    Check(index >= 0 && !rows[static_cast<std::size_t>(index)].heading,
          "selected preset did not map to its visible leaf");
  }
  Check(counts == (std::array<int, 3>{3, 8, 4}),
        "unexpected Low/Medium/High preset distribution");
  Check(unique.size() == UpscalePresets().size(),
        "legacy preset missing from grouped catalog");
  std::array<bool, 3> highOnly{false, false, true};
  Check(Catalog::VisibleIndexOf(UpscalePreset::ArtCNNLarge, highOnly) >= 0,
        "selected high preset hidden despite expanded folder");
  Check(Catalog::VisibleIndexOf(UpscalePreset::ArtCNN, highOnly) == -1,
        "collapsed folder leaked a selectable preset");
  Check(!Catalog::SupportedShaderFile("FSRCNNX") &&
            !Catalog::SupportedShaderFile("assets/FSRCNNX/") &&
            Catalog::SupportedShaderFile(
                "assets/FSRCNNX/FSRCNNX_x2_16-0-4-1.glsl") &&
            Catalog::SupportedShaderFile("C:\\tmp\\filter.HOOK"),
        "Custom shader validation must reject folders and accept files");
  std::cout << "SchoolDaysShaderPresetCatalogTest PASS 3+8+4 presets\n";
}
