#pragma once

#include <Kotonoha/parsers/Ors.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kotonoha {

// L4-only companion metadata. Never adds fields to commercial SLog/GlobalFlag.
// This is an integrity-bound origin hint for NEW Kotonoha saves, not a
// signature: missing, damaged or externally edited sidecars are untrusted.
class SchoolDaysSaveProvenance final {
public:
  struct Origin {
    bool lxhEpilogue = false;
    uint32_t tick = 0;
    uint32_t movieStartTick = 0;
    uint32_t nextTick = 0;
  };
  enum class Verdict { Untrusted, Normal, LxhEpilogue };

  // Does not change ORS/event state. The caller's movieStarted must come from
  // actual movie dispatch/phase, not merely from elapsed time or SceneKey.
  static Origin Capture(const std::string& sceneKey,
                        const Kotonoha_orsData& script, uint32_t tick,
                        bool temporaryPlayback,
                        bool pendingHandoff, bool pendingSceneAdvance,
                        bool movieStarted);

  static std::string Sha256(const std::vector<uint8_t>& bytes);
  static std::string SaveSceneKey(const std::vector<uint8_t>& bytes);
  static std::filesystem::path SidecarPath(const std::filesystem::path& savePath);

  // Call before touching a target SaveFile: fail if stale origin cannot be
  // invalidated. A crash after invalidation but before publication produces an
  // untrusted save, never a false assertion that it came from LxH.
  static bool Invalidate(const std::filesystem::path& savePath,
                         std::string* error = nullptr);
  // Call only after successful SaveFile write. Re-reads the file and verifies
  // the exact bytes, then publishes a temporary sidecar by same-dir rename.
  static bool Publish(const std::filesystem::path& savePath, int slot,
                      const std::vector<uint8_t>& savedBytes,
                      Origin origin, std::string* error = nullptr);
  // Pure preflight; no bridge, scene, timeline or session mutation.
  static Verdict Check(const std::filesystem::path& savePath, int slot,
                       const std::vector<uint8_t>& savedBytes);
};

} // namespace Kotonoha
