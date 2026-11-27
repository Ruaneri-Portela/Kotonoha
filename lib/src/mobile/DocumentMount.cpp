#ifdef __ANDROID__
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <Kotonoha/mobile/DocumentMount.hpp>
#include <json-c/json.h>
#include <map>

namespace Kotonoha::Filesystem {
namespace {
struct Document {
  std::string uri;
  std::int64_t size;
};

class DocumentMount final : public IMount {
  std::map<std::string, Document> files_;

public:
  explicit DocumentMount(const std::filesystem::path &catalog) {
    std::unique_ptr<json_object, decltype(&json_object_put)> json(
        json_object_from_file(catalog.c_str()), json_object_put);
    if (!json_object_is_type(json.get(), json_type_object))
      throw FilesystemError(ErrorCode::IOError, "Cannot read document catalog");
    json_object_object_foreach(json.get(), key, value) {
      json_object *uri = nullptr, *size = nullptr;
      if (normalizePath(key) != key ||
          std::string_view(key).find('/') != std::string_view::npos ||
          !json_object_object_get_ex(value, "uri", &uri) ||
          !json_object_is_type(uri, json_type_string) ||
          !json_object_object_get_ex(value, "size", &size) ||
          !json_object_is_type(size, json_type_int))
        throw FilesystemError(ErrorCode::InvalidPath, "Invalid document entry");
      std::string address(json_object_get_string(uri),
                          json_object_get_string_len(uri));
      if (address.rfind("content://", 0) != 0 ||
          address.find('\0') != std::string::npos)
        throw FilesystemError(ErrorCode::InvalidPath, "Invalid document URI");
      files_.emplace(key,
                     Document{std::move(address), json_object_get_int64(size)});
    }
  }

  bool exists(std::string_view path) const override {
    auto name = normalizePath(path, true);
    return name.empty() || files_.count(name) != 0;
  }
  FileInfo stat(std::string_view path) const override {
    auto name = normalizePath(path, true);
    if (name.empty())
      return {"", FileType::Directory, 0};
    auto it = files_.find(name);
    if (it == files_.end())
      throw FilesystemError(ErrorCode::NotFound, "Unknown document");
    return {name, FileType::Regular, it->second.size};
  }
  std::vector<FileInfo> list(std::string_view path) const override {
    if (stat(path).type != FileType::Directory)
      throw FilesystemError(ErrorCode::NotDirectory,
                            "Document is not a directory");
    std::vector<FileInfo> result;
    for (const auto &[name, file] : files_)
      result.push_back({name, FileType::Regular, file.size});
    return result;
  }
  std::unique_ptr<FileHandle> open(std::string_view path) const override {
    auto info = stat(path);
    if (info.type == FileType::Directory)
      throw FilesystemError(ErrorCode::IsDirectory,
                            "Cannot open document directory");
    try {
      auto file = std::make_unique<SDLFileSource>(files_.at(info.path).uri);
      const auto length = file->size();
      if (length < 0 || (info.size >= 0 && length != info.size) ||
          file->seek(0, SeekOrigin::End) != length ||
          file->seek(0, SeekOrigin::Begin) != 0)
        throw FilesystemError(ErrorCode::IOError,
                              "Document is not seekable or has changed");
      return file;
    } catch (const FilesystemError &) {
      throw FilesystemError(ErrorCode::IOError,
                            "Cannot open seekable document: " + info.path);
    }
  }
};
} // namespace

std::shared_ptr<IMount>
openDocumentDirectory(const std::filesystem::path &catalog) {
  return std::make_shared<DocumentMount>(catalog);
}
} // namespace Kotonoha::Filesystem
#endif
