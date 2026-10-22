#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <fstream>
#include <limits>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* text) {
    if (error != nullptr) *error = text != nullptr ? text : "School Days save storage error";
    return false;
}

} // namespace

bool SchoolDaysSaveStorage::Initialize(
    const std::filesystem::path& executableDirectory,
    const SchoolDaysSaveProfile& profile, std::string* error) {
    if (error != nullptr) error->clear();
    initialized_ = false;
    baseDirectory_.clear();
    profile_ = {};
    if (executableDirectory.empty() || profile.saveFolder.empty()) {
        return Fail(error, "School Days save storage init arguments are invalid");
    }
    baseDirectory_ = std::filesystem::absolute(executableDirectory).lexically_normal();
    profile_ = profile;

    std::filesystem::path folder;
    if (!ResolveRelative(profile_.saveFolder, &folder, error)) return false;
    // Native init ignores CreateDirectoryA's return. Mirror that ownership:
    // attempt exactly once here and let later slot open report any real failure.
    std::error_code ignored;
    (void)std::filesystem::create_directories(folder, ignored);
    initialized_ = true;
    return true;
}

bool SchoolDaysSaveStorage::ResolveRelative(const std::string& configured,
                                            std::filesystem::path* out,
                                            std::string* error) const {
    if (out == nullptr || configured.empty() || baseDirectory_.empty()) {
        return Fail(error, "School Days save path resolution arguments are invalid");
    }
    std::filesystem::path relative(configured);
    // The authenticated v1.02 profile is relative to the executable.  Refuse
    // absolute/traversal paths here rather than letting extracted config escape
    // the Kotonoha runtime directory.
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return Fail(error, "School Days save path must be executable-relative");
    }
    const auto normalized = relative.lexically_normal();
    for (const auto& part : normalized) {
        if (part == "..") return Fail(error, "School Days save path escapes executable directory");
    }
    *out = (baseDirectory_ / normalized).lexically_normal();
    return true;
}

bool SchoolDaysSaveStorage::ReadFile(const std::filesystem::path& path,
                                     std::vector<uint8_t>* bytes,
                                     std::string* error) {
    if (bytes == nullptr) return Fail(error, "School Days read output is null");
    bytes->clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Fail(error, "School Days save file could not be opened for read");
    const auto end = file.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) >
                       static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
        return Fail(error, "School Days save file size is invalid");
    }
    bytes->resize(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes->empty() && !file.read(reinterpret_cast<char*>(bytes->data()),
                                     static_cast<std::streamsize>(bytes->size()))) {
        bytes->clear();
        return Fail(error, "School Days save file read failed");
    }
    return true;
}

bool SchoolDaysSaveStorage::WriteFileTruncate(
    const std::filesystem::path& path, const std::vector<uint8_t>& bytes,
    std::string* error) {
    if (bytes.empty()) return Fail(error, "School Days save write payload is empty");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return Fail(error, "School Days save file could not be opened for write");
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) return Fail(error, "School Days save file write failed");
    file.close();
    if (!file) return Fail(error, "School Days save file close failed");
    return true;
}

bool SchoolDaysSaveStorage::LoadGlobalFlag(std::vector<uint8_t>* bytes,
                                           bool* present,
                                           std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_ || bytes == nullptr || present == nullptr) {
        return Fail(error, "School Days GlobalFlag load storage is not initialized");
    }
    *present = false;
    bytes->clear();
    std::filesystem::path path;
    if (!ResolveRelative(profile_.flagFileName, &path, error)) return false;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return Fail(error, "School Days GlobalFlag existence query failed");
    if (!exists) return true; // OPEN_EXISTING miss is a normal first-run state.
    if (!ReadFile(path, bytes, error)) return false;
    *present = true;
    return true;
}

bool SchoolDaysSaveStorage::ProbeSaveFile(int slot, bool* openable,
                                          std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_ || openable == nullptr || slot < 0 || slot >= 100) {
        return Fail(error, "School Days save-slot probe arguments are invalid");
    }
    *openable = false;

    std::string configured;
    std::string profileError;
    if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                               &configured, &profileError)) {
        return Fail(error, profileError.empty()
                               ? "School Days save-slot path format is invalid"
                               : profileError.c_str());
    }

    std::filesystem::path path;
    if (!ResolveRelative(configured, &path, error)) return false;

    // Native +0x9C treats an unsuccessful file open as an unoccupied slot and
    // returns 0; it does not escalate the miss through the SysMenu interface.
    std::ifstream file(path, std::ios::binary);
    if (!file) return true;
    *openable = true;
    return true;
}

bool SchoolDaysSaveStorage::ReadSaveFile(int slot, std::vector<uint8_t>* bytes,
                                         std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_ || bytes == nullptr || slot < 0 || slot >= 100) {
        return Fail(error, "School Days save-slot read arguments are invalid");
    }
    bytes->clear();

    std::string configured;
    std::string profileError;
    if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                               &configured, &profileError)) {
        return Fail(error, profileError.empty()
                               ? "School Days save-slot path format is invalid"
                               : profileError.c_str());
    }
    std::filesystem::path path;
    if (!ResolveRelative(configured, &path, error)) return false;
    return ReadFile(path, bytes, error);
}

bool SchoolDaysSaveStorage::WriteSaveFile(
    const SchoolDaysSaveService::Plan& plan, std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_) return Fail(error, "School Days save storage is not initialized");
    std::filesystem::path path;
    if (!ResolveRelative(plan.saveFilePath, &path, error)) return false;
    // No lazy mkdir here: native writer relies on init-time SaveFolder creation.
    return WriteFileTruncate(path, plan.saveFileBytes, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlag(
    const SchoolDaysSaveService::Plan& plan, std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_) return Fail(error, "School Days save storage is not initialized");
    std::filesystem::path path;
    if (!ResolveRelative(plan.globalFlagPath, &path, error)) return false;
    return WriteFileTruncate(path, plan.globalFlagBytes, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlagBytes(
    const std::vector<uint8_t>& bytes, std::string* error) const {
    if (error != nullptr) error->clear();
    if (!initialized_) return Fail(error, "School Days save storage is not initialized");
    std::filesystem::path path;
    if (!ResolveRelative(profile_.flagFileName, &path, error)) return false;
    return WriteFileTruncate(path, bytes, error);
}

} // namespace Kotonoha
