#pragma once

#include <Kotonoha/Filesystem/IMount.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Kotonoha {

// Shared preference, availability probe, and effective-state resolution for
// optional School Days LxH endings. Both Windows and Android launchers persist
// the exact same EnableLxh=0|1 key in Kotonoha.ini so the shared C++ runtime
// owns all functional interpretation across platforms.
class SchoolDaysLxhPreference final {
public:
  static constexpr std::string_view IniKey = "EnableLxh=";

  struct Availability {
    bool packPresent = false;
    bool packValid = false;
    std::string reason;
  };

  struct Resolution {
    bool userPreference = false;
    bool explicitPreference = false;
    bool packPresent = false;
    bool packValid = false;
    bool effectiveEnabled = false;
    std::string statusReason;
  };

  // Returns std::nullopt when no explicit EnableLxh key has been saved yet.
  static std::optional<bool> Read(const std::filesystem::path &documentsRoot);

  // Atomically updates or inserts EnableLxh=0|1 in <documentsRoot>/Kotonoha.ini
  // without altering ContentAspectPreference, AndroidFrameLimit, or other keys.
  static void Write(const std::filesystem::path &documentsRoot, bool enabled);

  // Non-mutating validation of Packs/LxH.GPK (28 seekable raw canonical files).
  static Availability ProbePack(
      const std::filesystem::path &gameOrPacksRoot,
      std::shared_ptr<const Filesystem::IMount> source = {});

  // Separates user preference, content availability, and effective runtime
  // state. Never erases an explicit user preference when files are absent.
  static Resolution Resolve(std::optional<bool> savedPreference,
                            const Availability &availability,
                            bool translationReady = true,
                            const std::string &translationReason = {});
};

} // namespace Kotonoha
