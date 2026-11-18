#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

namespace Kotonoha {
namespace {

bool Fail(std::string *error, const char *message) {
  if (error != nullptr)
    *error = message != nullptr ? message : "School Days save storage error";
  return false;
}

} // namespace

bool SchoolDaysSaveStorage::Initialize(
    const std::filesystem::path &executableDirectory,
    const SchoolDaysSaveProfile &profile, std::string *error) {
  if (error != nullptr)
    error->clear();
  initialized_ = false;
  baseDirectory_.clear();
  profile_ = {};
  if (executableDirectory.empty() || profile.saveFolder.empty()) {
    return Fail(error, "School Days save storage init arguments are invalid");
  }
  baseDirectory_ =
      std::filesystem::absolute(executableDirectory).lexically_normal();
  profile_ = profile;

  std::filesystem::path folder;
  if (!ResolveRelative(profile_.saveFolder, &folder, error))
    return false;
  // Create the designated save folder once at startup. Later slot access
  // reports real failures rather than silently changing the directory tree.
  std::error_code ignored;
  (void)std::filesystem::create_directories(folder, ignored);
  initialized_ = true;
  return true;
}

bool SchoolDaysSaveStorage::ResolveRelative(const std::string &configured,
                                            std::filesystem::path *out,
                                            std::string *error) const {
  if (out == nullptr || configured.empty() || baseDirectory_.empty()) {
    return Fail(error,
                "School Days save path resolution arguments are invalid");
  }
  std::filesystem::path relative(configured);
  // The authenticated v1.02 profile is relative to the executable.  Refuse
  // absolute/traversal paths here rather than letting extracted config escape
  // the Kotonoha runtime directory.
  if (relative.is_absolute() || relative.has_root_name() ||
      relative.has_root_directory()) {
    return Fail(error, "School Days save path must be executable-relative");
  }
  const auto normalized = relative.lexically_normal();
  for (const auto &part : normalized) {
    if (part == "..")
      return Fail(error, "School Days save path escapes executable directory");
  }
  *out = (baseDirectory_ / normalized).lexically_normal();
  return true;
}

bool SchoolDaysSaveStorage::ReadFile(const std::filesystem::path &path,
                                     std::vector<uint8_t> *bytes,
                                     std::string *error) {
  if (bytes == nullptr)
    return Fail(error, "School Days read output is null");
  bytes->clear();
  // Reading physical saves goes through the same SDL-backed adapter as
  // every other Kotonoha provider. It also accepts Unicode directories on
  // Windows because SDLFileSource passes UTF-8 to SDL_IOFromFile.
  try {
    *bytes = Kotonoha::Filesystem::readLocalFile(path);
    return true;
  } catch (const std::exception &failure) {
    bytes->clear();
    if (error)
      *error =
          std::string("School Days save file read failed: ") + failure.what();
    return false;
  }
}

bool SchoolDaysSaveStorage::WriteFileTruncate(const std::filesystem::path &path,
                                              const std::vector<uint8_t> &bytes,
                                              std::string *error) {
  if (bytes.empty())
    return Fail(error, "School Days save write payload is empty");
  Kotonoha::Filesystem::OutputFile file(path,
                                        std::ios::binary | std::ios::trunc);
  if (!file)
    return Fail(error, "School Days save file could not be opened for write");
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file)
    return Fail(error, "School Days save file write failed");
  file.close();
  if (!file)
    return Fail(error, "School Days save file close failed");
  return true;
}

bool SchoolDaysSaveStorage::LoadGlobalFlag(std::vector<uint8_t> *bytes,
                                           bool *present,
                                           std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || bytes == nullptr || present == nullptr) {
    return Fail(error,
                "School Days GlobalFlag load storage is not initialized");
  }
  *present = false;
  bytes->clear();
  std::filesystem::path path;
  if (!ResolveRelative(profile_.flagFileName, &path, error))
    return false;
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec)
    return Fail(error, "School Days GlobalFlag existence query failed");
  if (!exists)
    return true; // OPEN_EXISTING miss is a normal first-run state.
  if (!ReadFile(path, bytes, error))
    return false;
  *present = true;
  return true;
}

bool SchoolDaysSaveStorage::ProbeSaveFile(int slot, bool *openable,
                                          std::string *error) const {
  if (error != nullptr)
    error->clear();
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
  if (!ResolveRelative(configured, &path, error))
    return false;

  // Native +0x9C treats an unsuccessful file open as an unoccupied slot and
  // returns 0; it does not escalate the miss through the SysMenu interface.
  try {
    Kotonoha::Filesystem::SDLFileSource probe(path);
    *openable = true;
  } catch (const std::exception &) {
    // An inaccessible or missing slot is an empty row, not a save failure.
  }
  return true;
}

bool SchoolDaysSaveStorage::ReadSaveFile(int slot, std::vector<uint8_t> *bytes,
                                         std::string *error) const {
  if (error != nullptr)
    error->clear();
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
  if (!ResolveRelative(configured, &path, error))
    return false;
  return ReadFile(path, bytes, error);
}

bool SchoolDaysSaveStorage::SaveFilePath(
    int slot, std::filesystem::path *path, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || path == nullptr || slot < 0 || slot >= 100)
    return Fail(error, "School Days save-slot path arguments are invalid");
  std::string configured;
  std::string profileError;
  if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                           &configured, &profileError))
    return Fail(error, profileError.empty()
                           ? "School Days save-slot path format is invalid"
                           : profileError.c_str());
  return ResolveRelative(configured, path, error);
}

bool SchoolDaysSaveStorage::WriteSaveFile(
    const SchoolDaysSaveService::Plan &plan, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(plan.saveFilePath, &path, error))
    return false;
  // No lazy mkdir here: native writer relies on init-time SaveFolder creation.
  return WriteFileTruncate(path, plan.saveFileBytes, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlag(
    const SchoolDaysSaveService::Plan &plan, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(plan.globalFlagPath, &path, error))
    return false;
  return SchoolDaysAtomicFile::Write(path, plan.globalFlagBytes,
                                     globalFlagWriteFaultForTesting_, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlagBytes(
    const std::vector<uint8_t> &bytes, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(profile_.flagFileName, &path, error))
    return false;
  return SchoolDaysAtomicFile::Write(path, bytes,
                                     globalFlagWriteFaultForTesting_, error);
}

} // namespace Kotonoha
