#pragma once

#include <Kotonoha/script/Timeline.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Kotonoha {

struct VNSave {
  int nodeId = -1;
  ScriptMode mode = ScriptMode::Standard;
  double timelinePosition = 0;
  std::vector<std::size_t> resolvedChoices;
  std::optional<int> pendingBranch;
  std::string gameData;
  // Optional packed RGB24 preview. Embedded in the same file as the save so
  // the native slot manager can rename or delete both in one operation.
  static constexpr int thumbnailWidth = 192;
  static constexpr int thumbnailHeight = 108;
  static constexpr std::size_t thumbnailBytes =
      thumbnailWidth * thumbnailHeight * 3;
  std::vector<std::uint8_t> thumbnailRgb;

  void write(const std::string &path) const;
  static VNSave read(const std::string &path);
};

struct VNSaveSlot {
  std::string path;
  std::string name;
};

class VNSaveSlots {
  std::string directory;

public:
  explicit VNSaveSlots(std::string directory);
  std::vector<VNSaveSlot> list() const;
  std::string nextPath(const std::string &suggestion = {}) const;
  std::string rename(const std::string &name, const std::string &newName) const;
  void remove(const std::string &name) const;
};

} // namespace Kotonoha
