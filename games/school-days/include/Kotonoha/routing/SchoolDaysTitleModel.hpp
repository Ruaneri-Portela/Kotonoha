#pragma once

#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// Pure MENU::TitleView presentation/input model. Result codes and the optional
// start selector are authenticated from SysMenu/EXE; ownership of the target
// outer-flow screens remains outside this presenter.
class SchoolDaysTitleModel {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    enum class Family { Title, TitleClear, TitleAc };
    enum class Result : int {
        Exit = -1,
        Start = 1,
        Load = 3,
        Config = 4,
        Replay = 5,
    };

    struct Activation {
        Result result = Result::Start;
        int startSelector = -1;
    };

    struct HostState {
        bool ac = false;              // authenticated host +0xE8 gate
        bool alternate = false;       // authenticated host +0x34 gate
        bool endClearVisual = false;  // host +0xEC(0)
        bool endClearReplay = false;  // host +0xEC(1)
    };

    static Family SelectFamily(const HostState& state);
    static bool ItemAvailable(int item, const HostState& state);
    static const char* ResourceStem(Family family);
    static const char* ChipStem(Family family);
    static std::string CmapRelativePath(Family family, Variant variant);

    bool Open(const std::string& assetsRoot, Variant variant,
              const HostState& state, std::string* error = nullptr);
    void Close();

    int HitItem(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);
    bool ActivateSelection(Activation* out) const;

    bool IsOpen() const { return open_; }
    int Selection() const { return selection_; }
    bool SelectionAvailable() const {
        return selection_ >= 0 && ItemAvailable(selection_, host_);
    }
    Family CurrentFamily() const { return family_; }
    Variant CurrentVariant() const { return variant_; }
    const HostState& CurrentHostState() const { return host_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }

private:
    SchoolDaysCmap cmap_;
    HostState host_{};
    Family family_ = Family::Title;
    Variant variant_ = Variant::Standard;
    int selection_ = -1;
    bool open_ = false;
};

} // namespace Kotonoha
