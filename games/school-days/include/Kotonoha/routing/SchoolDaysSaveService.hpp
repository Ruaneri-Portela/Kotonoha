#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysPersistence.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>

namespace Kotonoha {

// High-level Save(slot, metadata) planning for the authenticated School Days
// HQ v1.02 profile.  This class deliberately does not perform filesystem I/O:
// the original high-level writer writes the SLog first, then updates the two
// SaveConfig type-8 values, then flushes GlobalFlag.  A UI/filesystem owner can
// preserve that ordering using this plan without persistence owning dialogs or
// overwrite policy.
class SchoolDaysSaveService {
public:
    struct Plan {
        int slot = -1;
        std::string saveFilePath;
        std::string globalFlagPath;
        std::string saveConfigKey;
        std::string saveConfigSubKey;
        std::vector<uint8_t> saveFileBytes;
        std::vector<uint8_t> globalFlagBytes;
        SchoolDaysPersistence stagedPersistence;
    };

    struct SlotMetadata {
        bool occupied = false;
        std::u16string date;
        std::u16string story;
        std::u16string comment;
    };

    static bool PlanSlot(int slot, const std::u16string& primaryMetadata,
                         const std::u16string& subMetadata,
                         const SchoolDaysPersistence& persistence,
                         const SchoolDaysStateStore& state, Plan* out,
                         Kotonoha_KtrfError* error);
    static bool PlanSlot(const SchoolDaysSaveProfile& profile, int slot,
                         const std::u16string& primaryMetadata,
                         const std::u16string& subMetadata,
                         const SchoolDaysPersistence& persistence,
                         const SchoolDaysStateStore& state, Plan* out,
                         Kotonoha_KtrfError* error);

    // Call only after the caller has completed the external writes it chose to
    // perform from Plan.  This mirrors the fact that metadata becomes live only
    // at the high-level Save boundary, not during low-level SLog serialization.
    static bool Commit(const Plan& plan, SchoolDaysPersistence* persistence,
                       Kotonoha_KtrfError* error);

    // Read-only projection of EXE host +0x9C (0x0042A980) after the caller has
    // reproduced its SaveFile-open gate.  Missing SaveConfig is an empty slot;
    // valid occupied metadata is type-8 and split with the native JP/EN suffix
    // widths.  Corrupt non-type8 SaveConfig entries fail closed rather than
    // emulating COM coercions that never occur in authenticated v1.02 saves.
    static bool ReadSlotMetadata(const SchoolDaysSaveProfile& profile, int slot,
                                 bool fileOpenable,
                                 const SchoolDaysFlgH* globalFlag,
                                 SlotMetadata* out,
                                 Kotonoha_KtrfError* error);
    static bool ReadSlotMetadata(const SchoolDaysSaveProfile& profile, int slot,
                                 bool fileOpenable,
                                 const SchoolDaysPersistence& persistence,
                                 SlotMetadata* out,
                                 Kotonoha_KtrfError* error);

private:
    static bool Fail(Kotonoha_KtrfError* error,
                     Kotonoha_KtrfErrorCode code, const char* message);
};

} // namespace Kotonoha
