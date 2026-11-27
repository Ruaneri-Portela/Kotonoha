#pragma once

#include <cstdint>
#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>

namespace Kotonoha {

// Pure semantic/input model for MENU::SaveFeelView. Visual rendering remains a
// separate SchoolDaysSystemUi concern, but the native 30-second timer is part
// of this model because it controls when the view closes.
class SchoolDaysSaveFeelModel {
public:
    enum class Variant {
        Standard,
        Wide,
        WideFull,
        WideNote,
    };

    enum class Result : int {
        Skip = 0,
        OpenSave = 4,
    };

    static Variant SelectVariant(int effectiveDisplayType, int windowMode,
                                 int typeMiniNote);
    static const char* CmapRelativePath(Variant variant);

    bool Open(const std::string& assetsRoot, Variant variant,
              std::string* error = nullptr);
    void Close();

    // Native CMAP ids are 1-based controls; SysMenu helper 0x1000BA00 returns
    // id-1. A zero/background or out-of-map hit returns -1.
    int HitChoice(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);
    void Previous();
    void Next();
    bool Activate(Result* result) const;
    void SetFeelings(int64_t feeling001, int64_t feeling002);

    // Full native SaveFeel tick order: the 1500 ms bar-entry state machine
    // runs first (FUN_10010C60), then the independent 30-second countdown
    // (FUN_10010F80).  Keeping the order in one pure-model call prevents the
    // presenter from accidentally enabling input before +D0 clears.
    void Tick(uint32_t nowMs);

    // SysMenuSDHQ.dll:0x10010F80. The first update samples timeGetTime() and
    // arms state 1. State 1 displays 30-floor(elapsed/1000) while elapsed is
    // below 30000 ms. At/after 30000 it displays 00 and advances to state 2;
    // the following update closes the view. uint32 subtraction deliberately
    // preserves native timeGetTime() wrap-around behavior.
    void TickCountdown(uint32_t nowMs);

    bool IsOpen() const { return open_; }
    int Selection() const { return selection_; }
    Variant CurrentVariant() const { return variant_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }
    bool CountdownEnabled() const { return countdownEnabled_; }
    int CountdownTens() const { return countdownTens_; }
    int CountdownOnes() const { return countdownOnes_; }
    bool CountdownCloseRequested() const { return countdownCloseRequested_; }
    bool CountdownResult(Result* result) const;
    bool Interactive() const { return open_ && !entryTransitionActive_; }
    float Feeling001Visible() const { return feeling001Visible_; }
    float Feeling002Visible() const { return feeling002Visible_; }

private:
    SchoolDaysCmap cmap_;
    Variant variant_ = Variant::Standard;
    int selection_ = 0;
    bool open_ = false;
    uint32_t countdownStartMs_ = 0;
    int countdownState_ = 0;
    int countdownTens_ = 0;
    int countdownOnes_ = 0;
    bool countdownEnabled_ = false;
    bool countdownCloseRequested_ = false;
    int64_t feeling001Raw_ = 0;
    int64_t feeling002Raw_ = 0;
    float feeling001Visible_ = 0.0f;
    float feeling002Visible_ = 0.0f;
    uint32_t entryStartMs_ = 0;
    int entryState_ = 0;
    bool entryTransitionActive_ = false;

    static float ClampFeelingFraction(float value);
    void TickEntryTransition(uint32_t nowMs);
};

} // namespace Kotonoha
