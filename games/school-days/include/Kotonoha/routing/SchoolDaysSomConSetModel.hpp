#pragma once

#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>

namespace Kotonoha {

// Independent MENU::SomconSet popup used by ConfigMenu outer mode7.  The
// authenticated screen has one control; activating it asks the host for
// transition 2, which returns to Config outer mode4.
class SchoolDaysSomConSetModel {
public:
    using Variant = SchoolDaysConfigMenuModel::Variant;
    static constexpr int kReturnToConfigHostTransition = 2;

    static std::string CmapRelativePath(Variant variant);
    static const char* BaseRelativePath() { return "System/Option/Pop_Som.png"; }
    static const char* ChipRelativePath() {
        return "System/Option/Pop_Som_Chip.png";
    }

    bool Open(const std::string& assetsRoot, Variant variant,
              std::string* error = nullptr);
    void Close();

    int HitControl(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);
    bool Activate(int* hostTransition) const;

    bool IsOpen() const { return open_; }
    int Selection() const { return selection_; }
    Variant CurrentVariant() const { return variant_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }

private:
    SchoolDaysCmap cmap_;
    Variant variant_ = Variant::Standard;
    int selection_ = -1;
    bool open_ = false;
};

} // namespace Kotonoha
