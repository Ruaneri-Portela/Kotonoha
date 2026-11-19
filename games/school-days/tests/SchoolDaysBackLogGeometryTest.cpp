#include <Kotonoha/routing/SchoolDaysBackLog.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Model = Kotonoha::SchoolDaysBackLogModel;
using Plan = Kotonoha::SchoolDaysBackLogRenderPlan;

namespace {

void Check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void WriteSyntheticCmap(const fs::path &root, Model::Variant variant) {
  const auto layout = Model::LayoutFor(Model::Orientation::Horizon, variant);
  const auto file = root / Model::CmapRelativePath(Model::Orientation::Horizon,
                                                   variant);
  fs::create_directories(file.parent_path());
  std::vector<uint8_t> bytes(
      8u + static_cast<size_t>(layout.logicalWidth) * layout.logicalHeight, 0);
  for (int i = 0; i < 4; ++i) {
    bytes[static_cast<size_t>(i)] =
        static_cast<uint8_t>(layout.logicalWidth >> (8 * i));
    bytes[static_cast<size_t>(i + 4)] =
        static_cast<uint8_t>(layout.logicalHeight >> (8 * i));
  }
  std::ofstream output(file, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Check(bool(output), "Cannot create a synthetic BackLog CMAP");
}

void CheckAspectFit() {
  const auto widthBound = Plan::FitTextRaster(470, 1000, 66);
  Check(widthBound.w == 470 && widthBound.h == 31,
        "Raster fitting must preserve glyph aspect ratio at the width limit");
  const auto heightBound = Plan::FitTextRaster(1100, 1000, 66);
  Check(heightBound.w == 848 && heightBound.h == 56,
        "Raster fitting must preserve glyph aspect ratio at the line limit");
  const auto empty = Plan::FitTextRaster(0, 1000, 66);
  Check(empty.w == 0 && empty.h == 0,
        "Invalid dimensions must not produce a raster rectangle");
  const auto natural = Plan::FitTextRaster(100, 50, 30);
  Check(natural.w == 50 && natural.h == 30,
        "BackLog must not enlarge an already-fitting raster row");
}

void CheckProjectedClip(Model::Variant variant) {
  const auto clip = Plan::TextSurfaceClip();
  const auto source = Plan::TextSurfaceSource();
  const auto destination = Plan::TextSurfaceDestination(variant);
  const auto native = Model::LayoutFor(Model::Orientation::Horizon, variant);
  const float scaleX = destination.w / source.w;
  const float scaleY = destination.h / source.h;
  const float right = destination.x + (clip.x + clip.w) * scaleX;
  const float bottom = destination.y + (clip.y + clip.h) * scaleY;
  const auto &navigation = native.controls[0].bounds;
  Check(right + 10.0f * Plan::LayoutFor(variant).scale < navigation.x,
        "Text intersects the right-side BackLog navigation controls");
  // The lower blue skin band begins near y=409 in 800x450 authored space.
  Check(bottom < (409.0f + Plan::LayoutFor(variant).yOffset) *
                     Plan::LayoutFor(variant).scale,
        "Text intersects the BackLog bottom skin band");
}

void CheckGlyphPlan(const Plan::NativeTextPlan &plan) {
  Check(plan.valid, "Synthetic BackLog native plan is invalid");
  const auto clip = Plan::TextSurfaceClip();
  std::vector<std::pair<int, int>> visibleRows;
  for (const auto &block : plan.blocks) {
    for (const auto &glyph : block.glyphs) {
      const auto advance = Plan::RetailEnglishGlyphAdvance(glyph.codepoint);
      Check(glyph.x >= 0 && glyph.x + advance <= clip.x + clip.w,
            "BackLog native glyph reaches the navigation buttons");
    }
    // Rows are shared within a block. Their raster maximum must not overlap
    // rows belonging to different BackLog records.
    for (size_t i = 0; i < block.glyphs.size(); ++i) {
      const int y = block.glyphs[i].y;
      if (i != 0 && block.glyphs[i - 1].y == y)
        continue;
      if (y + 56 <= clip.y || y >= clip.y + clip.h)
        continue;
      visibleRows.emplace_back(y, y + 56);
    }
  }
  // Different rows, including current, previous and following records, may
  // touch one another but cannot occupy the same source pixels.
  std::sort(visibleRows.begin(), visibleRows.end());
  for (size_t i = 1; i < visibleRows.size(); ++i)
    Check(visibleRows[i - 1].second <= visibleRows[i].first,
          "BackLog rows overlap due to underestimated entry extent");
}

void CheckLongText(const fs::path &root, Model::Variant variant) {
  WriteSyntheticCmap(root, variant);
  Kotonoha::SchoolDaysBackLogStore store;
  store.AppendLogMessage(0, u"Sekai",
                         u"A resposta em português às vezes tem muito mais "
                         u"palavras do que a fala inglesa. Cada linha precisa "
                         u"terminar antes das setas de navegação e antes da "
                         u"faixa azul inferior, mesmo com tradução longa.");
  store.AppendLogMessage(1, u"Makoto", std::u16string(70u, u'W'));
  store.AppendLogMessage(2, std::u16string(44u, u'W'),
                         u"A última fala mantém a ordem das outras entradas.");
  Model model;
  std::string error;
  Check(model.Open(root.string(), store, Model::Orientation::Horizon, variant,
                   &error),
        "Failed to open a synthetic BackLog model");
  for (int i = 0; i < 3; ++i) {
    const auto plan = Plan::BuildRetailHorizonEnglish(model);
    CheckGlyphPlan(plan);
    if (i == 0) {
      const auto found = std::find_if(
          plan.blocks.begin(), plan.blocks.end(), [](const auto &block) {
            return block.rawIndex == 1u;
          });
      Check(found != plan.blocks.end(), "Long unbroken word is missing");
      std::vector<int> ys;
      for (const auto &glyph : found->glyphs)
        ys.push_back(glyph.y);
      std::sort(ys.begin(), ys.end());
      ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
      Check(ys.size() >= 2u,
            "A long unbroken word must wrap within the text column");
    }
    if (i != 2)
      Check(model.StepCursor(-1), "BackLog cursor failed to step backwards");
  }

  Kotonoha::SchoolDaysBackLogStore oversizedStore;
  oversizedStore.AppendLogMessage(0, u"", std::u16string(279u, u'W'));
  Model oversizedModel;
  Check(oversizedModel.Open(root.string(), oversizedStore,
                            Model::Orientation::Horizon, variant, &error),
        "Failed to open oversized synthetic BackLog model");
  const auto oversized = Plan::BuildRetailHorizonEnglish(oversizedModel);
  CheckGlyphPlan(oversized);
  Check(oversized.blocks.size() == 1u &&
            oversized.blocks[0].extent > 8 * 57,
        "Translated messages over eight lines must retain their full extent");
  std::vector<int> rows;
  for (const auto &glyph : oversized.blocks[0].glyphs)
    rows.push_back(glyph.y);
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  Check(rows.size() >= 9u,
        "Translated messages over eight lines must not be silently truncated");
}

} // namespace

int main(int argc, char **argv) {
  try {
    Check(argc == 2, "Expected an isolated synthetic fixture directory");
    const fs::path root(argv[1]);
    CheckAspectFit();
    for (const auto variant : {Model::Variant::Standard, Model::Variant::Wide,
                               Model::Variant::WideFull,
                               Model::Variant::WideNote}) {
      CheckProjectedClip(variant);
      CheckLongText(root, variant);
    }
    std::cout << "School Days BackLog: aspect, safe regions, translated "
                 "wrapping and row spacing PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysBackLogGeometryTest: " << error.what() << '\n';
    return 1;
  }
}
