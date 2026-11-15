#include <Kotonoha/Filesystem/Mounts/TarMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Filesystem {
namespace {

std::string field(const char *data, std::size_t length) {
  const auto end = static_cast<const char *>(std::memchr(data, '\0', length));
  return std::string(data, end ? end : data + length);
}

std::uint64_t octal(const char *text, std::size_t length) {
  if (length && (static_cast<unsigned char>(text[0]) & 0x80u))
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "TAR base-256 sizes are not yet supported");
  std::uint64_t result = 0;
  bool parsed = false;
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch == ' ' || ch == '\0') {
      if (parsed)
        break;
      continue;
    }
    if (ch < '0' || ch > '7' || result > (UINT64_MAX - (ch - '0')) / 8)
      throw FilesystemError(ErrorCode::CorruptImage, "Invalid TAR octal field");
    parsed = true;
    result = result * 8 + (ch - '0');
  }
  return result;
}

void readExactly(FileHandle &file, void *buffer, std::size_t size) {
  auto *bytes = static_cast<std::uint8_t *>(buffer);
  std::size_t done = 0;
  while (done < size) {
    const auto got = file.read(bytes + done, size - done);
    if (got <= 0)
      throw FilesystemError(ErrorCode::CorruptImage, "Truncated TAR archive");
    done += static_cast<std::size_t>(got);
  }
}

std::string parentOf(const std::string &name) {
  const auto split = name.rfind('/');
  return split == std::string::npos ? "" : name.substr(0, split);
}

class SliceHandle final : public FileHandle {
  std::unique_ptr<FileHandle> base_;
  std::int64_t begin_, length_, cursor_ = 0;
public:
  SliceHandle(std::unique_ptr<FileHandle> base, std::uint64_t begin,
              std::uint64_t length)
      : base_(std::move(base)), begin_(static_cast<std::int64_t>(begin)),
        length_(static_cast<std::int64_t>(length)) {
    if (!base_ || begin > INT64_MAX || length > INT64_MAX ||
        begin > static_cast<std::uint64_t>(INT64_MAX) - length)
      throw FilesystemError(ErrorCode::CorruptImage, "TAR extent overflow");
  }
  std::int64_t read(void *dest, std::size_t count) override {
    if (cursor_ == length_) return 0;
    if (!dest && count) return -1;
    if (base_->seek(begin_ + cursor_, SeekOrigin::Begin) < 0)
      return -1;
    const auto n = base_->read(dest, static_cast<std::size_t>(
        std::min<std::uint64_t>(count, length_ - cursor_)));
    if (n > 0) cursor_ += n;
    return n;
  }
  std::int64_t seek(std::int64_t offset, SeekOrigin origin) override {
    const std::int64_t anchor = origin == SeekOrigin::Begin ? 0
        : (origin == SeekOrigin::Current ? cursor_ : length_);
    if (offset >= 0) {
      if (offset > length_ - anchor) return -1;
    } else if (offset < -anchor) {
      return -1;
    }
    cursor_ = anchor + offset;
    return cursor_;
  }
  std::int64_t tell() const override { return cursor_; }
  std::int64_t size() const override { return length_; }
};

std::string paxPath(const std::string &payload) {
  std::size_t position = 0;
  std::string path;
  while (position < payload.size()) {
    const auto separator = payload.find(' ', position);
    if (separator == std::string::npos)
      throw FilesystemError(ErrorCode::CorruptImage, "Invalid PAX record");
    std::size_t length = 0;
    for (auto i = position; i < separator; ++i) {
      if (payload[i] < '0' || payload[i] > '9' ||
          length > (payload.size() - position) / 10)
        throw FilesystemError(ErrorCode::CorruptImage, "Invalid PAX length");
      length = length * 10 + static_cast<unsigned>(payload[i] - '0');
    }
    if (length < separator - position + 3 ||
        length > payload.size() - position ||
        payload[position + length - 1] != '\n')
      throw FilesystemError(ErrorCode::CorruptImage, "Truncated PAX record");
    const auto keyStart = separator + 1;
    const auto equals = payload.find('=', keyStart);
    if (equals != std::string::npos && equals < position + length - 1 &&
        payload.substr(keyStart, equals - keyStart) == "path")
      path = payload.substr(equals + 1, position + length - 2 - equals);
    position += length;
  }
  return path;
}

} // namespace

TarMount::TarMount(const std::filesystem::path &archive)
    : TarMount([archive]() {
        return std::make_unique<SDLFileSource>(archive);
      }) {}

TarMount::TarMount(std::function<std::unique_ptr<FileHandle>()> openSource)
    : source_(std::move(openSource)) {
  if (!source_)
    throw FilesystemError(ErrorCode::InvalidPath, "Null TAR byte source");
  auto file = source_();
  const auto length = file->size();
  if (length < 1024)
    throw FilesystemError(ErrorCode::CorruptImage, "Truncated TAR header");
  nodes_.emplace("", Node{FileInfo{"", FileType::Directory, 0}, 0});
  std::uint64_t offset = 0;
  std::string nextName;
  while (offset <= static_cast<std::uint64_t>(length) - 512) {
    if (file->seek(static_cast<std::int64_t>(offset),
                   SeekOrigin::Begin) != static_cast<std::int64_t>(offset))
      throw FilesystemError(ErrorCode::IOError, "Cannot seek TAR");
    std::array<char, 512> hdr{};
    readExactly(*file, hdr.data(), hdr.size());
    if (std::all_of(hdr.begin(), hdr.end(), [](char ch) { return ch == 0; }))
      return;

    auto stored = octal(hdr.data() + 148, 8);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < hdr.size(); ++i)
      sum += (i >= 148 && i < 156) ? 32
             : static_cast<unsigned char>(hdr[i]);
    if (sum != stored)
      throw FilesystemError(ErrorCode::CorruptImage, "TAR checksum mismatch");
    const auto size = octal(hdr.data() + 124, 12);
    const auto dataAt = offset + 512;
    if (size > UINT64_MAX - 511)
      throw FilesystemError(ErrorCode::CorruptImage, "TAR size overflow");
    const auto padded = ((size + 511) / 512) * 512;
    if (dataAt > static_cast<std::uint64_t>(length) ||
        padded > static_cast<std::uint64_t>(length) - dataAt ||
        size > INT64_MAX)
      throw FilesystemError(ErrorCode::CorruptImage, "TAR extent out of bounds");

    const auto type = hdr[156];
    if (type == 'L' || type == 'x') {
      if (size > 1024 * 1024)
        throw FilesystemError(ErrorCode::CorruptImage,
                              "Oversized TAR extended filename");
      if (file->seek(static_cast<std::int64_t>(dataAt),
                     SeekOrigin::Begin) != static_cast<std::int64_t>(dataAt))
        throw FilesystemError(ErrorCode::IOError, "Cannot seek TAR extension");
      std::string extension(static_cast<std::size_t>(size), '\0');
      if (size)
        readExactly(*file, extension.data(), extension.size());
      nextName = type == 'x'
          ? paxPath(extension) : field(extension.data(), extension.size());
    } else if (type == '\0' || type == '0' || type == '5') {
      auto name = field(hdr.data(), 100);
      const auto prefix = field(hdr.data() + 345, 155);
      if (!prefix.empty())
        name = prefix + "/" + name;
      if (!nextName.empty())
        name = std::exchange(nextName, {});
      // A directory can have a trailing slash in USTAR.
      while (!name.empty() && name.back() == '/')
        name.pop_back();
      if (name.empty() || name == ".") {
        if (type != '5')
          throw FilesystemError(ErrorCode::InvalidPath, "Empty TAR filename");
      } else {
        name = normalizePath(name);
        const auto addDirectory = [&](std::string path) {
          std::vector<std::string> ancestors;
          while (!path.empty()) {
            ancestors.push_back(path);
            path = parentOf(path);
          }
          for (auto i = ancestors.rbegin(); i != ancestors.rend(); ++i) {
            const auto key = lookupKey(*i);
            const auto found = nodes_.find(key);
            if (found == nodes_.end()) {
              nodes_.emplace(key, Node{
                  FileInfo{*i, FileType::Directory, 0}, 0});
            } else if (found->second.info.type != FileType::Directory) {
              throw FilesystemError(ErrorCode::CorruptImage,
                                    "TAR file/directory conflict");
            }
          }
        };
        addDirectory(parentOf(name));
        if (type == '5') {
          addDirectory(name);
        } else {
          const auto key = lookupKey(name);
          if (!nodes_.emplace(key, Node{
                  FileInfo{name, FileType::Regular,
                           static_cast<std::int64_t>(size)}, dataAt}).second)
            throw FilesystemError(ErrorCode::CorruptImage,
                                  "Duplicate TAR filename: " + name);
        }
      }
    } else if (type == 'g') {
      // Global PAX metadata: no file record.
    } else {
      throw FilesystemError(ErrorCode::UnsupportedMount,
                            "Unsupported TAR link/special file");
    }
    offset = dataAt + padded;
  }
  throw FilesystemError(ErrorCode::CorruptImage, "Missing TAR end marker");
}

bool TarMount::exists(std::string_view path) const {
  return nodes_.count(lookupKey(path, true)) != 0;
}
FileInfo TarMount::stat(std::string_view path) const {
  const auto it = nodes_.find(lookupKey(path, true));
  if (it == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "TAR entry not found");
  return it->second.info;
}
std::vector<FileInfo> TarMount::list(std::string_view dir) const {
  const auto path = lookupKey(dir, true);
  if (stat(path).type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory, "Not a TAR directory");
  std::vector<FileInfo> out;
  for (const auto &[key, entry] : nodes_) {
    if (!key.empty() && key != path && parentOf(key) == path)
      out.push_back(entry.info);
  }
  return out;
}
std::unique_ptr<FileHandle> TarMount::open(std::string_view path) const {
  const auto it = nodes_.find(lookupKey(path));
  if (it == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "TAR entry not found");
  if (it->second.info.type != FileType::Regular)
    throw FilesystemError(ErrorCode::IsDirectory, "Cannot open TAR directory");
  return std::make_unique<SliceHandle>(
      source_(), it->second.offset,
      static_cast<std::uint64_t>(it->second.info.size));
}
} // namespace Kotonoha::Filesystem
