#include <Kotonoha/routing/SchoolDaysKtrfProfile.hpp>

extern "C" {
#include <Kotonoha/parsers/KtrfTyped.h>
}

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Kotonoha {
namespace {

constexpr const char* kProfileId = "overflow.school-days-hq";
constexpr const char* kProfileVersion = "1.0.0";
constexpr const char* kSceneScheme = "overflow.sdhq:scene-key";
constexpr const char* kSceneMediaType = "application/x-overflow-ors";
constexpr const char* kHookContract = "overflow.sdhq.callback/1.0.0";
constexpr const char* kHookSymbolPrefix = "overflow.sdhq:";
constexpr const char* kNewGameEntry = "sdhq:entry:new-game";
constexpr const char* kAssetExtension = ".ENG.ORS";
constexpr const char* kSessionPrefix = "sdhq:var:session:";
constexpr const char* kGlobalPrefix = "sdhq:var:global:";
constexpr const char* kInternalPrefix = "sdhq:var:internal:";
constexpr const char* kRouteSymbol = "ROUTE";
constexpr const char* kSceneSymbol = "SCENE";
constexpr const char* kChoiceResultSymbol = "choice_result";

constexpr uint32_t kExpectedNodes = 1857u;
constexpr uint32_t kExpectedResources = 1855u;
constexpr uint32_t kExpectedChoices = 287u;
constexpr uint32_t kExpectedHooks = 3u;
constexpr uint32_t kExpectedEndings = 22u;
constexpr uint32_t kExpectedDispatchers = 2u;
constexpr int64_t kPendingChoice = -2;
constexpr uint32_t kMaxDispatchHops = 64u;

int Fail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
         const char* format, ...) {
    if (error != nullptr) {
        va_list args;
        error->code = code;
        va_start(args, format);
#if defined(_MSC_VER)
        _vsnprintf_s(error->message, sizeof(error->message), _TRUNCATE, format,
                     args);
#else
        std::vsnprintf(error->message, sizeof(error->message), format, args);
#endif
        va_end(args);
        error->message[sizeof(error->message) - 1u] = '\0';
    }
    return 0;
}

uint32_t Count(const Kotonoha_KtrfDocument* document, const char type[4]) {
    const Kotonoha_KtrfSection* section = Kotonoha_KtrfFindSection(document, type);
    return section != nullptr ? section->item_count : 0u;
}

int StringEquals(const Kotonoha_KtrfDocument* document, uint32_t stringIndex,
                 const char* expected, Kotonoha_KtrfError* error) {
    const char* data = nullptr;
    size_t size = 0u;
    const size_t expectedSize = std::strlen(expected);
    if (!Kotonoha_KtrfGetString(document, stringIndex, &data, &size, error))
        return -1;
    return size == expectedSize &&
                   (size == 0u || std::memcmp(data, expected, size) == 0)
               ? 1
               : 0;
}

int StringStartsWith(const Kotonoha_KtrfDocument* document, uint32_t stringIndex,
                     const char* prefix, Kotonoha_KtrfError* error) {
    const char* data = nullptr;
    size_t size = 0u;
    const size_t prefixSize = std::strlen(prefix);
    if (!Kotonoha_KtrfGetString(document, stringIndex, &data, &size, error))
        return -1;
    return size >= prefixSize &&
                   (prefixSize == 0u || std::memcmp(data, prefix, prefixSize) == 0)
               ? 1
               : 0;
}

int FindEntry(const Kotonoha_KtrfDocument* document, const char* id,
              uint32_t* entryIndex, Kotonoha_KtrfError* error) {
    const uint32_t count = Count(document, "ENTR");
    for (uint32_t i = 0u; i < count; ++i) {
        Kotonoha_KtrfEntryPoint entry{};
        if (!Kotonoha_KtrfGetEntryPoint(document, i, &entry, error)) return 0;
        const int equal = StringEquals(document, entry.id_str, id, error);
        if (equal < 0) return 0;
        if (equal != 0) {
            *entryIndex = i;
            return 1;
        }
    }
    return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                "KTRF entry point '%s' is missing", id);
}

int ValidateDocument(const Kotonoha_KtrfDocument* document,
                     uint32_t* newGameEntry, Kotonoha_KtrfError* error) {
    if (document == nullptr || newGameEntry == nullptr)
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days KTRF profile validation argument is null");
    if (!Kotonoha_KtrfValidateTypedCore(document, error)) return 0;

    Kotonoha_KtrfMeta meta{};
    if (!Kotonoha_KtrfGetMeta(document, &meta, error)) return 0;
    int equal = StringEquals(document, meta.profile_id_str, kProfileId, error);
    if (equal <= 0)
        return equal < 0
                   ? 0
                   : Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "KTRF profile is not %s", kProfileId);
    equal = StringEquals(document, meta.profile_version_str, kProfileVersion, error);
    if (equal <= 0)
        return equal < 0
                   ? 0
                   : Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "unsupported KTRF profile version");

    if (Count(document, "NODE") != kExpectedNodes ||
        Count(document, "RSRC") != kExpectedResources ||
        Count(document, "CHOI") != kExpectedChoices ||
        Count(document, "HOOK") != kExpectedHooks ||
        Count(document, "ENDG") != kExpectedEndings) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "KTRF profile inventory mismatch");
    }
    if (!FindEntry(document, kNewGameEntry, newGameEntry, error)) return 0;

    uint32_t sceneNodes = 0u;
    uint32_t dispatcherNodes = 0u;
    for (uint32_t i = 0u; i < kExpectedNodes; ++i) {
        Kotonoha_KtrfNode node{};
        if (!Kotonoha_KtrfGetNode(document, i, &node, error)) return 0;
        const int isScene = StringEquals(document, node.kind_str, "ktrf:scene", error);
        if (isScene < 0) return 0;
        const int isDispatcher =
            StringEquals(document, node.kind_str, "ktrf:dispatcher", error);
        if (isDispatcher < 0) return 0;

        if (isScene != 0) {
            if (node.resource_count != 1u)
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "KTRF scene node %u has %u resources", i,
                            node.resource_count);
            uint32_t resourceIndex = KOTONOHA_KTRF_NULL_INDEX;
            Kotonoha_KtrfResource resource{};
            if (!Kotonoha_KtrfGetNodeResource(document, i, 0u, &resourceIndex, error) ||
                !Kotonoha_KtrfGetResource(document, resourceIndex, &resource, error))
                return 0;
            const int schemeOk =
                StringEquals(document, resource.scheme_str, kSceneScheme, error);
            if (schemeOk < 0) return 0;
            const int mediaOk =
                StringEquals(document, resource.media_type_str, kSceneMediaType, error);
            if (mediaOk < 0) return 0;
            if (schemeOk == 0 || mediaOk == 0 ||
                (resource.flags & (KOTONOHA_KTRF_RSRC_REQUIRED |
                                   KOTONOHA_KTRF_RSRC_HAS_REQUIRED)) !=
                    (KOTONOHA_KTRF_RSRC_REQUIRED |
                     KOTONOHA_KTRF_RSRC_HAS_REQUIRED)) {
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "KTRF scene resource %u is malformed", resourceIndex);
            }
            ++sceneNodes;
        } else if (isDispatcher != 0) {
            if (node.resource_count != 0u)
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "KTRF dispatcher node %u owns resources", i);
            ++dispatcherNodes;
        } else {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF node %u has unsupported kind", i);
        }
    }
    if (sceneNodes != kExpectedResources || dispatcherNodes != kExpectedDispatchers)
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "KTRF scene/dispatcher inventory mismatch");

    for (uint32_t i = 0u; i < kExpectedHooks; ++i) {
        Kotonoha_KtrfHook hook{};
        if (!Kotonoha_KtrfGetHook(document, i, &hook, error)) return 0;
        const int symbolOk =
            StringStartsWith(document, hook.symbol_str, kHookSymbolPrefix, error);
        if (symbolOk < 0) return 0;
        const int contractOk =
            StringEquals(document, hook.contract_str, kHookContract, error);
        if (contractOk < 0) return 0;
        if (symbolOk == 0 || contractOk == 0)
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF hook %u is malformed", i);
    }

    uint32_t endingMask = 0u;
    for (uint32_t i = 0u; i < kExpectedEndings; ++i) {
        Kotonoha_KtrfEnding ending{};
        if (!Kotonoha_KtrfGetEnding(document, i, &ending, error)) return 0;
        if (!ending.has_code || ending.code.kind != KOTONOHA_KTRF_SCALAR_INT64)
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF ending %u lacks int64 code", i);
        const int64_t code = ending.code.as.int64_value;
        if (code < 0 || code >= static_cast<int64_t>(kExpectedEndings))
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF ending code out of range: %lld",
                        static_cast<long long>(code));
        const uint32_t bit = UINT32_C(1) << static_cast<uint32_t>(code);
        if ((endingMask & bit) != 0u)
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "duplicate KTRF ending code: %lld",
                        static_cast<long long>(code));
        endingMask |= bit;
    }
    if (endingMask != ((UINT32_C(1) << kExpectedEndings) - 1u))
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "KTRF ending catalog is incomplete");
    return 1;
}

int ValidateSceneKey(const char* sceneKey, size_t length,
                     Kotonoha_KtrfError* error) {
    if (sceneKey == nullptr || length < 7u)
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "KTRF scene-key is empty or too short");
    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
    const auto name = [](char value) {
        return (value >= '0' && value <= '9') ||
               (value >= 'A' && value <= 'Z') ||
               (value >= 'a' && value <= 'z') || value == '-';
    };
    if (!digit(sceneKey[0]) || !digit(sceneKey[1]) || sceneKey[2] != '/' ||
        sceneKey[3] != sceneKey[0] || sceneKey[4] != sceneKey[1] ||
        sceneKey[5] != '-') {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "invalid KTRF scene-key layout");
    }
    for (size_t i = 3u; i < length; ++i) {
        if (!name(sceneKey[i]))
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF scene-key contains invalid character");
    }
    return 1;
}

} // namespace

const Kotonoha_KtrfProfileConfig& SchoolDaysKtrfProfile() {
    static const Kotonoha_KtrfProfileConfig profile = {
        &ValidateDocument,
        kMaxDispatchHops,
    };
    return profile;
}

const SchoolDaysKtrfRuntimeConfig& SchoolDaysKtrfRuntimeProfile() {
    static const SchoolDaysKtrfRuntimeConfig profile = {
        &ValidateSceneKey,
        kAssetExtension,
        kSessionPrefix,
        kGlobalPrefix,
        kInternalPrefix,
        kRouteSymbol,
        kSceneSymbol,
        kChoiceResultSymbol,
        kPendingChoice,
    };
    return profile;
}

} // namespace Kotonoha
