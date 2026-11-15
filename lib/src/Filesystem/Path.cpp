#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/IMount.hpp>

#include <algorithm>
#include <vector>

namespace Kotonoha::Filesystem {

std::string normalizePath(std::string_view value, bool allowRoot) {
  if (value.empty() && allowRoot)
    return {};
  if (value.empty())
    throw FilesystemError(ErrorCode::InvalidPath, "Empty virtual path");

  std::string source(value);
  if (source.find('\0') != std::string::npos ||
      source.find(':') != std::string::npos)
    throw FilesystemError(ErrorCode::InvalidPath, "Invalid virtual path");

  std::replace(source.begin(), source.end(), '\\', '/');
  if (source.front() == '/')
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Virtual path cannot be absolute");

  std::vector<std::string> parts;
  std::size_t from = 0;
  while (from < source.size()) {
    const auto end = source.find('/', from);
    const auto size = (end == std::string::npos ? source.size() : end) - from;
    const auto part = source.substr(from, size);
    if (part == "..")
      throw FilesystemError(ErrorCode::InvalidPath, "Traversal in virtual path");
    if (!part.empty() && part != ".")
      parts.push_back(part);
    if (end == std::string::npos)
      break;
    from = end + 1;
  }
  if (parts.empty()) {
    if (allowRoot)
      return {};
    throw FilesystemError(ErrorCode::InvalidPath, "Invalid virtual path");
  }
  std::string out;
  for (const auto &part : parts) {
    if (!out.empty())
      out.push_back('/');
    out += part;
  }
  return out;
}

std::string lookupKey(std::string_view path, bool allowRoot) {
  auto key = normalizePath(path, allowRoot);
  for (char &ch : key)
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  return key;
}

} // namespace Kotonoha::Filesystem
