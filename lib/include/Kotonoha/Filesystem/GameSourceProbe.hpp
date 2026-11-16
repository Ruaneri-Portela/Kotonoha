#pragma once

#include <Kotonoha/Filesystem/Filesystem.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace Kotonoha::Filesystem {

// Locate a group of required regular files in a mounted game source.
// The lookup uses the same case-insensitive, layered virtual filesystem as
// the asset providers, including ISO images. A directory with the right name
// is NOT accepted as a required file. I/O exceptions intentionally propagate.
inline std::optional<std::string> findRequiredFilesPrefix(
    const Filesystem &source,
    std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> prefixes = {""}) {
  if (required.size() == 0)
    return std::nullopt;
  for (const auto prefix : prefixes) {
    bool complete = true;
    for (const auto name : required) {
      if (name.empty()) {
        complete = false;
        break;
      }
      const std::string id = std::string(prefix) + std::string(name);
      if (!source.exists(id) ||
          source.stat(id).type != FileType::Regular) {
        complete = false;
        break;
      }
    }
    if (complete)
      return std::string(prefix);
  }
  return std::nullopt;
}

} // namespace Kotonoha::Filesystem
