#pragma once

#include <cstddef>
#include <string>

namespace Kotonoha {

// Pure v1.02 MENU::SceneView replay-script oracle.
//
// Ordinary HScene entries expose the authenticated static table order.  The
// three special entries (11/22/30) expose the selected native table as a
// descriptor only: their transition cursor is resolved by the native replay
// selector and must not be flattened into sceneKeys[n + 1] playback.
class SchoolDaysReplayScriptOracle final {
public:
    enum class Kind {
        Ordinary,
        SpecialChoiceTable,
    };

    enum class TransitionCursorResolution {
        Invalid,
        StaticTableOrder,
        NativeSelectorRequired,
    };

    struct SequenceDescriptor {
        const char* const* sceneKeys = nullptr;
        std::size_t count = 0;
    };

    struct ChoiceDescriptor {
        int choice = -1;
        SequenceDescriptor sequence{};
    };

    struct HSceneDescriptor {
        int index = -1;
        Kind kind = Kind::Ordinary;
        int sceneCheckKey = -1; // 0/2/3 for indices 11/22/30.
        SequenceDescriptor ordinary{};
        const ChoiceDescriptor* choices = nullptr;
        std::size_t choiceCount = 0;
        TransitionCursorResolution cursorResolution =
            TransitionCursorResolution::Invalid;
    };

    static constexpr int HSceneCount() { return 41; }

    // When orsRoot is non-null, every SceneKey exposed by the returned
    // descriptor is first checked as <orsRoot>/<SceneKey>.ENG.ORS.
    static const HSceneDescriptor* HScene(int index,
                                          const char* orsRoot = nullptr,
                                          std::string* error = nullptr);

    // Ordinary entries only. Special indices return nullptr.
    static const SequenceDescriptor* OrdinarySequence(
        int index, const char* orsRoot = nullptr, std::string* error = nullptr);

    // Special entries only. This returns the chosen table descriptor; callers
    // still need native transition-cursor resolution before advancing it.
    static const SequenceDescriptor* SpecialChoiceSequence(
        int index, int choice, const char* orsRoot = nullptr,
        std::string* error = nullptr);

    // Resolves the next native cursor for the three choice-driven replay
    // entries. priorSelectResult uses the original FILM convention:
    // -2 = no selection/pending, -1 = timed/no choice, 0/1 = selected option.
    // A successful terminal edge returns completed=true and nextCursor=-1.
    static bool ResolveSpecialNextCursor(int index, int choice,
                                         int currentCursor,
                                         int priorSelectResult,
                                         int* nextCursor, bool* completed,
                                         std::string* error = nullptr);

    static TransitionCursorResolution CursorResolutionFor(int index);

    // Checks the complete oracle corpus (ordinary + every special choice).
    static bool ValidateCorpus(const char* orsRoot,
                               std::string* error = nullptr);

    static std::string OrsRelativePath(const char* sceneKey);
};

} // namespace Kotonoha
