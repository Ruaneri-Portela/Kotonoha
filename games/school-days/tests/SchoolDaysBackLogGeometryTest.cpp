#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
  // One authenticated close-control pixel permits an isolated hover test
  // without embedding or copying any retail CMAP resource into the repo.
  const auto &close = layout.controls[4].bounds;
  bytes[8u + static_cast<size_t>(close.y) * layout.logicalWidth +
        static_cast<size_t>(close.x)] = 5u;
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
  oversizedStore.AppendLogMessage(0, u"", std::u16string(600u, u'W'));
  Model oversizedModel;
  Check(oversizedModel.Open(root.string(), oversizedStore,
                            Model::Orientation::Horizon, variant, &error),
        "Failed to open oversized synthetic BackLog model");
  const auto oversized = Plan::BuildRetailHorizonEnglish(oversizedModel);
  CheckGlyphPlan(oversized);
  Check(oversized.blocks.size() == 1u &&
            oversized.blocks[0].extent > 8 * 57,
        "Translated messages over eight lines must retain their full extent");
  Check(oversizedModel.CurrentRawIndex() == 0 &&
            oversizedModel.MaxCurrentLineOffset() > 0 &&
            oversizedModel.CurrentLineOffset() ==
                oversizedModel.MaxCurrentLineOffset(),
        "Oversized BackLog must start at the final lines of the newest entry");
  std::vector<int> rows;
  for (const auto &glyph : oversized.blocks[0].glyphs)
    rows.push_back(glyph.y);
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  Check(rows.size() >= 9u,
        "Translated messages over eight lines must not be silently truncated");
  int backSteps = 0;
  while (oversizedModel.StepCursor(-1)) {
    ++backSteps;
    Check(oversizedModel.CurrentRawIndex() == 0,
          "Intra-entry scrolling unexpectedly changed history record");
    CheckGlyphPlan(Plan::BuildRetailHorizonEnglish(oversizedModel));
    Check(backSteps <= oversizedModel.MaxCurrentLineOffset(),
          "BackLog vertical scrolling failed to terminate");
  }
  Check(backSteps == oversizedModel.MaxCurrentLineOffset() &&
            oversizedModel.CurrentLineOffset() == 0,
        "Up navigation cannot reveal the first lines of oversized text");
  int forwardSteps = 0;
  while (oversizedModel.StepCursor(1)) {
    ++forwardSteps;
    Check(oversizedModel.CurrentRawIndex() == 0,
          "Down navigation must scroll long text before changing record");
    CheckGlyphPlan(Plan::BuildRetailHorizonEnglish(oversizedModel));
    Check(forwardSteps <= oversizedModel.MaxCurrentLineOffset(),
          "BackLog forward scrolling failed to terminate");
  }
  Check(forwardSteps == backSteps &&
            oversizedModel.CurrentLineOffset() ==
                oversizedModel.MaxCurrentLineOffset(),
        "Down navigation cannot reveal the last lines of oversized text");

  Kotonoha::SchoolDaysBackLogStore chineseStore;
  chineseStore.AppendLogMessage(
      0, u"Kotonoha", u"\u53D1\u4EF6\u4EBA\uFF1A\u6842\u8A00\u53F6\uFF5C\u6807\u9898\uFF1A\u300A\u5199\u771F\u300B");
  Model chineseModel;
  Check(chineseModel.Open(root.string(), chineseStore,
                          Model::Orientation::Horizon, variant, &error),
        "Could not open synthetic translated Chinese BackLog");
  const auto chinesePlan = Plan::BuildRetailHorizonEnglish(chineseModel);
  Check(chinesePlan.valid && !chinesePlan.unsupportedMarkup,
        "Fullwidth Chinese punctuation unexpectedly disabled the BackLog");
  bool hasVerticalBar = false, hasBookBracket = false;
  for (const auto &block : chinesePlan.blocks)
    for (const auto &glyph : block.glyphs) {
      hasVerticalBar |= glyph.codepoint == 0xFF5Cu;
      hasBookBracket |= glyph.codepoint == 0x300Au;
    }
  Check(hasVerticalBar && hasBookBracket,
        "Localized Chinese glyphs were silently dropped from the BackLog");

  // Short, untranslated-width Portuguese and Russian rows retain their
  // source glyph identities. Neither clipping nor the fitted wrap policy may
  // throw away ordinary accented/Cyrillic text from the history projection.
  Kotonoha::SchoolDaysBackLogStore multilingualStore;
  multilingualStore.AppendLogMessage(0, u"Kotonoha",
                                   u"N\u00E3o, hoje volto de trem.");
  multilingualStore.AppendLogMessage(
      1, u"Sekai", u"\u041F\u0440\u0438\u0432\u0435\u0442, \u041C\u0430\u043A\u043E\u0442\u043E.");
  Model multilingualModel;
  Check(multilingualModel.Open(root.string(), multilingualStore,
                               Model::Orientation::Horizon, variant, &error),
        "Could not open Portuguese/Russian BackLog synthetic model");
  auto hasGlyph = [](const Plan::NativeTextPlan &plan, uint16_t codepoint) {
    for (const auto &block : plan.blocks)
      for (const auto &glyph : block.glyphs)
        if (glyph.codepoint == codepoint)
          return true;
    return false;
  };
  auto multilingualPlan = Plan::BuildRetailHorizonEnglish(multilingualModel);
  Check(multilingualPlan.valid && hasGlyph(multilingualPlan, 0x0440u),
        "Russian Cyrillic text was lost in BackLog projection");
  Check(multilingualModel.StepCursor(-1),
        "Cannot inspect previous localized BackLog message");
  multilingualPlan = Plan::BuildRetailHorizonEnglish(multilingualModel);
  Check(multilingualPlan.valid && hasGlyph(multilingualPlan, 0x00E3u),
        "Portuguese accented text was lost in BackLog projection");

  // Leaving a window must clear a highlighted navigation/Close CHIP. The
  // runtime forwards SDL window-leave/focus-lost to this same model state.
  const auto &close = Model::LayoutFor(Model::Orientation::Horizon,
                                      variant).controls[4].bounds;
  Check(multilingualModel.SetSelectionFromPointer(close.x, close.y) &&
            multilingualModel.Selection() == 4,
        "BackLog close hover did not select the synthetic CMAP control");
  const auto hovered = Plan::BuildSkin(multilingualModel);
  Check(std::any_of(hovered.begin(), hovered.end(), [](const auto &sprite) {
          return sprite.role == Plan::Role::SelectedControl &&
                 sprite.control == 4;
        }), "BackLog close did not draw the hover CHIP");
  Check(multilingualModel.SetSelectionFromPointer(-1, -1) &&
            multilingualModel.Selection() == -1,
        "BackLog failed to clear hover when pointer left its window");
  const auto cleared = Plan::BuildSkin(multilingualModel);
  Check(std::none_of(cleared.begin(), cleared.end(), [](const auto &sprite) {
          return sprite.role == Plan::Role::SelectedControl;
        }), "BackLog blue hover remained after window leave");
}

void CheckSupplementaryUnicodeAndLongSpeaker(const fs::path &root,
                                             Model::Variant variant) {
  // Both speaker and line contain a real supplementary Unicode symbol. The
  // old wrap split its UTF-16 surrogate pair at the right edge; the old
  // speaker plan discarded the end of a 44+ character speaker entirely.
  std::u16string speaker(52u, u'W');
  speaker += u"\U0001F642";
  std::u16string message(45u, u'a');
  message += u"\U0001F642";
  message += u"z";
  Kotonoha::SchoolDaysBackLogStore store;
  store.AppendLogMessage(0, speaker, message);
  Model model;
  std::string error;
  Check(model.Open(root.string(), store, Model::Orientation::Horizon, variant,
                   &error), "Failed to open supplementary Unicode BackLog");
  const auto plan = Plan::BuildRetailHorizonEnglish(model);
  CheckGlyphPlan(plan);
  Check(plan.blocks.size() == 1u,
        "Supplementary Unicode BackLog lost the current history entry");

  std::map<int, std::u16string> rows;
  for (const auto &glyph : plan.blocks.front().glyphs)
    rows[glyph.y].push_back(static_cast<char16_t>(glyph.codepoint));
  Check(rows.size() >= 3u,
        "The long line did not wrap while keeping a separate speaker row");
  Check(rows.begin()->second == speaker,
        "Long speaker was truncated instead of proportionally fitted");
  std::u16string reconstructed;
  for (auto it = std::next(rows.begin()); it != rows.end(); ++it) {
    const auto &line = it->second;
    for (size_t i = 0u; i < line.size(); ++i) {
      const bool high = line[i] >= 0xd800u && line[i] <= 0xdbffu;
      const bool low = line[i] >= 0xdc00u && line[i] <= 0xdfffu;
      if (high)
        Check(i + 1u < line.size() && line[i + 1u] >= 0xdc00u &&
                  line[i + 1u] <= 0xdfffu,
              "BackLog wrap split high surrogate from low surrogate");
      if (low)
        Check(i > 0u && line[i - 1u] >= 0xd800u &&
                  line[i - 1u] <= 0xdbffu,
              "BackLog wrap left orphaned low surrogate");
    }
    reconstructed += line;
  }
  Check(reconstructed == message,
        "BackLog dropped or reordered translated supplementary characters");

  int utf8SmileRows = 0;
  for (const auto &[y, line] : rows) {
    (void)y;
    const std::string utf8 = Kotonoha::Utf16ToUtf8(line);
    Check(!utf8.empty(),
          "BackLog generated a row that cannot convert to UTF-8");
    if (utf8.find("\xF0\x9F\x99\x82") != std::string::npos)
      ++utf8SmileRows;
  }
  Check(utf8SmileRows == 2,
        "Speaker or translated text lost the UTF-8 smiley after wrapping");
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
      CheckSupplementaryUnicodeAndLongSpeaker(root, variant);
    }
    std::cout << "School Days BackLog: aspect, safe regions, translated "
                 "wrapping and row spacing PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysBackLogGeometryTest: " << error.what() << '\n';
    return 1;
  }
}
