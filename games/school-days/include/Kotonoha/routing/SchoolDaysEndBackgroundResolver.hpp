#pragma once

#include <string>
#include <vector>

#include <Kotonoha/routing/KtrfStateStore.hpp>

namespace Kotonoha {

// Selection logic recovered from EXE 0x0041EB20 / 0x0041FEE0.  This is only
// the end-background decision used by TitleView; it does not own title UI,
// transitions, gallery/extras unlocks, or terminal-flow policy.
class SchoolDaysEndBackgroundResolver {
public:
    struct Profile {
        bool endBgView = false;
        int endingMax = 0;
        std::string baseAsset;
        // STARTSCRIPT.INI [TitleBGM] logical stem.  The retail Title path
        // appends _loop.ogg and accepts a loop-only asset.
        std::string titleBgmStem;
        // STARTSCRIPT.INI [SystemBGM] logical stem. SysMenu/SceneView own this
        // separately from TitleBGM and release it before replay FILM playback.
        std::string systemBgmStem;
        std::string allClearAsset;
        std::vector<std::string> endingAssets;
    };

    enum class Kind {
        Base,
        Ending,
        AllClear,
    };

    struct Result {
        Kind kind = Kind::Base;
        std::string asset;
        int endingIndex = -1; // zero-based EndNo, only for Kind::Ending
        bool promotedAllClear = false;
    };

    static bool Resolve(const Profile& profile, KtrfStateStore* state,
                        Result* out, Kotonoha_KtrfError* error);

private:
    static bool Fail(Kotonoha_KtrfError* error,
                     Kotonoha_KtrfErrorCode code, const char* message);
};

} // namespace Kotonoha
