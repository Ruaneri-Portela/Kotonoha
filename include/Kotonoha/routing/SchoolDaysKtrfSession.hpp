#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/routing/SchoolDaysKtrfAdapter.h>
#include <Kotonoha/routing/SchoolDaysKtrfAssetResolver.h>
}

namespace Kotonoha {

struct SchoolDaysKtrfScene {
    uint32_t nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
    uint32_t resourceIndex = KOTONOHA_KTRF_NULL_INDEX;
    std::string sceneKey;
    std::string orsPath;
};

class SchoolDaysKtrfSession {
public:
    enum class StepKind {
        Unresolved,
        BlockedChoice,
        Scene,
        Terminal,
    };

    struct StepResult {
        StepKind kind = StepKind::Unresolved;
        uint32_t transitionIndex = KOTONOHA_KTRF_NULL_INDEX;
        uint32_t hopCount = 0;
        SchoolDaysKtrfScene scene;
        std::vector<std::string> hooks;
        std::vector<int64_t> endings;

        bool HasHook(const std::string& symbol) const;
    };

    SchoolDaysKtrfSession();
    ~SchoolDaysKtrfSession();

    SchoolDaysKtrfSession(const SchoolDaysKtrfSession&) = delete;
    SchoolDaysKtrfSession& operator=(const SchoolDaysKtrfSession&) = delete;

    bool Open(const char* ktrfPath, const char* assetsRoot,
              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened; }
    bool ResetNewGame(SchoolDaysKtrfScene* scene, Kotonoha_KtrfError* error);
    bool CurrentScene(SchoolDaysKtrfScene* scene, Kotonoha_KtrfError* error) const;
    bool CommitChoice(int64_t value, int* accepted, Kotonoha_KtrfError* error);
    bool Advance(const char* trigger, StepResult* out, Kotonoha_KtrfError* error);

    // Debug-only scene support. ResolveSceneNode validates and probes the
    // physical ORS without changing router state. DebugActivateNode then moves
    // the router to that physical node while preserving runtime variables.
    bool ResolveSceneNode(uint32_t nodeIndex, SchoolDaysKtrfScene* scene,
                          Kotonoha_KtrfError* error) const;
    bool DebugActivateNode(uint32_t nodeIndex, Kotonoha_KtrfError* error);

    const std::string& AssetsRoot() const { return assetsRoot; }
    const Kotonoha_SchoolDaysKtrfAdapter* Adapter() const { return &adapter; }

private:
    static int OnEnding(int64_t endingCode, void* userdata,
                        Kotonoha_KtrfError* error);
    static int OnHook(const char* symbol, size_t symbolLength, void* userdata,
                      Kotonoha_KtrfError* error);

    bool FillScene(const Kotonoha_SchoolDaysKtrfOrsAsset& asset,
                   SchoolDaysKtrfScene* out) const;

    Kotonoha_KtrfDocument document{};
    Kotonoha_SchoolDaysKtrfAdapter adapter{};
    std::string assetsRoot;
    std::vector<std::string> pendingHooks;
    std::vector<int64_t> pendingEndings;
    bool opened = false;
};

} // namespace Kotonoha
