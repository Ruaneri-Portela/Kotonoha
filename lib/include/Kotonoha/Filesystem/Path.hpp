#pragma once
#include <string>
#include <string_view>
namespace Kotonoha::Filesystem {
// Portable relative virtual paths; allowRoot=true accepts the empty root.
// Use ASCII-only case folding like AssetOverlay.
std::string normalizePath(std::string_view path, bool allowRoot = false);
std::string lookupKey(std::string_view path, bool allowRoot = false);
} // namespace Kotonoha::Filesystem
