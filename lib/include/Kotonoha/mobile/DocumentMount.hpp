#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <filesystem>

namespace Kotonoha::Filesystem {
// Opens an app-private catalog of immediate children of a granted SAF folder.
// Document IDs are opaque. Each open gets an independent read-only cursor.
std::shared_ptr<IMount>
openDocumentDirectory(const std::filesystem::path &catalog);
} // namespace Kotonoha::Filesystem
