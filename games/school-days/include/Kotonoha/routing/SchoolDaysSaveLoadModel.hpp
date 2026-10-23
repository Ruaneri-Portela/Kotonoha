#pragma once

#include <array>
#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// Pure MENU::SaveLoad control/page/slot model. SAVE and LOAD share the native
// singleton/page state but have different row activation semantics.
class SchoolDaysSaveLoadModel {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    enum class Mode {
        Load,
        Save,
    };

    enum class Action {
        None,
        Close,
        RouteMap,
        LoadSlotRequested,
        CommentInputRequested,
        SaveSlotRequested,
    };

    struct Output {
        Action action = Action::None;
        int slot = -1;
        std::u16string comment;
    };

    bool Open(const std::string& cmapPath, Variant variant, Mode mode,
              bool textInputEnabled,
              std::string* error = nullptr);
    bool Open(const std::string& cmapPath, Variant variant,
              bool textInputEnabled, std::string* error = nullptr) {
        return Open(cmapPath, variant, Mode::Save, textInputEnabled, error);
    }
    void Close();

    bool IsOpen() const { return open_; }
    int Page() const { return page_; }
    int Selection() const { return selection_; }
    bool SavePending() const { return pendingRow_ >= 0; }
    bool CommentInputPending() const { return commentRow_ >= 0; }
    // Host +0xD8 (TextInput) is a capability shared by SAVE and LOAD display:
    // both modes show the optional third/comment metadata column when enabled.
    // Only SAVE may open/edit the comment dialog.
    bool TextInputEnabled() const { return textInputEnabled_; }
    bool CanEditComment() const {
        return mode_ == Mode::Save && textInputEnabled_;
    }
    Mode CurrentMode() const { return mode_; }
    Variant CurrentVariant() const { return variant_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }

    int HitControl(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);

    // Executes the already-selected control.  SAVE rows emit a logical slot
    // request; the caller must reproduce the high-level Save boundary before
    // reporting StayOpen back to the outer flow.
    bool Activate(Output* output);

    // _CommentSet@4 is invoked only by the native Win32 dialog's OK path.
    // Cancel closes the dialog without calling it and therefore aborts this
    // slot request.  A successful comment callback arms +0x98 for the next
    // SaveLoad update, exactly like the TextInput==0 direct path.
    bool SubmitComment(const std::u16string& comment);
    bool CancelComment();

    // Native SAVE dispatch is deferred by one view tick: row activation stores
    // +0x1F4 and sets +0x98; the next 0x10014C90 update invokes the high-level
    // writer and refreshes the view.  The logical request is surfaced here.
    bool Tick(Output* output);

    // SaveLoad obtains occupancy from the host's ten visible slot probes. LOAD
    // row activation is invalid until the current page has been published.
    void SetRowOccupancy(const std::array<bool, 10>& occupied) {
        rowOccupied_ = occupied;
        occupancyReady_ = true;
    }

    static const char* CmapRelativePath(Variant variant);

private:
    SchoolDaysCmap cmap_;
    Variant variant_ = Variant::Standard;
    Mode mode_ = Mode::Save;
    int page_ = 0;
    int selection_ = -1;
    int pendingRow_ = -1;
    int commentRow_ = -1;
    bool textInputEnabled_ = false;
    std::array<bool, 10> rowOccupied_{};
    bool occupancyReady_ = false;
    std::u16string pendingComment_;
    bool open_ = false;
};

} // namespace Kotonoha
