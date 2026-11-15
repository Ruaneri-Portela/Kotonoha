#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/KtrfStateStore.hpp>

namespace Kotonoha {

struct SchoolDaysCoordinate {
    int route = 0;
    int scene = 0;
    uint32_t nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
    std::string sceneKey;

    bool operator==(const SchoolDaysCoordinate& other) const {
        return route == other.route && scene == other.scene &&
               nodeIndex == other.nodeIndex && sceneKey == other.sceneKey;
    }
};

class SchoolDaysHistory {
public:
    static constexpr size_t kFeelingCount = 5u;

    struct Entry {
        SchoolDaysCoordinate source;
        SchoolDaysCoordinate destination;
        std::array<int64_t, kFeelingCount> inverseFeeling{};
    };

    void Clear();
    bool Rebase(const SchoolDaysCoordinate& presented,
                const KtrfStateStore& state,
                Kotonoha_KtrfError* error);

    // Called after a semantic forward advance has selected destination, and
    // only committed once that destination has actually become presented.
    // The inverse vector is derived from the difference between state at the
    // last presented baseline and state just before/after the forward routing
    // step.  It is an audit/rollback aid; executable Back rules remain the
    // authority for semantic reversal.
    bool BuildForwardEntry(const SchoolDaysCoordinate& destination,
                           const KtrfStateStore& state,
                           Entry* entry,
                           Kotonoha_KtrfError* error) const;
    bool CommitForward(const Entry& entry);

    bool LatestPredecessor(const SchoolDaysCoordinate& current,
                           SchoolDaysCoordinate* predecessor) const;
    bool PopIfCurrent(const SchoolDaysCoordinate& current);

    const SchoolDaysCoordinate& Presented() const { return presented_; }
    const std::vector<Entry>& Entries() const { return entries_; }
    bool HasPresented() const { return hasPresented_; }

private:
    static bool ReadFeelings(const KtrfStateStore& state,
                             std::array<int64_t, kFeelingCount>* values,
                             Kotonoha_KtrfError* error);

    SchoolDaysCoordinate presented_{};
    std::array<int64_t, kFeelingCount> baselineFeeling_{};
    std::vector<Entry> entries_;
    bool hasPresented_ = false;
};

} // namespace Kotonoha
