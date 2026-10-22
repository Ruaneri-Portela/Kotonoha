#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <algorithm>

namespace Kotonoha {
namespace {

constexpr const char* kHSceneUnlockKeys[] = {
    "REP02_2S_W03", "REP02_2K_J03", "REP02_2K_L01", "REP02_2K_O01",
    "REP02_2K_Q03", "REP03_SA_B07", "REP03_A0_A00", "REP03_A1_E08",
    "REP03_AK_E00", "REP03_SB_A00", "REP03_B2_C00", "REP03_KB_N00",
    "REP04_KA_D01", "REP04_KA_B03", "REP04_KA_L00", "REP04_KA_B04",
    "REP04_SB_G05", "REP04_KB_C03", "REP04_KB_E01", "REP04_KC_C03",
    "REP04_KC_E01", "REP04_SC_B00", "REP04_C1_A00", "REP04_SC_D02",
    "REP04_SC_F00", "REP04_C0_I00", "REP04_KE_C01", "REP04_KE_B07",
    "REP05_KA_A05", "REP05_5H_C03", "REP05_5H_D00", "REP05_SD_D05",
    "REP05_SD_A02", "REP05_SA_I00", "REP05_SB_I01", "REP05_KC_A00",
    "REP05_KC_D00", "REP05_KC_E00", "REP05_KC_A02", "REP05_5O_D10",
    "REP05_SE_N00",
};

static_assert(sizeof(kHSceneUnlockKeys) / sizeof(kHSceneUnlockKeys[0]) == 41,
              "authenticated HScene unlock table size drifted");

const char* VariantSuffix(SchoolDaysReplayModel::Variant variant) {
    using Variant = SchoolDaysReplayModel::Variant;
    switch (variant) {
    case Variant::Standard: return "";
    case Variant::Wide: return "_Wide";
    case Variant::WideFull: return "_Wide_Full";
    case Variant::WideNote: return "_Wide_Note";
    }
    return "";
}


} // namespace

const char* SchoolDaysReplayModel::HSceneUnlockKey(int index) {
    if (index < 0 || index >= HSceneUnlockCount()) return nullptr;
    return kHSceneUnlockKeys[index];
}

const char* SchoolDaysReplayModel::SceneCheckAvailabilityKey(int sceneCheckKey,
                                                             int choice) {
    static constexpr const char* kSelector0[] = {
        "REP03_KB_N00A", "REP03_KB_N00B", "REP03_KB_N00C", "REP03_KB_N00D"};
    static constexpr const char* kSelector2[] = {
        "REP04_C1_A00A", "REP04_C1_A00B"};
    static constexpr const char* kSelector3[] = {
        "REP05_5H_D00A", "REP05_5H_D00B"};
    if (choice < 0) return nullptr;
    if (sceneCheckKey == 0 && choice < 4) return kSelector0[choice];
    if (sceneCheckKey == 2 && choice < 2) return kSelector2[choice];
    if (sceneCheckKey == 3 && choice < 2) return kSelector3[choice];
    return nullptr;
}

std::string SchoolDaysReplayModel::CmapRelativePath(View view, Variant variant) {
    return std::string("System/Replay/Replay_") +
           (view == View::HScene ? "HScene" : "PlayData") +
           VariantSuffix(variant) + ".cmap";
}

std::string SchoolDaysReplayModel::SceneCheckCmapRelativePath(
    int choiceCount, Variant variant) {
    return std::string("System/Replay/Pop_Replay_") +
           (choiceCount == 4 ? "4" : "2") + VariantSuffix(variant) + ".cmap";
}

const char* SchoolDaysReplayModel::BaseRelativePath(View view) {
    return view == View::HScene ? "System/Replay/Replay_HScene.png"
                                : "System/Replay/Replay_PlayData.png";
}

const char* SchoolDaysReplayModel::ChipRelativePath(View view) {
    return view == View::HScene ? "System/Replay/Replay_HScene_Chip.png"
                                : "System/Replay/Replay_PlayData_Chip.png";
}

std::string SchoolDaysReplayModel::SceneCheckBaseRelativePath(int choiceCount) {
    return std::string("System/Replay/Pop_Replay_") +
           (choiceCount == 4 ? "4" : "2") + ".png";
}

std::string SchoolDaysReplayModel::SceneCheckChipRelativePath(int choiceCount) {
    return std::string("System/Replay/Pop_Replay_") +
           (choiceCount == 4 ? "4" : "2") + "_Chip.png";
}

bool SchoolDaysReplayModel::LoadViewCmap(const std::string& assetsRoot, View view,
                                         std::string* error) {
    std::string physical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(view, variant_), &physical, error,
            "Replay CMAP")) return false;
    SchoolDaysCmap next;
    if (!next.LoadFile(physical, error)) return false;
    cmap_ = std::move(next);
    return true;
}

bool SchoolDaysReplayModel::Open(const std::string& assetsRoot, Variant variant,
                                 View view, std::string* error) {
    Close();
    assetsRoot_ = assetsRoot;
    variant_ = variant;
    view_ = view;
    page_ = 0;
    if (!LoadViewCmap(assetsRoot_, view_, error)) {
        Close();
        return false;
    }
    open_ = true;
    return true;
}

void SchoolDaysReplayModel::Close() {
    cmap_.Clear();
    sceneCheckCmap_.Clear();
    assetsRoot_.clear();
    page_ = 0;
    selection_ = -1;
    sceneCheckSelection_ = -1;
    sceneCheckChoiceCount_ = 0;
    sceneCheckContextIndex_ = -1;
    sceneCheckKey_ = -1;
    sceneCheckOpen_ = false;
    open_ = false;
}

bool SchoolDaysReplayModel::SwitchView(const std::string& assetsRoot, View view,
                                       std::string* error) {
    if (!open_ || sceneCheckOpen_) return false;
    if (view == view_) return true;
    const View old = view_;
    view_ = view;
    if (!LoadViewCmap(assetsRoot, view_, error)) {
        view_ = old;
        return false;
    }
    assetsRoot_ = assetsRoot;
    page_ = 0;
    selection_ = -1;
    return true;
}

int SchoolDaysReplayModel::HitControl(int x, int y) const {
    if (!open_) return -1;
    const int raw = Cmap().Get(x, y);
    return raw > 0 ? raw - 1 : -1;
}

bool SchoolDaysReplayModel::SetSelectionFromPointer(int x, int y) {
    if (!open_) return false;
    const int hit = HitControl(x, y);
    int& selected = sceneCheckOpen_ ? sceneCheckSelection_ : selection_;
    const bool changed = selected != hit;
    selected = hit;
    return changed;
}

void SchoolDaysReplayModel::ClearAvailability() {
    hSceneAvailable_.fill(false);
    playDataAvailable_.fill(false);
    sceneCheckChoiceAvailable_.fill(false);
}

void SchoolDaysReplayModel::SetHSceneAvailable(int index, bool available) {
    if (index >= 0 && index < static_cast<int>(hSceneAvailable_.size()))
        hSceneAvailable_[static_cast<size_t>(index)] = available;
}

void SchoolDaysReplayModel::SetPlayDataAvailable(int index, bool available) {
    if (index >= 0 && index < static_cast<int>(playDataAvailable_.size()))
        playDataAvailable_[static_cast<size_t>(index)] = available;
}

void SchoolDaysReplayModel::SetSceneCheckChoiceAvailable(int choice, bool available) {
    if (choice >= 0 && choice < static_cast<int>(sceneCheckChoiceAvailable_.size()))
        sceneCheckChoiceAvailable_[static_cast<size_t>(choice)] = available;
}

bool SchoolDaysReplayModel::HSceneAvailable(int index) const {
    return index >= 0 && index < static_cast<int>(hSceneAvailable_.size()) &&
           hSceneAvailable_[static_cast<size_t>(index)];
}

bool SchoolDaysReplayModel::PlayDataAvailable(int index) const {
    return index >= 0 && index < static_cast<int>(playDataAvailable_.size()) &&
           playDataAvailable_[static_cast<size_t>(index)];
}

bool SchoolDaysReplayModel::OpenSceneCheck(const std::string& assetsRoot,
                                           int absoluteIndex, int choiceCount,
                                           int key, Action* out,
                                           std::string* error) {
    std::string physical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), SceneCheckCmapRelativePath(choiceCount, variant_),
            &physical, error, "Replay SceneCheck CMAP")) return false;
    SchoolDaysCmap next;
    if (!next.LoadFile(physical, error)) return false;
    sceneCheckCmap_ = std::move(next);
    sceneCheckOpen_ = true;
    sceneCheckChoiceCount_ = choiceCount;
    sceneCheckContextIndex_ = absoluteIndex;
    sceneCheckKey_ = key;
    sceneCheckSelection_ = -1;
    sceneCheckChoiceAvailable_.fill(false);
    if (out != nullptr) {
        out->kind = ActionKind::SceneCheckRequested;
        out->index = absoluteIndex;
        out->choiceCount = choiceCount;
        out->sceneCheckKey = key;
    }
    return true;
}

bool SchoolDaysReplayModel::CancelSceneCheck() {
    if (!sceneCheckOpen_) return false;
    sceneCheckCmap_.Clear();
    sceneCheckOpen_ = false;
    sceneCheckSelection_ = -1;
    sceneCheckChoiceCount_ = 0;
    sceneCheckContextIndex_ = -1;
    sceneCheckKey_ = -1;
    sceneCheckChoiceAvailable_.fill(false);
    return true;
}

bool SchoolDaysReplayModel::Activate(Action* out) {
    if (!open_ || out == nullptr) return false;
    *out = {};

    if (sceneCheckOpen_) {
        const int choice = sceneCheckSelection_;
        if (choice < 0 || choice >= sceneCheckChoiceCount_ ||
            !sceneCheckChoiceAvailable_[static_cast<size_t>(choice)]) return false;
        const int contextIndex = sceneCheckContextIndex_;
        const int choiceCount = sceneCheckChoiceCount_;
        const int key = sceneCheckKey_;
        out->kind = ActionKind::SceneCheckAccepted;
        out->index = contextIndex;
        out->choice = choice;
        out->choiceCount = choiceCount;
        out->sceneCheckKey = key;
        sceneCheckCmap_.Clear();
        sceneCheckOpen_ = false;
        sceneCheckSelection_ = -1;
        sceneCheckChoiceCount_ = 0;
        sceneCheckContextIndex_ = -1;
        sceneCheckKey_ = -1;
        sceneCheckChoiceAvailable_.fill(false);
        return true;
    }

    const int control = selection_;
    if (control < 0) return false;
    if (control == 0) return SwitchView(assetsRoot_, View::HScene, nullptr);
    if (control == 1) return SwitchView(assetsRoot_, View::PlayData, nullptr);
    if (control == 2) {
        out->kind = ActionKind::Close;
        return true;
    }

    if (view_ == View::HScene) {
        if (control >= 3 && control <= 6) {
            page_ = control - 3;
            selection_ = -1;
            return true;
        }
        if (control >= 7 && control <= 18) {
            const int index = page_ * 12 + (control - 7);
            if (!HSceneAvailable(index)) return false;
            if (index == 11) return OpenSceneCheck(assetsRoot_, index, 4, 0, out);
            if (index == 22) return OpenSceneCheck(assetsRoot_, index, 2, 2, out);
            if (index == 30) return OpenSceneCheck(assetsRoot_, index, 2, 3, out);
            out->kind = ActionKind::HSceneRequested;
            out->index = index;
            return true;
        }
        return false;
    }

    if (control >= 13 && control <= 22) {
        page_ = control - 13;
        selection_ = -1;
        return true;
    }
    int row = -1;
    if (control >= 3 && control <= 12) row = control - 3;
    else if (control >= 23 && control <= 32) row = control - 23;
    if (row >= 0) {
        const int index = page_ * 10 + row;
        if (!PlayDataAvailable(index)) return false;
        out->kind = ActionKind::PlayDataRequested;
        out->index = index;
        return true;
    }
    return false;
}

} // namespace Kotonoha
