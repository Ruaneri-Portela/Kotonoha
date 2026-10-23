#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>

namespace Kotonoha {

// Filesystem owner for the authenticated EXE-dir-relative save boundary.
// It intentionally keeps slot write and GlobalFlag flush separate so the app
// can commit in-memory metadata between them, matching EXE 0x0042AEA0.
class SchoolDaysSaveStorage {
public:
    bool Initialize(const std::filesystem::path& executableDirectory,
                    const SchoolDaysSaveProfile& profile,
                    std::string* error = nullptr);

    bool LoadGlobalFlag(std::vector<uint8_t>* bytes, bool* present,
                        std::string* error = nullptr) const;
    // EXE host +0x9C (0x0042A980) first formats SaveFileName for the slot and
    // attempts to open it.  A missing/unopenable slot is a normal "empty row"
    // condition rather than a storage failure; path/profile errors still fail.
    bool ProbeSaveFile(int slot, bool* openable,
                       std::string* error = nullptr) const;
    bool ReadSaveFile(int slot, std::vector<uint8_t>* bytes,
                      std::string* error = nullptr) const;
    bool WriteSaveFile(const SchoolDaysSaveService::Plan& plan,
                       std::string* error = nullptr) const;
    bool WriteGlobalFlag(const SchoolDaysSaveService::Plan& plan,
                         std::string* error = nullptr) const;
    // Shutdown/global-state flush has no slot plan.  It targets the configured
    // FlagFileName directly, matching the native standalone GlobalFlag writer.
    bool WriteGlobalFlagBytes(const std::vector<uint8_t>& bytes,
                              std::string* error = nullptr) const;

    bool IsInitialized() const { return initialized_; }
    const std::filesystem::path& BaseDirectory() const { return baseDirectory_; }

private:
    bool ResolveRelative(const std::string& configured,
                         std::filesystem::path* out,
                         std::string* error) const;
    static bool ReadFile(const std::filesystem::path& path,
                         std::vector<uint8_t>* bytes,
                         std::string* error);
    static bool WriteFileTruncate(const std::filesystem::path& path,
                                  const std::vector<uint8_t>& bytes,
                                  std::string* error);

    std::filesystem::path baseDirectory_;
    SchoolDaysSaveProfile profile_;
    bool initialized_ = false;
};

} // namespace Kotonoha
