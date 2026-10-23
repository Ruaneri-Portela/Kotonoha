#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>

namespace Kotonoha {

const char* SchoolDaysSaveLoadModel::CmapRelativePath(Variant variant) {
    switch (variant) {
    case Variant::Standard:
        return "System/SaveLoad/SaveLoad.cmap";
    case Variant::Wide:
        return "System/SaveLoad/SaveLoad_Wide.cmap";
    case Variant::WideFull:
        return "System/SaveLoad/SaveLoad_Wide_Full.cmap";
    case Variant::WideNote:
        return "System/SaveLoad/SaveLoad_Wide_Note.cmap";
    }
    return "System/SaveLoad/SaveLoad.cmap";
}

bool SchoolDaysSaveLoadModel::Open(const std::string& cmapPath, Variant variant,
                                   Mode mode, bool textInputEnabled,
                                   std::string* error) {
    // MENU::SaveLoad is a singleton: construction seeds page 0 once, while
    // later open calls refresh the page already stored at +0x1F0.
    cmap_.Clear();
    selection_ = -1;
    pendingRow_ = -1;
    commentRow_ = -1;
    pendingComment_.clear();
    mode_ = mode;
    // Native host +0xD8 gates the third/comment metadata object independently
    // of SAVE/LOAD mode. SAVE-only editing is enforced in Activate/SubmitComment.
    textInputEnabled_ = textInputEnabled;
    rowOccupied_.fill(false);
    occupancyReady_ = false;
    open_ = false;
    if (!cmap_.LoadFile(cmapPath, error)) return false;
    variant_ = variant;
    selection_ = -1; // Native setup seeds from current pointer; presenter does that.
    open_ = true;
    return true;
}

void SchoolDaysSaveLoadModel::Close() {
    cmap_.Clear();
    selection_ = -1;
    pendingRow_ = -1;
    commentRow_ = -1;
    pendingComment_.clear();
    open_ = false;
}

int SchoolDaysSaveLoadModel::HitControl(int x, int y) const {
    if (!open_) return -1;
    const int region = cmap_.Get(x, y);
    if (region <= 0 || region > 32) return -1;
    return region - 1;
}

bool SchoolDaysSaveLoadModel::SetSelectionFromPointer(int x, int y) {
    if (pendingRow_ >= 0 || commentRow_ >= 0) return false;
    const int control = HitControl(x, y);
    if (control == selection_) return false;
    selection_ = control;
    return true;
}

bool SchoolDaysSaveLoadModel::Activate(Output* output) {
    if (output == nullptr) return false;
    *output = {};
    if (!open_ || selection_ < 0 || pendingRow_ >= 0 || commentRow_ >= 0) return false;

    // 10..19 are page tabs.  Activation changes page and refreshes the ten
    // visible slot rows; it does not emit a SysMenu result.
    if (selection_ >= 10 && selection_ <= 19) {
        page_ = selection_ - 10;
        rowOccupied_.fill(false);
        occupancyReady_ = false;
        return true;
    }

    // Control 20 is the explicit close button -> SaveLoad result 0.
    if (selection_ == 20) {
        output->action = Action::Close;
        return true;
    }

    // Control 21 dispatches RouteMap only from LOAD mode.
    if (selection_ == 21) {
        if (mode_ != Mode::Load) return false;
        output->action = Action::RouteMap;
        return true;
    }

    int row = -1;
    if (selection_ >= 0 && selection_ <= 9) row = selection_;
    if (selection_ >= 22 && selection_ <= 31) row = selection_ - 22;
    if (row < 0) return false;

    if (mode_ == Mode::Load) {
        if (!occupancyReady_ || !rowOccupied_[static_cast<size_t>(row)]) {
            return false;
        }
        output->action = Action::LoadSlotRequested;
        output->slot = page_ * 10 + row;
        return true;
    }

    // With TextInput enabled, native 0x10014990 records +0x1F4 and invokes host
    // +0xDC synchronously.  Only the dialog OK path reaches _CommentSet@4 and
    // sets +0x98. Cancel never arms the deferred writer.
    if (CanEditComment()) {
        commentRow_ = row;
        output->action = Action::CommentInputRequested;
        output->slot = page_ * 10 + row;
        return true;
    }

    pendingComment_.clear();
    pendingRow_ = row;
    return true;
}

bool SchoolDaysSaveLoadModel::SubmitComment(const std::u16string& comment) {
    if (!open_ || !CanEditComment() || commentRow_ < 0 || pendingRow_ >= 0) {
        return false;
    }
    pendingComment_ = comment;
    pendingRow_ = commentRow_;
    commentRow_ = -1;
    return true;
}

bool SchoolDaysSaveLoadModel::CancelComment() {
    if (!open_ || commentRow_ < 0 || pendingRow_ >= 0) return false;
    commentRow_ = -1;
    pendingComment_.clear();
    return true;
}

bool SchoolDaysSaveLoadModel::Tick(Output* output) {
    if (output == nullptr) return false;
    *output = {};
    if (!open_ || pendingRow_ < 0) return false;

    output->action = Action::SaveSlotRequested;
    output->slot = page_ * 10 + pendingRow_;
    output->comment = pendingComment_;
    pendingRow_ = -1;
    pendingComment_.clear();
    return true;
}

} // namespace Kotonoha
