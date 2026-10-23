#pragma once

#include <array>
#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// Pure MENU::SceneView + nested MENU::SceneCheck pointer model.  The original
// mode8 SceneCheck is kept internal here rather than promoted to a top-level
// flow state.  Playback/unlock ownership remains outside this model.
class SchoolDaysReplayModel {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    enum class View { HScene, PlayData };
    enum class ActionKind {
        None,
        Close,
        HSceneRequested,
        PlayDataRequested,
        SceneCheckRequested,
        SceneCheckAccepted,
    };

    struct Action {
        ActionKind kind = ActionKind::None;
        int index = -1;       // absolute HScene/PlayData index
        int choice = -1;      // nested SceneCheck choice, when accepted
        int choiceCount = 0;  // 2/4 for SceneCheckRequested
        int sceneCheckKey = -1; // authenticated staging key: 0/2/3
    };

    bool Open(const std::string& assetsRoot, Variant variant,
              View view = View::HScene, std::string* error = nullptr);
    void Close();

    bool SwitchView(const std::string& assetsRoot, View view,
                    std::string* error = nullptr);
    bool SetSelectionFromPointer(int x, int y);
    int HitControl(int x, int y) const;
    bool Activate(Action* out);

    // Unlock/availability is owned by the host.  Defaults are fail-closed.
    void SetHSceneAvailable(int index, bool available);
    void SetPlayDataAvailable(int index, bool available);
    void SetSceneCheckChoiceAvailable(int choice, bool available);
    void ClearAvailability();

    // SceneCheck cancel is a semantic operation whose original physical key
    // binding is not authenticated.  The presenter must not invent a key.
    bool CancelSceneCheck();

    bool IsOpen() const { return open_; }
    bool SceneCheckOpen() const { return sceneCheckOpen_; }
    View CurrentView() const { return view_; }
    Variant CurrentVariant() const { return variant_; }
    int Page() const { return page_; }
    int Selection() const { return selection_; }
    int SceneCheckSelection() const { return sceneCheckSelection_; }
    int SceneCheckChoiceCount() const { return sceneCheckChoiceCount_; }
    int SceneCheckContextIndex() const { return sceneCheckContextIndex_; }
    int SceneCheckKey() const { return sceneCheckKey_; }
    int SceneCheckPreviewRecord() const {
        if (!sceneCheckOpen_) return -1;
        if (sceneCheckChoiceCount_ == 4) return 4;
        if (sceneCheckKey_ == 2) return 3;
        if (sceneCheckKey_ == 3) return 4;
        return -1;
    }
    bool IsHSceneAvailable(int index) const { return HSceneAvailable(index); }
    bool IsPlayDataAvailable(int index) const { return PlayDataAvailable(index); }
    bool IsSceneCheckChoiceAvailable(int choice) const {
        return choice >= 0 && choice < static_cast<int>(sceneCheckChoiceAvailable_.size()) &&
               sceneCheckChoiceAvailable_[static_cast<size_t>(choice)];
    }
    const SchoolDaysCmap& Cmap() const {
        return sceneCheckOpen_ ? sceneCheckCmap_ : cmap_;
    }

    static std::string CmapRelativePath(View view, Variant variant);
    static std::string SceneCheckCmapRelativePath(int choiceCount,
                                                  Variant variant);
    static const char* BaseRelativePath(View view);
    static const char* ChipRelativePath(View view);
    static std::string SceneCheckBaseRelativePath(int choiceCount);
    static std::string SceneCheckChipRelativePath(int choiceCount);
    static int HSceneUnlockCount() { return 41; }
    static const char* HSceneUnlockKey(int index);
    static const char* SceneCheckAvailabilityKey(int sceneCheckKey, int choice);

private:
    bool LoadViewCmap(const std::string& assetsRoot, View view,
                      std::string* error);
    bool OpenSceneCheck(const std::string& assetsRoot, int absoluteIndex,
                        int choiceCount, int key, Action* out,
                        std::string* error = nullptr);
    bool HSceneAvailable(int index) const;
    bool PlayDataAvailable(int index) const;

    std::string assetsRoot_;
    SchoolDaysCmap cmap_;
    SchoolDaysCmap sceneCheckCmap_;
    Variant variant_ = Variant::Standard;
    View view_ = View::HScene;
    int page_ = 0;
    int selection_ = -1;
    int sceneCheckSelection_ = -1;
    int sceneCheckChoiceCount_ = 0;
    int sceneCheckContextIndex_ = -1;
    int sceneCheckKey_ = -1;
    std::array<bool, 48> hSceneAvailable_{};
    std::array<bool, 100> playDataAvailable_{};
    std::array<bool, 4> sceneCheckChoiceAvailable_{};
    bool sceneCheckOpen_ = false;
    bool open_ = false;
};

} // namespace Kotonoha
