#pragma once

#include <cstdint>

#include <SDL3/SDL.h>
#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

class Gameplay;
class Kotonoha;

class SchoolDaysKtrfAppController {
public:
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

    bool ContinueHandoff(Kotonoha_KtrfError* error);
    SDL_AppResult Main(Gameplay** out);

private:
    static Gameplay* CreateGameplay(const char* orsPath,
                                    Kotonoha_Game* gameContext,
                                    void* userdata);
    static void DestroyGameplay(Gameplay* gameplay, void* userdata);

    Kotonoha* engine = nullptr;
    Kotonoha_Game* gameContext = nullptr;
    SchoolDaysKtrfGameplayBridge bridge;
    bool opened = false;
};

} // namespace Kotonoha
