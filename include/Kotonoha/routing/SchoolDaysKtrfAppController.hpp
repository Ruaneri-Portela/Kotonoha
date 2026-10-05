#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

class Gameplay;
class Kotonoha;

class SchoolDaysKtrfAppController {
public:
    struct DebugSceneEntry {
        uint32_t nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
        std::string sceneKey;
    };

    SchoolDaysKtrfAppController() = default;
    ~SchoolDaysKtrfAppController();

    SchoolDaysKtrfAppController(const SchoolDaysKtrfAppController&) = delete;
    SchoolDaysKtrfAppController& operator=(const SchoolDaysKtrfAppController&) = delete;

    bool Open(const char* ktrfPath, const char* orsRoot,
              Kotonoha* engine, Kotonoha_Game* gameContext,
              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened; }
    bool HasPendingHandoff() const { return bridge.HasPendingHandoff(); }
    bool PendingHandoffIsTerminal() const {
        return bridge.PendingHandoffIsTerminal();
    }

    Gameplay* CurrentGameplay() const { return bridge.CurrentGameplay(); }
    const SchoolDaysKtrfScene& CurrentScene() const { return bridge.CurrentScene(); }

    std::size_t DebugSceneCount() const { return debugScenes.size(); }
    const DebugSceneEntry* DebugSceneAt(std::size_t ordinal) const;
    int DebugCurrentSceneOrdinal() const;
    bool DebugJumpToScene(std::size_t ordinal, Kotonoha_KtrfError* error);
    bool DebugJumpRelative(int delta, Kotonoha_KtrfError* error);

    bool ContinueHandoff(Kotonoha_KtrfError* error);
    SDL_AppResult Main(Gameplay** out);

private:
    static Gameplay* CreateGameplay(const char* orsPath,
                                    Kotonoha_Game* gameContext,
                                    void* userdata);
    static void DestroyGameplay(Gameplay* gameplay, void* userdata);

    bool BuildDebugSceneCatalog(Kotonoha_KtrfError* error);

    Kotonoha* engine = nullptr;
    Kotonoha_Game* gameContext = nullptr;
    SchoolDaysKtrfGameplayBridge bridge;
    std::vector<DebugSceneEntry> debugScenes;
    bool opened = false;
};

} // namespace Kotonoha
