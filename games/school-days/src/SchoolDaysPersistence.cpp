#include <Kotonoha/routing/SchoolDaysPersistence.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Kotonoha {
namespace {

#include "generated/SchoolDaysPersistenceEffectTypes.inc"

constexpr const char* kLiveChoiceCommitMarker =
    "__KOTONOHA_LIVE_CHOICE_COMMITTED";

bool Fail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
          const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

void ClearError(Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
}

const SchoolDaysPersistenceEffectTypeEntry* FindPersistenceEffect(
    uint32_t effectIndex) {
    const auto* begin = std::begin(kSchoolDaysPersistenceEffectTypes);
    const auto* end = std::end(kSchoolDaysPersistenceEffectTypes);
    const auto* found = std::lower_bound(
        begin, end, effectIndex,
        [](const SchoolDaysPersistenceEffectTypeEntry& entry, uint32_t index) {
            return entry.effect_index < index;
        });
    return found != end && found->effect_index == effectIndex ? found : nullptr;
}

const SchoolDaysGlobalPersistenceEffectEntry* FindGlobalPersistenceEffect(
    uint32_t sourceEffectIndex) {
    const auto* begin = std::begin(kSchoolDaysGlobalPersistenceEffects);
    const auto* end = std::end(kSchoolDaysGlobalPersistenceEffects);
    const auto* found = std::lower_bound(
        begin, end, sourceEffectIndex,
        [](const SchoolDaysGlobalPersistenceEffectEntry& entry,
           uint32_t index) {
            return entry.source_effect_index < index;
        });
    return found != end && found->source_effect_index == sourceEffectIndex
        ? found
        : nullptr;
}

bool StartsWith(const std::string& value, const char* prefix) {
    const size_t prefixLength = std::strlen(prefix);
    return value.size() >= prefixLength &&
           std::memcmp(value.data(), prefix, prefixLength) == 0;
}

bool ParseUnsignedSuffix(const std::string& value, const char* prefix,
                         uint32_t* out) {
    if (out == nullptr || !StartsWith(value, prefix)) return false;
    const char* begin = value.c_str() + std::strlen(prefix);
    if (*begin == '\0') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(begin, &end, 10);
    if (errno != 0 || end == begin || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *out = static_cast<uint32_t>(parsed);
    return true;
}

} // namespace

bool SchoolDaysPersistence::Utf16Ascii(const std::u16string& value,
                                       std::string* out,
                                       Kotonoha_KtrfError* error) {
    if (out == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days persistence string output is null");
    }
    out->clear();
    out->reserve(value.size());
    for (char16_t unit : value) {
        if (unit > 0x7fu) {
            out->clear();
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days persistence key is not ASCII");
        }
        out->push_back(static_cast<char>(unit));
    }
    return true;
}

std::u16string SchoolDaysPersistence::AsciiUtf16(const std::string& value) {
    std::u16string out;
    out.reserve(value.size());
    for (unsigned char ch : value) out.push_back(static_cast<char16_t>(ch));
    return out;
}

bool SchoolDaysPersistence::NumericEntries(
    const SchoolDaysFlgH& node,
    std::vector<std::pair<std::string, int64_t>>* values,
    Kotonoha_KtrfError* error) {
    if (values == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days numeric projection output is null");
    }
    values->clear();
    values->reserve(node.entries.size());
    for (const auto& entry : node.entries) {
        if (entry.type != 3 && entry.type != 11) continue;
        std::string key;
        if (!Utf16Ascii(entry.key, &key, error)) return false;
        values->emplace_back(std::move(key), entry.integer);
    }
    return true;
}

SchoolDaysFlgHEntry* SchoolDaysPersistence::FindEntry(
    SchoolDaysFlgH* node, const std::u16string& key) {
    if (node == nullptr) return nullptr;
    for (auto& entry : node->entries) {
        if (entry.key == key) return &entry;
    }
    return nullptr;
}

const SchoolDaysFlgHEntry* SchoolDaysPersistence::FindEntry(
    const SchoolDaysFlgH& node, const std::u16string& key) {
    for (const auto& entry : node.entries) {
        if (entry.key == key) return &entry;
    }
    return nullptr;
}

bool SchoolDaysPersistence::FindInteger(const SchoolDaysFlgH& node,
                                        const char* key, int64_t* value,
                                        Kotonoha_KtrfError* error) {
    if (key == nullptr || value == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days integer lookup argument is null");
    }
    const auto* entry = FindEntry(node, AsciiUtf16(key));
    if (entry == nullptr || (entry->type != 3 && entry->type != 11)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days save lacks required integer flag");
    }
    *value = entry->integer;
    return true;
}

bool SchoolDaysPersistence::UpsertInteger(SchoolDaysFlgH* node,
                                          const std::string& key,
                                          int32_t type, int32_t value,
                                          Kotonoha_KtrfError* error) {
    if (node == nullptr || (type != 3 && type != 11)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days integer upsert argument is invalid");
    }
    const std::u16string wide = AsciiUtf16(key);
    auto* entry = FindEntry(node, wide);
    if (entry == nullptr) {
        SchoolDaysFlgHEntry created;
        created.key = wide;
        created.identifier = -1;
        created.type = type;
        created.integer = value;
        const auto insert = std::lower_bound(
            node->entries.begin(), node->entries.end(), wide,
            [](const SchoolDaysFlgHEntry& current,
               const std::u16string& wanted) {
                return current.key < wanted;
            });
        node->entries.insert(insert, std::move(created));
        return true;
    }
    if (entry->type != type) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days persisted flag type changed unexpectedly");
    }
    entry->integer = value;
    return true;
}

bool SchoolDaysPersistence::UpsertGlobalString(
    const std::string& key, const std::u16string& value,
    Kotonoha_KtrfError* error) {
    ClearError(error);
    if (!hasGlobalFlag_ || key.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days global string metadata requires a loaded GlobalFlag");
    }

    const std::u16string wide = AsciiUtf16(key);
    auto* entry = FindEntry(&globalFlag_, wide);
    if (entry == nullptr) {
        SchoolDaysFlgHEntry created;
        created.key = wide;
        created.identifier = -1;
        created.type = 8;
        created.stringValue = value;
        const auto insert = std::lower_bound(
            globalFlag_.entries.begin(), globalFlag_.entries.end(), wide,
            [](const SchoolDaysFlgHEntry& current,
               const std::u16string& wanted) {
                return current.key < wanted;
            });
        globalFlag_.entries.insert(insert, std::move(created));
        return true;
    }
    if (entry->type != 8) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days global metadata key is not type 8");
    }
    entry->identifier = -1;
    entry->stringValue = value;
    return true;
}

bool SchoolDaysPersistence::WriteGlobalInteger(
    const std::string& key, int32_t type, int32_t value,
    KtrfStateStore* state, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (key.empty() || (type != 3 && type != 11)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days global integer type/key is invalid");
    }
    return UpsertGlobalInteger(key, type, value, state, error);
}

bool SchoolDaysPersistence::LoadGlobalFlag(const std::vector<uint8_t>& bytes,
                                           KtrfStateStore* state,
                                           Kotonoha_KtrfError* error) {
    ClearError(error);
    if (state == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days GlobalFlag state target is null");
    }
    SchoolDaysFlgH parsed;
    std::string codecError;
    if (!SchoolDaysPersistenceCodec::ParseFlgH(bytes, &parsed, &codecError)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT, codecError.c_str());
    }
    std::vector<std::pair<std::string, int64_t>> numeric;
    if (!NumericEntries(parsed, &numeric, error)) return false;

    KtrfStateStore::Snapshot before;
    if (!state->Capture(&before, error)) return false;

    // GlobalFlag.DAT is one persisted FlagsHash, not the complete KTRF global
    // namespace. In particular the authenticated route document has two
    // KTRF-only globals (one defaulting to 1) that are absent from the file.
    // Therefore replace only keys belonging to the previously/currently loaded
    // GlobalFlag document and leave unrelated KTRF globals at their defaults.
    std::unordered_set<std::string> incomingKeys;
    incomingKeys.reserve(numeric.size());
    for (const auto& item : numeric) incomingKeys.insert(item.first);

    if (hasGlobalFlag_) {
        std::vector<std::pair<std::string, int64_t>> previousNumeric;
        if (!NumericEntries(globalFlag_, &previousNumeric, error)) return false;
        for (const auto& item : previousNumeric) {
            if (incomingKeys.find(item.first) == incomingKeys.end() &&
                !state->WriteGlobalInt(item.first, 0, error)) {
                Kotonoha_KtrfError ignored{};
                (void)state->Restore(before, &ignored);
                return false;
            }
        }
    }
    for (const auto& item : numeric) {
        if (!state->WriteGlobalInt(item.first, item.second, error)) {
            Kotonoha_KtrfError ignored{};
            (void)state->Restore(before, &ignored);
            return false;
        }
    }

    if (error != nullptr && error->code != KOTONOHA_KTRF_OK) {
        Kotonoha_KtrfError ignored{};
        (void)state->Restore(before, &ignored);
        return false;
    }
    globalFlag_ = std::move(parsed);
    hasGlobalFlag_ = true;
    return true;
}

bool SchoolDaysPersistence::SerializeGlobalFlag(
    const KtrfStateStore& state, std::vector<uint8_t>* bytes,
    Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (!hasGlobalFlag_ || bytes == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days GlobalFlag document is not loaded");
    }
    std::unordered_map<std::string, int64_t> numeric;
    if (!state.EnumerateGlobalInts(&numeric, error)) return false;

    SchoolDaysFlgH merged = globalFlag_;
    for (auto& entry : merged.entries) {
        if (entry.type != 3 && entry.type != 11) continue;
        std::string key;
        if (!Utf16Ascii(entry.key, &key, error)) return false;
        const auto found = numeric.find(key);
        if (found == numeric.end()) continue;
        if (found->second < std::numeric_limits<int32_t>::min() ||
            found->second > std::numeric_limits<int32_t>::max()) {
            return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                        "School Days GlobalFlag integer exceeds writer range");
        }
        entry.integer = entry.type == 11
            ? (found->second != 0 ? -1 : 0)
            : static_cast<int32_t>(found->second);
    }

    std::string codecError;
    if (!SchoolDaysPersistenceCodec::SerializeFlgH(merged, bytes,
                                                    &codecError)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT, codecError.c_str());
    }
    return true;
}

bool SchoolDaysPersistence::ParseSaveImage(const std::vector<uint8_t>& bytes,
                                           SaveImage* image,
                                           Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (image == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days save image output is null");
    }
    SchoolDaysSLog parsed;
    std::string codecError;
    if (!SchoolDaysPersistenceCodec::ParseSLog(bytes, &parsed, &codecError)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT, codecError.c_str());
    }

    size_t scriptCount = 0u;
    size_t scriptIndex = 0u;
    std::unordered_map<std::string, int64_t> mergedSession;
    std::vector<std::string> mergedOrder;
    for (size_t i = 0u; i < parsed.records.size(); ++i) {
        if (parsed.records[i].tag == 1) {
            if (parsed.records[i].floatBits != UINT32_C(0x3f800000)) {
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days save route version does not match v1.02 runtime");
            }
            std::vector<std::pair<std::string, int64_t>> recordNumeric;
            if (!NumericEntries(parsed.records[i].flags, &recordNumeric, error)) {
                return false;
            }
            for (const auto& item : recordNumeric) {
                if (mergedSession.find(item.first) == mergedSession.end()) {
                    mergedOrder.push_back(item.first);
                }
                mergedSession[item.first] = item.second;
            }
            ++scriptCount;
            scriptIndex = i;
        }
    }
    if (scriptCount == 0u) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days save contains no tag-1 script record");
    }

    SaveImage planned;
    planned.document = std::move(parsed);
    planned.scriptRecordIndex = scriptIndex;
    auto& script = planned.document.records[scriptIndex];

    // tag4 is a historical SceneKey -> choice oracle used by PlayData.  It does
    // not identify whether the current visit to that SceneKey has already
    // committed.  Kotonoha-created saves therefore carry one private bit in the
    // current tag1 FlgH.  Consume it here and strip it from every tag1 before
    // rebuilding/restoring native local state, so it never becomes a live script
    // variable or persists through the in-memory School Days manager.
    const std::u16string liveChoiceMarker = AsciiUtf16(kLiveChoiceCommitMarker);
    for (size_t i = 0u; i < planned.document.records.size(); ++i) {
        auto& record = planned.document.records[i];
        if (record.tag != 1) continue;
        auto marker = std::find_if(
            record.flags.entries.begin(), record.flags.entries.end(),
            [&](const SchoolDaysFlgHEntry& entry) {
                return entry.key == liveChoiceMarker;
            });
        if (marker == record.flags.entries.end()) continue;
        if (i == scriptIndex) {
            if (marker->type != 3 ||
                (marker->integer != 0 && marker->integer != 1)) {
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days live-choice save marker is invalid");
            }
            planned.liveChoiceCommitMarkerPresent = true;
            planned.liveChoiceCommitted = marker->integer != 0;
        }
        record.flags.entries.erase(marker);
    }
    mergedSession.erase(kLiveChoiceCommitMarker);
    mergedOrder.erase(
        std::remove(mergedOrder.begin(), mergedOrder.end(),
                    std::string(kLiveChoiceCommitMarker)),
        mergedOrder.end());

    if (!Utf16Ascii(script.name, &planned.sceneKey, error) ||
        !NumericEntries(script.flags, &planned.sessionInts, error)) {
        return false;
    }
    planned.sessionInts.clear();
    planned.sessionInts.reserve(mergedOrder.size());
    for (const auto& key : mergedOrder) {
        planned.sessionInts.emplace_back(key, mergedSession[key]);
    }
    int64_t route = 0;
    int64_t scene = 0;
    if (!FindInteger(script.flags, "ROUTE", &route, error) ||
        !FindInteger(script.flags, "SCENE", &scene, error)) {
        return false;
    }
    if (route < std::numeric_limits<int>::min() ||
        route > std::numeric_limits<int>::max() ||
        scene < std::numeric_limits<int>::min() ||
        scene > std::numeric_limits<int>::max()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days save coordinate exceeds runtime range");
    }
    planned.route = static_cast<int>(route);
    planned.scene = static_cast<int>(scene);
    *image = std::move(planned);
    return true;
}

void SchoolDaysPersistence::CommitSaveImage(const SaveImage& image) {
    saveLog_ = image.document;
    hasSaveLog_ = true;
    RebuildLocalMetadataFromSave();
}

void SchoolDaysPersistence::RebuildLocalMetadataFromSave() {
    localMetadata_.clear();
    for (const auto& record : saveLog_.records) {
        if (record.tag != 1) continue;
        for (const auto& entry : record.flags.entries) {
            std::string key;
            key.reserve(entry.key.size());
            bool ascii = true;
            for (char16_t unit : entry.key) {
                if (unit > 0x7fu) {
                    ascii = false;
                    break;
                }
                key.push_back(static_cast<char>(unit));
            }
            if (ascii) localMetadata_[key] = entry;
        }
    }
}

void SchoolDaysPersistence::ResetNewGame() {
    saveLog_.records.clear();
    localMetadata_.clear();
    hasSaveLog_ = true;

    // SaveFile0030's first authenticated callback_00 snapshot at
    // 00/00-00-A00 contains exactly these seven fields.  They are the local
    // FlgH baseline before SP100 is created by that callback.
    static constexpr const char* kInitialLocal[] = {
        "000", "001", "002", "003", "004", "ROUTE", "SCENE"
    };
    for (const char* key : kInitialLocal) {
        SchoolDaysFlgHEntry entry;
        entry.key = AsciiUtf16(key);
        entry.identifier = -1;
        entry.type = 3;
        entry.integer = 0;
        localMetadata_[key] = std::move(entry);
    }
}

void SchoolDaysPersistence::ResetScriptManagerForFrontendReturn() {
    // EXE 0x432870 always clears the active script record (+0x20).  With the
    // RouteMap singleton's RouteLoad latch at its normal value 0 it also clears
    // checkpoint (+0x38/tag3) and recorded-choice (+0x58/tag4) collections.
    // In the ordinary MenuBar -> MainMenu path RouteLoad is 0: the singleton
    // initializes +0x4E8 to 0 and only a RouteMap node activation writes 1.
    // Keep localMetadata_ untouched because Script.Free does not clear the
    // host's live local FlagsHash; keep GlobalFlag wholly untouched as well.
    saveLog_.records.clear();
    hasSaveLog_ = true;
}

bool SchoolDaysPersistence::ValidateEffectOracle(
    const Kotonoha_KtrfDocument* document, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (document == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days persistence oracle document is null");
    }
    const Kotonoha_KtrfSection* effects =
        Kotonoha_KtrfFindSection(document, "EFFT");
    if (effects == nullptr ||
        effects->item_count != kSchoolDaysPersistenceEffectCount ||
        kSchoolDaysPersistenceEffectTypeCount != 11123u ||
        kSchoolDaysPersistenceEffectType3Count != 11014u ||
        kSchoolDaysPersistenceEffectType11Count != 109u ||
        kSchoolDaysPersistenceNoPersistenceEffectCount != 233u) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days persistence effect oracle count drift");
    }

    // The binary compiler canonicalizes EFFT by stable effect id, so the JSON
    // array index is not a runtime EFFT index.  Build a document-local map from
    // each binary effect id to the authenticated source/type oracle instead of
    // relying on positional coincidence.
    runtimeEffectTypes_.assign(effects->item_count, RuntimeEffectType{});
    uint32_t sourceEffects = 0u;
    uint32_t feelingEffects = 0u;
    uint32_t postTransitionEffects = 0u;
    uint32_t profileEffects = 0u;
    uint32_t persistent = 0u;
    uint32_t type3 = 0u;
    uint32_t type11 = 0u;
    uint32_t globalWrites = 0u;
    uint32_t noPersistence = 0u;
    bool saw999 = false;
    bool saw998 = false;
    bool sawChoiceReset = false;

    for (uint32_t binaryIndex = 0u; binaryIndex < effects->item_count;
         ++binaryIndex) {
        Kotonoha_KtrfEffect effect{};
        if (!Kotonoha_KtrfGetEffect(document, binaryIndex, &effect, error))
            return false;

        const char* rawId = nullptr;
        size_t rawIdSize = 0u;
        if (!Kotonoha_KtrfGetString(document, effect.id_str, &rawId,
                                    &rawIdSize, error)) {
            return false;
        }
        const std::string effectId(rawId != nullptr ? rawId : "", rawIdSize);

        const char* expectedTarget = nullptr;
        uint8_t expectedType = 0u;
        bool expectedGlobal = false;
        uint32_t sourceIndex = 0u;
        if (ParseUnsignedSuffix(effectId, "sdhq:effect:source:", &sourceIndex)) {
            ++sourceEffects;
            const auto* expected = FindPersistenceEffect(sourceIndex);
            if (expected != nullptr &&
                expected->source_effect_index == static_cast<int>(sourceIndex)) {
                expectedTarget = expected->target;
                expectedType = expected->type;
            }
            else if (const auto* globalExpected =
                         FindGlobalPersistenceEffect(sourceIndex)) {
                expectedTarget = globalExpected->target;
                expectedType = globalExpected->type;
                expectedGlobal = true;
                ++globalWrites;
            }
        }
        else if (StartsWith(effectId, "sdhq:effect:feeling:")) {
            ++feelingEffects;
            expectedType = 3u;
        }
        else if (StartsWith(effectId, "sdhq:effect:post-transition:")) {
            ++postTransitionEffects;
            expectedType = 3u;
        }
        else if (StartsWith(effectId, "sdhq:effect:profile:")) {
            ++profileEffects;
            if (effectId == "sdhq:effect:profile:reset-choice-result") {
                sawChoiceReset = true;
            }
            else {
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days persistence oracle saw unknown profile effect");
            }
        }
        else {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days persistence oracle saw unknown effect id family");
        }

        if (expectedType == 0u) {
            ++noPersistence;
            continue;
        }
        if (effect.shape != KOTONOHA_KTRF_EFFECT_SET &&
            effect.shape != KOTONOHA_KTRF_EFFECT_ADD &&
            effect.shape != KOTONOHA_KTRF_EFFECT_COPY) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days persistence oracle maps a non-write effect");
        }

        Kotonoha_KtrfVariable variable{};
        if (!Kotonoha_KtrfGetVariable(document, effect.payload_a,
                                      &variable, error)) {
            return false;
        }
        const char* rawVariableId = nullptr;
        size_t rawVariableIdSize = 0u;
        if (!Kotonoha_KtrfGetString(document, variable.id_str,
                                    &rawVariableId, &rawVariableIdSize,
                                    error)) {
            return false;
        }
        static constexpr const char kSessionPrefix[] = "sdhq:var:session:";
        static constexpr const char kGlobalPrefix[] = "sdhq:var:global:";
        const char* variablePrefix = expectedGlobal ? kGlobalPrefix : kSessionPrefix;
        const size_t variablePrefixSize = std::strlen(variablePrefix);
        if (rawVariableId == nullptr || rawVariableIdSize <= variablePrefixSize ||
            std::memcmp(rawVariableId, variablePrefix, variablePrefixSize) != 0) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        expectedGlobal
                            ? "School Days global persistence effect target drift"
                            : "School Days persistent effect does not target session state");
        }
        const std::string target(rawVariableId + variablePrefixSize,
                                 rawVariableIdSize - variablePrefixSize);

        if (expectedTarget != nullptr && target != expectedTarget) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days source persistence target drift");
        }
        if (StartsWith(effectId, "sdhq:effect:feeling:") &&
            target != "000" && target != "001" && target != "002" &&
            target != "003" && target != "004") {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days feeling persistence target drift");
        }
        if (StartsWith(effectId, "sdhq:effect:post-transition:") &&
            target != "ROUTE" && target != "SCENE") {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days post-transition persistence target drift");
        }

        runtimeEffectTypes_[binaryIndex].target = target;
        runtimeEffectTypes_[binaryIndex].type = expectedType;
        runtimeEffectTypes_[binaryIndex].global = expectedGlobal;
        if (!expectedGlobal) {
            ++persistent;
            if (expectedType == 3u) ++type3;
            else if (expectedType == 11u) ++type11;
        }

        if (sourceIndex == 197u) {
            saw999 = target == "999" && expectedType == 11u;
        }
        if (sourceIndex == 201u) {
            saw998 = target == "998" && expectedType == 11u;
        }
    }

    if (sourceEffects != 6183u || feelingEffects != 302u ||
        postTransitionEffects != 4870u || profileEffects != 1u ||
        persistent != kSchoolDaysPersistenceEffectTypeCount ||
        type3 != kSchoolDaysPersistenceEffectType3Count ||
        type11 != kSchoolDaysPersistenceEffectType11Count ||
        globalWrites != kSchoolDaysGlobalPersistenceEffectCount ||
        noPersistence + globalWrites !=
            kSchoolDaysPersistenceNoPersistenceEffectCount ||
        !saw999 || !saw998 || !sawChoiceReset) {
        runtimeEffectTypes_.clear();
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days persistence runtime effect map drift");
    }
    return true;
}

bool SchoolDaysPersistence::MarkLocalInteger(const std::string& key,
                                             int32_t type,
                                             Kotonoha_KtrfError* error) {
    if (key.empty() || (type != 3 && type != 11)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days local persistence type/key is invalid");
    }
    auto found = localMetadata_.find(key);
    if (found == localMetadata_.end()) {
        SchoolDaysFlgHEntry entry;
        entry.key = AsciiUtf16(key);
        entry.identifier = -1;
        entry.type = type;
        entry.integer = 0;
        localMetadata_.emplace(key, std::move(entry));
    }
    else {
        // Native FlagsHash assignment replaces the VARIANT payload/type.
        found->second.type = type;
        found->second.integer = 0;
        found->second.floatBits = 0u;
        found->second.stringValue.clear();
    }
    return true;
}

bool SchoolDaysPersistence::ObserveRoutingEffect(
    uint32_t effectIndex, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (effectIndex >= runtimeEffectTypes_.size()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days observed effect is outside validated EFFT map");
    }
    const auto& entry = runtimeEffectTypes_[effectIndex];
    if (entry.type == 0u) return true;
    if (entry.global) {
        if (!hasGlobalFlag_) {
            globalFlag_.entries.clear();
            hasGlobalFlag_ = true;
        }
        // The pinned global source oracle contains only SetGlobalConst(...,1)
        // REP* writes.  Native FlagsHash stores VT_BOOL true as -1/type11.
        return UpsertInteger(&globalFlag_, entry.target, entry.type, -1, error);
    }
    return MarkLocalInteger(entry.target, entry.type, error);
}

bool SchoolDaysPersistence::RoutingEffectTargetsTopMeter(
    uint32_t effectIndex) const {
    if (effectIndex >= runtimeEffectTypes_.size()) return false;
    const auto& entry = runtimeEffectTypes_[effectIndex];
    return !entry.global && entry.type != 0u &&
           (entry.target == "001" || entry.target == "002");
}

bool SchoolDaysPersistence::CaptureLocalFlgH(
    const KtrfStateStore& state, SchoolDaysFlgH* out,
    Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (out == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days local FlgH output is null");
    }
    out->entries.clear();
    out->entries.reserve(localMetadata_.size());
    for (const auto& item : localMetadata_) {
        SchoolDaysFlgHEntry entry = item.second;
        if (entry.type == 3 || entry.type == 11) {
            int64_t value = 0;
            if (!state.ReadSessionInt(item.first, &value, error)) return false;
            if (entry.type == 11) {
                entry.integer = value != 0 ? -1 : 0;
            }
            else {
                if (value < std::numeric_limits<int32_t>::min() ||
                    value > std::numeric_limits<int32_t>::max()) {
                    return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                                "School Days local FlgH integer exceeds writer range");
                }
                entry.integer = static_cast<int32_t>(value);
            }
        }
        out->entries.push_back(std::move(entry));
    }
    return true;
}

bool SchoolDaysPersistence::UpsertGlobalInteger(
    const std::string& key, int32_t type, int32_t value,
    KtrfStateStore* state, Kotonoha_KtrfError* error) {
    if (state == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days global persistence state is null");
    }
    if (!hasGlobalFlag_) {
        globalFlag_.entries.clear();
        hasGlobalFlag_ = true;
    }
    if (!UpsertInteger(&globalFlag_, key, type, value, error)) return false;
    return state->WriteGlobalInt(key, value, error);
}

bool SchoolDaysPersistence::RecordCheckpoint(
    int route, int scene, const std::string& sceneKey,
    KtrfStateStore* state, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (state == nullptr || sceneKey.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days checkpoint argument is invalid");
    }
    const int sp = SchoolDaysBackRules::SpForScene(route, scene);
    if (sp <= 0) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days callback_00 source has no authenticated SP mapping");
    }

    char spBuffer[32];
    std::snprintf(spBuffer, sizeof(spBuffer), "SP%03d", sp);
    const std::string spKey = spBuffer;

    KtrfStateStore::Snapshot stateBefore;
    if (!state->Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = *this;

    SchoolDaysFlgH snapshot;
    if (!CaptureLocalFlgH(*state, &snapshot, error)) return false;

    size_t checkpointCount = 0u;
    for (const auto& record : saveLog_.records) {
        if (record.tag != 3) continue;
        ++checkpointCount;
        if (record.secondary == AsciiUtf16(spKey)) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days duplicate checkpoint SP key is unsupported");
        }
    }
    if (checkpointCount > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days checkpoint count exceeds writer range");
    }

    SchoolDaysSLogRecord checkpoint;
    checkpoint.tag = 3;
    checkpoint.name = AsciiUtf16(sceneKey);
    checkpoint.secondary = AsciiUtf16(spKey);
    checkpoint.integer = static_cast<int32_t>(checkpointCount);
    checkpoint.flags = std::move(snapshot);

    const std::u16string secondaryKey = AsciiUtf16(spKey);
    auto insert = std::find_if(
        saveLog_.records.begin(), saveLog_.records.end(),
        [&](const SchoolDaysSLogRecord& record) {
            if (record.tag == 4) return true;
            return record.tag == 3 && record.secondary > secondaryKey;
        });
    saveLog_.records.insert(insert, std::move(checkpoint));
    hasSaveLog_ = true;

    if (!MarkLocalInteger(spKey, 11, error) ||
        !state->WriteSessionInt(spKey, -1, error) ||
        !UpsertGlobalInteger(spKey, 11, -1, state, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state->Restore(stateBefore, &ignored);
        *this = persistenceBefore;
        return false;
    }
    return true;
}

bool SchoolDaysPersistence::RecordChoice(const std::string& sceneKey,
                                         int64_t value,
                                         Kotonoha_KtrfError* error) {
    ClearError(error);
    if (sceneKey.empty() || value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days choice persistence argument is invalid");
    }
    const std::u16string key = AsciiUtf16(sceneKey);
    for (auto& record : saveLog_.records) {
        if (record.tag == 4 && record.name == key) {
            record.integer = static_cast<int32_t>(value);
            return true;
        }
    }
    SchoolDaysSLogRecord record;
    record.tag = 4;
    record.name = key;
    record.integer = static_cast<int32_t>(value);
    saveLog_.records.push_back(std::move(record));
    hasSaveLog_ = true;
    return true;
}

bool SchoolDaysPersistence::ReadRecordedChoice(
    const std::string& sceneKey, int32_t* value, bool* present,
    Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (sceneKey.empty() || value == nullptr || present == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days recorded choice lookup argument is invalid");
    }
    *value = -1;
    *present = false;
    if (!hasSaveLog_) return true;
    const std::u16string key = AsciiUtf16(sceneKey);
    for (const auto& record : saveLog_.records) {
        if (record.tag == 4 && record.name == key) {
            *value = record.integer;
            *present = true;
            return true;
        }
    }
    return true;
}

bool SchoolDaysPersistence::RestoreBackCheckpoint(
    const SchoolDaysCoordinate& destination, KtrfStateStore* state,
    bool* restored, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (state == nullptr || restored == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Back checkpoint argument is invalid");
    }
    *restored = false;
    if (!hasSaveLog_) return true;

    const int sp = SchoolDaysBackRules::SpForScene(destination.route,
                                                   destination.scene);
    if (sp <= 0) return true;

    char spBuffer[32];
    std::snprintf(spBuffer, sizeof(spBuffer), "SP%03d", sp);
    const std::u16string selectedKey = AsciiUtf16(spBuffer);

    auto selected = std::find_if(
        saveLog_.records.begin(), saveLog_.records.end(),
        [&](const SchoolDaysSLogRecord& record) {
            return record.tag == 3 && record.secondary == selectedKey;
        });
    if (selected == saveLog_.records.end()) return true;

    const std::u16string expectedScene = AsciiUtf16(destination.sceneKey);
    if (selected->name != expectedScene) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Back checkpoint SP resolves to a different SceneKey");
    }

    KtrfStateStore::Snapshot stateBefore;
    if (!state->Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = *this;

    for (const SchoolDaysFlgHEntry& entry : selected->flags.entries) {
        std::string key;
        key.reserve(entry.key.size());
        for (char16_t unit : entry.key) {
            if (unit > 0x7fu) {
                Kotonoha_KtrfError ignored{};
                (void)state->Restore(stateBefore, &ignored);
                *this = persistenceBefore;
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days Back checkpoint has non-ASCII state key");
            }
            key.push_back(static_cast<char>(unit));
        }

        // 0x00428A20 applies the checkpoint FlgH onto the existing local
        // FlagsHash; it does not perform the SaveFile-load clear.  Preserve
        // existing sparse members and upsert only entries carried by snapshot.
        localMetadata_[key] = entry;
        if (entry.type == 3 || entry.type == 11) {
            if (!state->WriteSessionInt(key, entry.integer, error)) {
                Kotonoha_KtrfError ignored{};
                (void)state->Restore(stateBefore, &ignored);
                *this = persistenceBefore;
                return false;
            }
        }
    }

    // manager+0x38 is an ordered associative collection keyed by secondary
    // SP%03d.  Native 0x004331A0 erases [found,end) inclusively after the
    // selected checkpoint successfully applies/launches.  Use key ordering,
    // not vector creation order, so out-of-order fixtures match that contract.
    saveLog_.records.erase(
        std::remove_if(saveLog_.records.begin(), saveLog_.records.end(),
                       [&](const SchoolDaysSLogRecord& record) {
                           return record.tag == 3 &&
                                  record.secondary >= selectedKey;
                       }),
        saveLog_.records.end());
    *restored = true;
    return true;
}

bool SchoolDaysPersistence::ReplaceCurrentScriptSnapshot(
    const std::string& sceneKey, uint32_t versionBits,
    const KtrfStateStore& state, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (sceneKey.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days script snapshot SceneKey is empty");
    }
    SchoolDaysFlgH flags;
    if (!CaptureLocalFlgH(state, &flags, error)) return false;

    saveLog_.records.erase(
        std::remove_if(saveLog_.records.begin(), saveLog_.records.end(),
                       [](const SchoolDaysSLogRecord& record) {
                           return record.tag == 1;
                       }),
        saveLog_.records.end());
    SchoolDaysSLogRecord script;
    script.tag = 1;
    script.name = AsciiUtf16(sceneKey);
    script.floatBits = versionBits;
    script.flags = std::move(flags);
    saveLog_.records.insert(saveLog_.records.begin(), std::move(script));
    hasSaveLog_ = true;
    return true;
}

bool SchoolDaysPersistence::RefreshCurrentScriptSnapshot(
    const KtrfStateStore& state, Kotonoha_KtrfError* error) {
    ClearError(error);
    const SchoolDaysSLogRecord* current = nullptr;
    for (const auto& record : saveLog_.records) {
        if (record.tag != 1) continue;
        if (current != nullptr) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days SLog contains multiple current script snapshots");
        }
        current = &record;
    }
    if (current == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days SLog has no current script snapshot");
    }

    std::string sceneKey;
    if (!Utf16Ascii(current->name, &sceneKey, error)) return false;
    const uint32_t versionBits = current->floatBits;
    return ReplaceCurrentScriptSnapshot(sceneKey, versionBits, state, error);
}

bool SchoolDaysPersistence::SerializeSaveLog(
    std::vector<uint8_t>* bytes, Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (!hasSaveLog_ || bytes == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days SLog document is not loaded");
    }
    std::string codecError;
    if (!SchoolDaysPersistenceCodec::SerializeSLog(saveLog_, bytes,
                                                   &codecError)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT, codecError.c_str());
    }
    return true;
}

bool SchoolDaysPersistence::SerializeSaveLogWithLiveChoiceCommitMarker(
    bool committed, std::vector<uint8_t>* bytes,
    Kotonoha_KtrfError* error) const {
    ClearError(error);
    if (!hasSaveLog_ || bytes == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days SLog document is not loaded");
    }

    SchoolDaysSLog marked = saveLog_;
    SchoolDaysSLogRecord* current = nullptr;
    for (auto& record : marked.records) {
        if (record.tag != 1) continue;
        if (current != nullptr) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days SLog contains multiple current script snapshots");
        }
        current = &record;
    }
    if (current == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days SLog has no current script snapshot");
    }
    if (!UpsertInteger(&current->flags, kLiveChoiceCommitMarker, 3,
                       committed ? 1 : 0, error)) {
        return false;
    }

    std::string codecError;
    if (!SchoolDaysPersistenceCodec::SerializeSLog(marked, bytes,
                                                   &codecError)) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT, codecError.c_str());
    }
    return true;
}

bool SchoolDaysPersistence::RegisterEnding(int endingCode,
                                           const std::string& sceneKey,
                                           KtrfStateStore* state,
                                           Kotonoha_KtrfError* error) {
    ClearError(error);
    if (state == nullptr || sceneKey.empty() || endingCode < 0 || endingCode >= 22) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days ending code is out of range");
    }

    KtrfStateStore::Snapshot stateSnapshot;
    if (!state->Capture(&stateSnapshot, error)) return false;
    const SchoolDaysPersistence persistenceBefore = *this;

    char endingKey[32];
    std::snprintf(endingKey, sizeof(endingKey), "[End%02d]=\"", endingCode);
    if (!UpsertGlobalInteger(endingKey, 11, -1, state, error) ||
        !UpsertGlobalInteger("EndClear", 11, -1, state, error) ||
        !UpsertGlobalInteger("EndNo", 3, endingCode, state, error) ||
        !MarkLocalInteger("EndClear", 11, error) ||
        !state->WriteSessionInt("EndClear", -1, error) ||
        !ReplaceCurrentScriptSnapshot(sceneKey, UINT32_C(0x3f800000),
                                      *state, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state->Restore(stateSnapshot, &ignored);
        *this = persistenceBefore;
        return false;
    }
    return true;
}

} // namespace Kotonoha
