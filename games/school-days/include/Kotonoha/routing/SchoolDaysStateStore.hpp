#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

extern "C" {
#include <Kotonoha/routing/SchoolDaysKtrfAdapter.h>
}

namespace Kotonoha {

// Semantic School Days state facade over the native KTRF runtime.
//
// KTRF remains the live authority for every variable represented by the
// document.  The detached maps exist only for original School Days symbols
// that are not yet represented by the forward KTRF corpus (notably SP markers
// and a small set of Back-only BS keys).  This avoids introducing a second
// ROUTE/SCENE/session/global authority while still giving R1/R2 one semantic
// API.
class SchoolDaysStateStore {
public:
    struct Snapshot {
        uint32_t currentNodeIndex = KOTONOHA_KTRF_NULL_INDEX;
        int terminal = 0;
        uint32_t lastRegisteredEnding = 0u;
        int hasRegisteredEnding = 0;
        std::vector<Kotonoha_KtrfRuntimeValue> variables;
        std::vector<uint8_t> expressionState;
        std::vector<uint8_t> choiceCommitted;
        std::vector<Kotonoha_KtrfRuntimeValue> choiceValues;
        std::unordered_map<std::string, int64_t> detachedSession;
        std::unordered_map<std::string, int64_t> detachedGlobal;
    };

    SchoolDaysStateStore() = default;

    bool Bind(Kotonoha_SchoolDaysKtrfAdapter* adapter,
              Kotonoha_KtrfError* error);
    void Unbind();

    bool IsBound() const { return adapter_ != nullptr; }
    bool HasRuntimeSession(const std::string& symbol) const;
    bool HasRuntimeGlobal(const std::string& symbol) const;

    bool ReadSessionInt(const std::string& symbol, int64_t* value,
                        Kotonoha_KtrfError* error) const;
    bool WriteSessionInt(const std::string& symbol, int64_t value,
                         Kotonoha_KtrfError* error);
    bool AddSessionInt(const std::string& symbol, int64_t delta,
                       Kotonoha_KtrfError* error);

    bool ReadGlobalInt(const std::string& symbol, int64_t* value,
                       Kotonoha_KtrfError* error) const;
    bool WriteGlobalInt(const std::string& symbol, int64_t value,
                        Kotonoha_KtrfError* error);

    // Replace an entire numeric FlagsHash projection transactionally. Runtime
    // variables in the selected namespace are first reset to zero and detached
    // numeric keys are discarded, matching load of a fresh FlagsHash snapshot
    // rather than overlaying a save on top of stale live state.
    bool ReplaceSessionInts(
        const std::vector<std::pair<std::string, int64_t>>& values,
        Kotonoha_KtrfError* error);
    bool ReplaceGlobalInts(
        const std::vector<std::pair<std::string, int64_t>>& values,
        Kotonoha_KtrfError* error);

    bool EnumerateSessionInts(std::unordered_map<std::string, int64_t>* values,
                              Kotonoha_KtrfError* error) const;
    bool EnumerateGlobalInts(std::unordered_map<std::string, int64_t>* values,
                             Kotonoha_KtrfError* error) const;

    bool ReadCoordinate(int* route, int* scene,
                        Kotonoha_KtrfError* error) const;
    bool WriteCoordinate(int route, int scene,
                         Kotonoha_KtrfError* error);

    bool Capture(Snapshot* snapshot, Kotonoha_KtrfError* error) const;
    bool Restore(const Snapshot& snapshot, Kotonoha_KtrfError* error);
    bool ResetChoicesForNode(uint32_t nodeIndex, Kotonoha_KtrfError* error);

    // Detached state is intentionally visible for persistence/tests.  Runtime
    // backed variables never appear here.
    const std::unordered_map<std::string, int64_t>& DetachedSession() const {
        return detachedSession_;
    }
    const std::unordered_map<std::string, int64_t>& DetachedGlobal() const {
        return detachedGlobal_;
    }

private:
    static bool SetError(Kotonoha_KtrfError* error,
                         Kotonoha_KtrfErrorCode code,
                         const char* message);
    static bool RuntimeValueToInt(const Kotonoha_KtrfRuntimeValue& value,
                                  int64_t* out,
                                  Kotonoha_KtrfError* error);

    bool ReadIndexed(const std::unordered_map<std::string, uint32_t>& index,
                     const std::unordered_map<std::string, int64_t>& detached,
                     const std::string& symbol, int64_t* value,
                     Kotonoha_KtrfError* error) const;
    bool WriteIndexed(const std::unordered_map<std::string, uint32_t>& index,
                      std::unordered_map<std::string, int64_t>* detached,
                      const std::string& symbol, int64_t value,
                      Kotonoha_KtrfError* error);
    bool ReplaceIndexed(
        const std::unordered_map<std::string, uint32_t>& index,
        std::unordered_map<std::string, int64_t>* detached,
        const std::vector<std::pair<std::string, int64_t>>& values,
        Kotonoha_KtrfError* error);
    bool EnumerateIndexed(
        const std::unordered_map<std::string, uint32_t>& index,
        const std::unordered_map<std::string, int64_t>& detached,
        std::unordered_map<std::string, int64_t>* values,
        Kotonoha_KtrfError* error) const;

    Kotonoha_SchoolDaysKtrfAdapter* adapter_ = nullptr;
    std::unordered_map<std::string, uint32_t> sessionIndex_;
    std::unordered_map<std::string, uint32_t> globalIndex_;
    std::unordered_map<std::string, uint32_t> internalIndex_;
    std::unordered_map<std::string, int64_t> detachedSession_;
    std::unordered_map<std::string, int64_t> detachedGlobal_;
};

} // namespace Kotonoha
