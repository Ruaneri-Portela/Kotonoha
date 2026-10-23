#include <Kotonoha/routing/SchoolDaysStateStore.hpp>

#include <cstdio>
#include <cstring>
#include <limits>

namespace Kotonoha {
namespace {

constexpr const char* kSessionPrefix = "sdhq:var:session:";
constexpr const char* kGlobalPrefix = "sdhq:var:global:";
constexpr const char* kInternalPrefix = "sdhq:var:internal:";

uint32_t VariableCount(const Kotonoha_KtrfDocument* document) {
    if (document == nullptr) return 0u;
    const Kotonoha_KtrfSection* section =
        Kotonoha_KtrfFindSection(document, "VARS");
    return section != nullptr ? section->item_count : 0u;
}

bool ExtractSymbol(const char* raw, size_t size, const char* prefix,
                   std::string* symbol) {
    const size_t prefixSize = std::strlen(prefix);
    if (raw == nullptr || symbol == nullptr || size < prefixSize ||
        std::memcmp(raw, prefix, prefixSize) != 0) {
        return false;
    }
    symbol->assign(raw + prefixSize, size - prefixSize);
    return !symbol->empty();
}

} // namespace

bool SchoolDaysStateStore::SetError(Kotonoha_KtrfError* error,
                                    Kotonoha_KtrfErrorCode code,
                                    const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message : "School Days state error");
    }
    return false;
}

bool SchoolDaysStateStore::RuntimeValueToInt(
    const Kotonoha_KtrfRuntimeValue& value, int64_t* out,
    Kotonoha_KtrfError* error) {
    if (out == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days integer output is null");
    }
    switch (value.kind) {
    case KOTONOHA_KTRF_RUNTIME_BOOL:
        *out = value.as.boolean_value != 0 ? 1 : 0;
        return true;
    case KOTONOHA_KTRF_RUNTIME_INT64:
        *out = value.as.int64_value;
        return true;
    case KOTONOHA_KTRF_RUNTIME_UINT64:
        if (value.as.uint64_value >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return SetError(error, KOTONOHA_KTRF_ERROR_RANGE,
                            "School Days variable exceeds signed integer range");
        }
        *out = static_cast<int64_t>(value.as.uint64_value);
        return true;
    case KOTONOHA_KTRF_RUNTIME_FLOAT64:
        *out = static_cast<int64_t>(value.as.float64_value);
        return true;
    default:
        return SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days variable is not numeric");
    }
}

bool SchoolDaysStateStore::Bind(Kotonoha_SchoolDaysKtrfAdapter* adapter,
                                Kotonoha_KtrfError* error) {
    Unbind();
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (adapter == nullptr || adapter->document == nullptr ||
        adapter->router.document == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store requires a bound adapter");
    }

    adapter_ = adapter;
    const uint32_t count = VariableCount(adapter_->document);
    for (uint32_t i = 0u; i < count; ++i) {
        Kotonoha_KtrfVariable variable{};
        const char* raw = nullptr;
        size_t size = 0u;
        if (!Kotonoha_KtrfGetVariable(adapter_->document, i, &variable, error) ||
            !Kotonoha_KtrfGetString(adapter_->document, variable.id_str,
                                    &raw, &size, error)) {
            Unbind();
            return false;
        }
        std::string symbol;
        if (ExtractSymbol(raw, size, kSessionPrefix, &symbol)) {
            sessionIndex_.emplace(std::move(symbol), i);
        }
        else if (ExtractSymbol(raw, size, kGlobalPrefix, &symbol)) {
            globalIndex_.emplace(std::move(symbol), i);
        }
        else if (ExtractSymbol(raw, size, kInternalPrefix, &symbol)) {
            internalIndex_.emplace(std::move(symbol), i);
        }
    }

    if (!HasRuntimeSession("ROUTE") || !HasRuntimeSession("SCENE")) {
        Unbind();
        return SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days KTRF document lacks ROUTE/SCENE variables");
    }
    return true;
}

void SchoolDaysStateStore::Unbind() {
    adapter_ = nullptr;
    sessionIndex_.clear();
    globalIndex_.clear();
    internalIndex_.clear();
    detachedSession_.clear();
    detachedGlobal_.clear();
}

bool SchoolDaysStateStore::HasRuntimeSession(const std::string& symbol) const {
    return sessionIndex_.find(symbol) != sessionIndex_.end();
}

bool SchoolDaysStateStore::HasRuntimeGlobal(const std::string& symbol) const {
    return globalIndex_.find(symbol) != globalIndex_.end();
}

bool SchoolDaysStateStore::ReadIndexed(
    const std::unordered_map<std::string, uint32_t>& index,
    const std::unordered_map<std::string, int64_t>& detached,
    const std::string& symbol, int64_t* value,
    Kotonoha_KtrfError* error) const {
    if (adapter_ == nullptr || value == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store is not bound or output is null");
    }
    const auto it = index.find(symbol);
    if (it != index.end()) {
        Kotonoha_KtrfRuntimeValue runtimeValue{};
        if (!Kotonoha_KtrfRuntimeGetVariable(&adapter_->router.runtime,
                                             it->second, &runtimeValue, error)) {
            return false;
        }
        return RuntimeValueToInt(runtimeValue, value, error);
    }
    const auto detachedIt = detached.find(symbol);
    *value = detachedIt != detached.end() ? detachedIt->second : 0;
    return true;
}

bool SchoolDaysStateStore::WriteIndexed(
    const std::unordered_map<std::string, uint32_t>& index,
    std::unordered_map<std::string, int64_t>* detached,
    const std::string& symbol, int64_t value,
    Kotonoha_KtrfError* error) {
    if (adapter_ == nullptr || detached == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store is not bound");
    }
    const auto it = index.find(symbol);
    if (it != index.end()) {
        Kotonoha_KtrfRuntimeValue runtimeValue{};
        runtimeValue.kind = KOTONOHA_KTRF_RUNTIME_INT64;
        runtimeValue.as.int64_value = value;
        return Kotonoha_KtrfRuntimeSetVariable(&adapter_->router.runtime,
                                               it->second, &runtimeValue,
                                               error) != 0;
    }
    (*detached)[symbol] = value;
    return true;
}

bool SchoolDaysStateStore::ReadSessionInt(const std::string& symbol,
                                          int64_t* value,
                                          Kotonoha_KtrfError* error) const {
    return ReadIndexed(sessionIndex_, detachedSession_, symbol, value, error);
}

bool SchoolDaysStateStore::WriteSessionInt(const std::string& symbol,
                                           int64_t value,
                                           Kotonoha_KtrfError* error) {
    return WriteIndexed(sessionIndex_, &detachedSession_, symbol, value, error);
}

bool SchoolDaysStateStore::AddSessionInt(const std::string& symbol,
                                         int64_t delta,
                                         Kotonoha_KtrfError* error) {
    int64_t current = 0;
    if (!ReadSessionInt(symbol, &current, error)) return false;
    if ((delta > 0 && current > std::numeric_limits<int64_t>::max() - delta) ||
        (delta < 0 && current < std::numeric_limits<int64_t>::min() - delta)) {
        return SetError(error, KOTONOHA_KTRF_ERROR_RANGE,
                        "School Days integer addition overflow");
    }
    // Deliberately no gameplay clamp.  The original Back subtraction path
    // stores the signed integer directly.
    return WriteSessionInt(symbol, current + delta, error);
}

bool SchoolDaysStateStore::ReadGlobalInt(const std::string& symbol,
                                         int64_t* value,
                                         Kotonoha_KtrfError* error) const {
    return ReadIndexed(globalIndex_, detachedGlobal_, symbol, value, error);
}

bool SchoolDaysStateStore::WriteGlobalInt(const std::string& symbol,
                                          int64_t value,
                                          Kotonoha_KtrfError* error) {
    return WriteIndexed(globalIndex_, &detachedGlobal_, symbol, value, error);
}

bool SchoolDaysStateStore::ReplaceIndexed(
    const std::unordered_map<std::string, uint32_t>& index,
    std::unordered_map<std::string, int64_t>* detached,
    const std::vector<std::pair<std::string, int64_t>>& values,
    Kotonoha_KtrfError* error) {
    if (adapter_ == nullptr || detached == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store is not bound");
    }

    Snapshot before;
    if (!Capture(&before, error)) return false;

    Kotonoha_KtrfRuntimeValue zero{};
    zero.kind = KOTONOHA_KTRF_RUNTIME_INT64;
    zero.as.int64_value = 0;
    for (const auto& item : index) {
        if (!Kotonoha_KtrfRuntimeSetVariable(&adapter_->router.runtime,
                                             item.second, &zero, error)) {
            Kotonoha_KtrfError ignored{};
            (void)Restore(before, &ignored);
            return false;
        }
    }
    detached->clear();

    for (const auto& item : values) {
        if (!WriteIndexed(index, detached, item.first, item.second, error)) {
            Kotonoha_KtrfError ignored{};
            (void)Restore(before, &ignored);
            return false;
        }
    }
    return true;
}

bool SchoolDaysStateStore::ReplaceSessionInts(
    const std::vector<std::pair<std::string, int64_t>>& values,
    Kotonoha_KtrfError* error) {
    return ReplaceIndexed(sessionIndex_, &detachedSession_, values, error);
}

bool SchoolDaysStateStore::ReplaceGlobalInts(
    const std::vector<std::pair<std::string, int64_t>>& values,
    Kotonoha_KtrfError* error) {
    return ReplaceIndexed(globalIndex_, &detachedGlobal_, values, error);
}

bool SchoolDaysStateStore::EnumerateIndexed(
    const std::unordered_map<std::string, uint32_t>& index,
    const std::unordered_map<std::string, int64_t>& detached,
    std::unordered_map<std::string, int64_t>* values,
    Kotonoha_KtrfError* error) const {
    if (adapter_ == nullptr || values == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state enumeration argument is invalid");
    }
    values->clear();
    values->reserve(index.size() + detached.size());
    for (const auto& item : index) {
        Kotonoha_KtrfRuntimeValue runtimeValue{};
        int64_t integer = 0;
        if (!Kotonoha_KtrfRuntimeGetVariable(&adapter_->router.runtime,
                                             item.second, &runtimeValue, error) ||
            !RuntimeValueToInt(runtimeValue, &integer, error)) {
            values->clear();
            return false;
        }
        values->emplace(item.first, integer);
    }
    for (const auto& item : detached) {
        (*values)[item.first] = item.second;
    }
    return true;
}

bool SchoolDaysStateStore::EnumerateSessionInts(
    std::unordered_map<std::string, int64_t>* values,
    Kotonoha_KtrfError* error) const {
    return EnumerateIndexed(sessionIndex_, detachedSession_, values, error);
}

bool SchoolDaysStateStore::EnumerateGlobalInts(
    std::unordered_map<std::string, int64_t>* values,
    Kotonoha_KtrfError* error) const {
    return EnumerateIndexed(globalIndex_, detachedGlobal_, values, error);
}

bool SchoolDaysStateStore::ReadCoordinate(int* route, int* scene,
                                          Kotonoha_KtrfError* error) const {
    if (route == nullptr || scene == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days coordinate output is null");
    }
    int64_t routeValue = 0;
    int64_t sceneValue = 0;
    if (!ReadSessionInt("ROUTE", &routeValue, error) ||
        !ReadSessionInt("SCENE", &sceneValue, error)) {
        return false;
    }
    if (routeValue < std::numeric_limits<int>::min() ||
        routeValue > std::numeric_limits<int>::max() ||
        sceneValue < std::numeric_limits<int>::min() ||
        sceneValue > std::numeric_limits<int>::max()) {
        return SetError(error, KOTONOHA_KTRF_ERROR_RANGE,
                        "School Days coordinate exceeds int range");
    }
    *route = static_cast<int>(routeValue);
    *scene = static_cast<int>(sceneValue);
    return true;
}

bool SchoolDaysStateStore::WriteCoordinate(int route, int scene,
                                           Kotonoha_KtrfError* error) {
    Snapshot before;
    if (!Capture(&before, error)) return false;
    if (WriteSessionInt("ROUTE", route, error) &&
        WriteSessionInt("SCENE", scene, error)) {
        return true;
    }
    Kotonoha_KtrfError ignored{};
    (void)Restore(before, &ignored);
    return false;
}

bool SchoolDaysStateStore::Capture(Snapshot* snapshot,
                                   Kotonoha_KtrfError* error) const {
    if (adapter_ == nullptr || snapshot == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state snapshot argument is invalid");
    }
    const Kotonoha_KtrfRouter& router = adapter_->router;
    snapshot->currentNodeIndex = router.current_node_index;
    snapshot->terminal = router.terminal;
    snapshot->lastRegisteredEnding = router.runtime.last_registered_ending;
    snapshot->hasRegisteredEnding = router.runtime.has_registered_ending;
    snapshot->variables.clear();
    snapshot->expressionState.clear();
    snapshot->choiceCommitted.clear();
    snapshot->choiceValues.clear();
    if (router.runtime.variable_count != 0u) {
        snapshot->variables.assign(
            router.runtime.variables,
            router.runtime.variables + router.runtime.variable_count);
    }
    if (router.runtime.expression_count != 0u) {
        snapshot->expressionState.assign(
            router.runtime.expression_state,
            router.runtime.expression_state + router.runtime.expression_count);
    }
    if (router.choice_count != 0u) {
        snapshot->choiceCommitted.assign(router.choice_committed,
                                         router.choice_committed + router.choice_count);
        snapshot->choiceValues.assign(router.choice_values,
                                      router.choice_values + router.choice_count);
    }
    snapshot->detachedSession = detachedSession_;
    snapshot->detachedGlobal = detachedGlobal_;
    return true;
}

bool SchoolDaysStateStore::Restore(const Snapshot& snapshot,
                                   Kotonoha_KtrfError* error) {
    if (adapter_ == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store is not bound");
    }
    Kotonoha_KtrfRouter& router = adapter_->router;
    if (snapshot.variables.size() != router.runtime.variable_count ||
        snapshot.expressionState.size() != router.runtime.expression_count ||
        snapshot.choiceCommitted.size() != router.choice_count ||
        snapshot.choiceValues.size() != router.choice_count) {
        return SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days state snapshot does not match bound KTRF document");
    }
    if (!snapshot.variables.empty()) {
        std::memcpy(router.runtime.variables, snapshot.variables.data(),
                    snapshot.variables.size() * sizeof(snapshot.variables[0]));
    }
    if (!snapshot.choiceCommitted.empty()) {
        std::memcpy(router.choice_committed, snapshot.choiceCommitted.data(),
                    snapshot.choiceCommitted.size());
        std::memcpy(router.choice_values, snapshot.choiceValues.data(),
                    snapshot.choiceValues.size() * sizeof(snapshot.choiceValues[0]));
    }
    if (!snapshot.expressionState.empty()) {
        std::memcpy(router.runtime.expression_state,
                    snapshot.expressionState.data(),
                    snapshot.expressionState.size());
    }
    router.current_node_index = snapshot.currentNodeIndex;
    router.terminal = snapshot.terminal;
    router.runtime.last_registered_ending = snapshot.lastRegisteredEnding;
    router.runtime.has_registered_ending = snapshot.hasRegisteredEnding;
    detachedSession_ = snapshot.detachedSession;
    detachedGlobal_ = snapshot.detachedGlobal;
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysStateStore::ResetChoicesForNode(uint32_t nodeIndex,
                                               Kotonoha_KtrfError* error) {
    if (adapter_ == nullptr || adapter_->document == nullptr) {
        return SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days state store is not bound");
    }
    Kotonoha_KtrfRouter& router = adapter_->router;
    for (uint32_t i = 0u; i < router.choice_count; ++i) {
        Kotonoha_KtrfChoice choice{};
        if (!Kotonoha_KtrfGetChoice(adapter_->document, i, &choice, error)) {
            return false;
        }
        if (choice.node_index != nodeIndex) continue;
        router.choice_committed[i] = 0u;
        std::memset(&router.choice_values[i], 0, sizeof(router.choice_values[i]));
    }
    // School Days' profile uses this internal value as the pending-choice
    // sentinel.  It is runtime-backed in the authenticated KTRF document.
    const auto choiceResult = internalIndex_.find("choice_result");
    if (choiceResult != internalIndex_.end()) {
        Kotonoha_KtrfRuntimeValue runtimeValue{};
        runtimeValue.kind = KOTONOHA_KTRF_RUNTIME_INT64;
        runtimeValue.as.int64_value = -2;
        return Kotonoha_KtrfRuntimeSetVariable(&router.runtime,
                                               choiceResult->second,
                                               &runtimeValue, error) != 0;
    }
    return true;
}

} // namespace Kotonoha
