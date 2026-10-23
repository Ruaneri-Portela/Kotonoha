#include <Kotonoha/routing/SchoolDaysSaveService.hpp>

#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>

#include <cstdio>

namespace Kotonoha {
namespace {

std::u16string AsciiToU16(const std::string& value) {
    std::u16string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        out.push_back(static_cast<char16_t>(ch));
    }
    return out;
}

const SchoolDaysFlgHEntry* FindEntry(const SchoolDaysFlgH& node,
                                     const std::u16string& key) {
    for (const auto& entry : node.entries) {
        if (entry.key == key) return &entry;
    }
    return nullptr;
}

bool ReadFail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
              const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message
                                         : "School Days SaveLoad metadata error");
    }
    return false;
}

} // namespace

bool SchoolDaysSaveService::ReadSlotMetadata(
    const SchoolDaysSaveProfile& profile, int slot, bool fileOpenable,
    const SchoolDaysFlgH* globalFlag, SlotMetadata* out,
    Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (out == nullptr || slot < 0 || slot >= 100) {
        return ReadFail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "School Days SaveLoad metadata slot must be in range 0..99");
    }
    *out = {};

    // 0x0042A980 returns 0 immediately when the formatted SaveFile cannot be
    // opened. The GlobalFlag lookup is not consulted in that case.
    if (!fileOpenable || globalFlag == nullptr) return true;

    std::string key;
    std::string profileError;
    if (!SchoolDaysSaveProfile::FormatIndexed(profile.saveConfig, slot, &key,
                                               &profileError)) {
        return ReadFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        profileError.empty()
                            ? "School Days SaveConfig slot key is invalid"
                            : profileError.c_str());
    }

    const auto* primary = FindEntry(*globalFlag, AsciiToU16(key));
    // Native operator[] creates VT_I4(0) for a missing key; BSTR conversion is
    // one code unit ("0"), so the following length>1 gate fails.  Projecting a
    // miss directly to empty is observationally equivalent without mutating the
    // read-only persistence document.
    if (primary == nullptr) return true;
    // The EXE calls VariantChangeType(VT_BSTR) without checking the original
    // VARIANT tag. Authenticated SaveConfig entries are type8; reproducing all
    // COM coercions would add unsafe behavior for malformed documents, so the
    // compatibility runtime deliberately fails closed outside that proven type.
    if (primary->type != 8) {
        return ReadFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days SaveConfig metadata is not authenticated type 8");
    }
    if (primary->stringValue.size() <= 1u) return true;

    std::string splitError;
    SlotMetadata projected;
    if (!SchoolDaysSaveMetadata::SplitStoredPrimary(
            primary->stringValue, profile.useEnglish, &projected.date,
            &projected.story, &splitError)) {
        // In particular, JP length==2 underflows the original len-3 substring
        // calculation instead of reaching a clean false branch. Reject it here
        // as an intentional safety guard.
        return ReadFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        splitError.empty()
                            ? "School Days SaveConfig metadata suffix is invalid"
                            : splitError.c_str());
    }

    const auto* sub = FindEntry(*globalFlag, AsciiToU16(key + "_Sub"));
    if (sub == nullptr) {
        // Native operator[] creates VT_I4(0) and VariantChangeType produces
        // BSTR "0". _Sub never participates in the occupied/empty decision.
        projected.comment = u"0";
    } else {
        // Same deliberate hardening as primary: shipped _Sub values are type8,
        // although the original would attempt Automation conversion of others.
        if (sub->type != 8) {
            return ReadFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days SaveConfig _Sub metadata is not authenticated type 8");
        }
        projected.comment = sub->stringValue;
    }
    projected.occupied = true;
    *out = std::move(projected);
    return true;
}

bool SchoolDaysSaveService::ReadSlotMetadata(
    const SchoolDaysSaveProfile& profile, int slot, bool fileOpenable,
    const SchoolDaysPersistence& persistence, SlotMetadata* out,
    Kotonoha_KtrfError* error) {
    const SchoolDaysFlgH* globalFlag =
        persistence.HasGlobalFlag() ? &persistence.GlobalFlagDocument() : nullptr;
    return ReadSlotMetadata(profile, slot, fileOpenable, globalFlag, out, error);
}

} // namespace Kotonoha
