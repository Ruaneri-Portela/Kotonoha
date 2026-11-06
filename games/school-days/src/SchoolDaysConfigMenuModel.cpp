#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>

#include <algorithm>

namespace Kotonoha {
namespace {

const char *VariantSuffix(SchoolDaysConfigMenuModel::Variant variant) {
  using Variant = SchoolDaysConfigMenuModel::Variant;
  switch (variant) {
  case Variant::Standard:
    return "";
  case Variant::Wide:
    return "_Wide";
  case Variant::WideFull:
    return "_Wide_Full";
  case Variant::WideNote:
    return "_Wide_Note";
  }
  return "";
}

} // namespace

SchoolDaysConfigMenuModel::Variant
SchoolDaysConfigMenuModel::SelectVariant(int effectiveDisplayType,
                                         int windowMode, int typeMiniNote) {
  // SysMenu's option/exit resource selectors use the same four-family gate:
  // ordinary display, Wide, Wide_Full, and Wide_Note.
  if (effectiveDisplayType != 1)
    return Variant::Standard;
  if (windowMode != 1)
    return Variant::Wide;
  return typeMiniNote == 0 ? Variant::WideFull : Variant::WideNote;
}

const char *SchoolDaysConfigMenuModel::PageToken(Page page) {
  switch (page) {
  case Page::Def:
    return "Def";
  case Page::Sound:
    return "Sound";
  case Page::Display:
    return "Display";
  }
  return "Def";
}

std::string SchoolDaysConfigMenuModel::CmapRelativePath(Page page,
                                                        Variant variant) {
  return std::string("System/Option/Option_") + PageToken(page) +
         VariantSuffix(variant) + ".cmap";
}

std::string SchoolDaysConfigMenuModel::BaseRelativePath(Page page) {
  return std::string("System/Option/Option_") + PageToken(page) + ".png";
}

std::string SchoolDaysConfigMenuModel::ChipRelativePath(Page page) {
  return std::string("System/Option/Option_") + PageToken(page) + "_Chip.png";
}

bool SchoolDaysConfigMenuModel::Open(const std::string &assetsRoot,
                                     Variant variant, Page page,
                                     std::string *error,
                                     bool retailDisplayArtworkFallback) {
  Close();
  const Page artworkPage =
      retailDisplayArtworkFallback && page == Page::Display ? Page::Def : page;
  std::string physical;
  if (!ResolveSchoolDaysRequiredAssetPath(
          assetsRoot.c_str(), CmapRelativePath(artworkPage, variant), &physical,
          error, "ConfigMenu CMAP")) {
    return false;
  }
  if (!cmap_.LoadFile(physical, error))
    return false;

  variant_ = variant;
  page_ = page;
  artworkPage_ = artworkPage;
  selection_ = -1;
  open_ = true;
  return true;
}

void SchoolDaysConfigMenuModel::Close() {
  cmap_.Clear();
  variant_ = Variant::Standard;
  page_ = Page::Def;
  artworkPage_ = Page::Def;
  selection_ = -1;
  open_ = false;
}

int SchoolDaysConfigMenuModel::HitControl(int x, int y) const {
  if (!open_)
    return -1;
  const int region = cmap_.RegionAt(x, y);
  const int control = region > 0 ? region - 1 : -1;
  // Def's configuration controls are not Display actions. Only its original
  // shared navigation and CLOSE hotspots apply under Android's native panel.
  return page_ == Page::Display && artworkPage_ != page_ && control > 3
             ? -1
             : control;
}

bool SchoolDaysConfigMenuModel::SetSelectionFromPointer(int x, int y) {
  const int control = HitControl(x, y);
  if (control == selection_)
    return false;
  selection_ = control;
  return true;
}

bool SchoolDaysConfigMenuModel::DecodeAction(Page page, int control,
                                             Action *action) {
  if (action == nullptr)
    return false;
  *action = {};

  // The shipped third-tab region used SOMCON. The remaster removes that
  // device UI and reuses the authenticated pointer region for Display.
  if (control >= 0 && control <= 2) {
    action->kind = ActionKind::SelectPage;
    action->page = static_cast<Page>(control);
    return true;
  }
  if (control == 3) {
    action->kind = ActionKind::Close;
    return true;
  }

  if (page == Page::Def) {
    switch (control) {
    case 4:
    case 5:
      action->kind = ActionKind::RequestWide;
      action->value = control == 4 ? 1 : 0;
      return true;
    case 6:
    case 7:
      action->kind = ActionKind::RequestFull;
      action->value = control == 6 ? 1 : 0;
      return true;
    case 8:
    case 9:
      action->kind = ActionKind::SetSkip;
      action->value = control == 8 ? 1 : 0;
      return true;
    case 10:
    case 11:
      action->kind = ActionKind::SetSuperSkip;
      action->value = control == 10 ? 1 : 0;
      return true;
    case 12:
    case 13:
      action->kind = ActionKind::SetDrawMessage;
      action->value = control == 12 ? 1 : 0;
      return true;
    default:
      return false;
    }
  }

  if (page == Page::Sound) {
    if (control >= 4 && control <= 9) {
      action->kind = ActionKind::StepSoundBank;
      action->soundBank = (control - 4) / 2;
      // Each bank pair is laid out as '-' on the left and '+' on the
      // right.  SysMenu 0x10007140 uses the opposite-shaped direction
      // ABI: nonzero decrements and zero increments.
      action->choice = ((control - 4) % 2) == 0 ? 1 : 0;
      return true;
    }
    if (control == 10 || control == 11) {
      action->kind = ActionKind::SetMenVoice;
      action->value = control == 10 ? 1 : 0;
      return true;
    }
    if (control == 12 || control == 13) {
      action->kind = ActionKind::SetMute;
      action->value = control == 12 ? 1 : 0;
      return true;
    }
    if (control >= 14 && control <= 43) {
      action->kind = ActionKind::SetSoundLevel;
      action->soundBank = (control - 14) / 10;
      action->value = ((control - 14) % 10) + 1;
      return true;
    }
    return false;
  }

  if (page == Page::Display) {
    // Corrected custom Display CMAP reserves regions 1..22 for control IDs
    // 0..21. Navigation/Close are handled above; the remaining controls are
    // forwarded without imposing any physical display policy in the model.
    if (control >= 4 && control <= 21) {
      action->kind = ActionKind::DisplayControl;
      action->value = control;
      return true;
    }
    return false;
  }

  return false;
}

bool SchoolDaysConfigMenuModel::Activate(Action *action) const {
  if (!open_ || selection_ < 0)
    return false;
  return DecodeAction(page_, selection_, action);
}

} // namespace Kotonoha
