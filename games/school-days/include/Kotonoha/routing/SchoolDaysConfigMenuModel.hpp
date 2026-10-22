#pragma once

#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>

namespace Kotonoha {

// Pointer-only semantic model for MENU::ConfigMenu.  The model deliberately
// stops at typed requests: applying display/audio policy and executing legacy
// SomCon transactions belong to the host/state-machine integration boundary.
class SchoolDaysConfigMenuModel {
public:
    enum class Variant {
        Standard,
        Wide,
        WideFull,
        WideNote,
    };

    enum class Page : int {
        Def = 0,
        Sound = 1,
        SomCon = 2,
    };

    enum class ActionKind {
        None,
        SelectPage,
        Close,
        RequestWide,
        RequestFull,
        SetSkip,
        SetSuperSkip,
        SetDrawMessage,
        StepSoundBank,
        SetMenVoice,
        SetMute,
        SetSoundLevel,
        SomConAutoScan,
        SomConDisable,
        SomConSelectDevice,
        SomConStartSecondary,
        SomConStopSecondary,
    };

    struct Action {
        ActionKind kind = ActionKind::None;
        Page page = Page::Def;

        // Boolean, direct-level and SomCon-device actions use value. ConfigMenu's visible
        // bank order is authenticated as BGM/SE/Voice = 0/1/2; the native host
        // bank ABI is Voice/SE/BGM = 0/1/2 and is translated by the retail
        // config policy at the integration boundary.
        int value = 0;
        int soundBank = -1;

        // Native Sound ids 4..9 are three '-'/ '+' pairs. choice is translated
        // to SysMenu 0x10007140's direction ABI: 1 decrements, 0 increments.
        int choice = -1;
    };

    static Variant SelectVariant(int effectiveDisplayType, int windowMode,
                                 int typeMiniNote);
    static const char* PageToken(Page page);
    static std::string CmapRelativePath(Page page, Variant variant);
    static std::string BaseRelativePath(Page page);
    static std::string ChipRelativePath(Page page);
    static const char* SomConSetRelativePath();

    bool Open(const std::string& assetsRoot, Variant variant, Page page,
              std::string* error = nullptr);
    void Close();

    // MENU's shared CMAP helper returns raw region id - 1.  Background and
    // out-of-bounds coordinates therefore map to -1.
    int HitControl(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);
    bool Activate(Action* action) const;

    bool IsOpen() const { return open_; }
    int Selection() const { return selection_; }
    Variant CurrentVariant() const { return variant_; }
    Page CurrentPage() const { return page_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }

private:
    static bool DecodeAction(Page page, int control, Action* action);

    SchoolDaysCmap cmap_;
    Variant variant_ = Variant::Standard;
    Page page_ = Page::Def;
    int selection_ = -1;
    bool open_ = false;
};

} // namespace Kotonoha
