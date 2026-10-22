#include <Kotonoha/routing/SchoolDaysHistory.hpp>

#include <cstdio>

namespace Kotonoha {

namespace {

bool HistoryError(Kotonoha_KtrfError* error, const char* message) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

} // namespace

void SchoolDaysHistory::Clear() {
    presented_ = {};
    baselineFeeling_.fill(0);
    entries_.clear();
    hasPresented_ = false;
}

bool SchoolDaysHistory::ReadFeelings(
    const SchoolDaysStateStore& state,
    std::array<int64_t, kFeelingCount>* values,
    Kotonoha_KtrfError* error) {
    if (values == nullptr) return HistoryError(error, "history feeling output is null");
    static constexpr const char* kNames[kFeelingCount] = {
        "000", "001", "002", "003", "004"
    };
    for (size_t i = 0; i < kFeelingCount; ++i) {
        if (!state.ReadSessionInt(kNames[i], &(*values)[i], error)) return false;
    }
    return true;
}

bool SchoolDaysHistory::Rebase(const SchoolDaysCoordinate& presented,
                               const SchoolDaysStateStore& state,
                               Kotonoha_KtrfError* error) {
    std::array<int64_t, kFeelingCount> feelings{};
    if (!ReadFeelings(state, &feelings, error)) return false;
    presented_ = presented;
    baselineFeeling_ = feelings;
    entries_.clear();
    hasPresented_ = true;
    return true;
}

bool SchoolDaysHistory::BuildForwardEntry(
    const SchoolDaysCoordinate& destination,
    const SchoolDaysStateStore& state, Entry* entry,
    Kotonoha_KtrfError* error) const {
    if (!hasPresented_ || entry == nullptr) {
        return HistoryError(error, "history has no presented source scene");
    }
    std::array<int64_t, kFeelingCount> now{};
    if (!ReadFeelings(state, &now, error)) return false;

    entry->source = presented_;
    entry->destination = destination;
    for (size_t i = 0; i < kFeelingCount; ++i) {
        entry->inverseFeeling[i] = now[i] - baselineFeeling_[i];
    }
    return true;
}

bool SchoolDaysHistory::CommitForward(const Entry& entry) {
    if (!hasPresented_ || !(entry.source == presented_)) return false;
    entries_.push_back(entry);
    presented_ = entry.destination;
    for (size_t i = 0; i < kFeelingCount; ++i) {
        baselineFeeling_[i] += entry.inverseFeeling[i];
    }
    return true;
}

bool SchoolDaysHistory::LatestPredecessor(
    const SchoolDaysCoordinate& current,
    SchoolDaysCoordinate* predecessor) const {
    if (predecessor == nullptr || entries_.empty()) return false;
    const Entry& entry = entries_.back();
    if (!(entry.destination == current)) return false;
    *predecessor = entry.source;
    return true;
}

bool SchoolDaysHistory::PopIfCurrent(const SchoolDaysCoordinate& current) {
    if (entries_.empty()) return false;
    const Entry entry = entries_.back();
    if (!(entry.destination == current)) return false;
    entries_.pop_back();
    presented_ = entry.source;
    // The new baseline is the state that existed when the predecessor was
    // presented.  Subtracting the recorded delta from the destination's
    // baseline reconstructs it for auditing purposes without mutating Store.
    for (size_t i = 0; i < kFeelingCount; ++i) {
        baselineFeeling_[i] -= entry.inverseFeeling[i];
    }
    hasPresented_ = true;
    return true;
}

} // namespace Kotonoha
