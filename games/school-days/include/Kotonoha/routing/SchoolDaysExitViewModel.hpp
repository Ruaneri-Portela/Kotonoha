#pragma once

#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>

namespace Kotonoha {

// Pointer-only semantic model for MENU::ExitView.  Host shutdown/title routing
// is expressed as a typed outcome so the presenter never treats native code 9
// as a SysMenu mode number.
class SchoolDaysExitViewModel {
public:
    enum class Variant {
        Standard,
        Wide,
        WideFull,
        WideNote,
    };

    enum class BaseArtwork {
        Exit,
        Title,
    };

    enum class Outcome {
        ConfirmExit,
        ReturnToTitle,
    };

    struct Activation {
        Outcome outcome = Outcome::ReturnToTitle;
        int resFlag = 0;
    };

    // Authenticated outer-host constants. Code 9 is a host action used only by
    // ConfirmExit; parent mode 2 is MENU::TitleView for the cancel path.
    static constexpr int ConfirmExitHostCode = 9;
    static constexpr int ReturnToTitleParentMode = 2;

    static Variant SelectVariant(int effectiveDisplayType, int windowMode,
                                 int typeMiniNote);
    static const char* CmapRelativePath(Variant variant);
    static const char* BaseRelativePath(BaseArtwork artwork);
    static const char* ChipRelativePath();

    bool Open(const std::string& assetsRoot, Variant variant,
              std::string* error = nullptr);
    void Close();

    int HitChoice(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);
    bool Activate(Activation* activation) const;

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
