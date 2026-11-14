#pragma once
// Ren'Py 6.99 containers: RPA-3.0 archives, pickled data and compiled scripts
// (.rpyc).
#include "Ddlc/Value.hpp"
#include <Kotonoha/Filesystem/FileHandle.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Ddlc {

// Python pickle (protocol 2) to Value. Objects keep their identity (memo).
Value unpickle(const uint8_t *data, size_t size);
inline Value unpickle(const std::vector<uint8_t> &d) {
  return unpickle(d.data(), d.size());
}

bool inflate(const uint8_t *data, size_t size, std::vector<uint8_t> &out);

class Archive {
public:
  Archive() = default;
  ~Archive();
  Archive(const Archive &) = delete;
  Archive &operator=(const Archive &) = delete;

  bool open(const std::string &path);
  bool open(std::unique_ptr<Kotonoha::Filesystem::FileHandle> source);
  bool has(const std::string &name) const;
  std::vector<uint8_t> read(const std::string &name) const;
  std::vector<std::string> names() const;

private:
  struct Entry {
    uint64_t offset = 0, length = 0;
    std::string prefix;
  };
  using File = std::unique_ptr<Kotonoha::Filesystem::FileHandle>;
  File file_;
  mutable std::mutex readMutex_;
  std::map<std::string, std::vector<Entry>> index_; // lower-case name
};

// Every archive of a game directory plus its loose files, looked up the way
// Ren'Py does (case-insensitive, "images/" prefix for image files).
class Assets {
public:
  bool open(const std::string &gameDir);
  using OverlayRead =
      std::function<bool(const std::string &, std::vector<uint8_t> *)>;
  using OverlayNames = std::function<std::vector<std::string>()>;
  void mountOverlay(const std::vector<std::string> &packages, OverlayRead read,
                    OverlayNames names, OverlayRead rootRead = {});
  bool readOverlayRoot(const std::string &name,
                       std::vector<uint8_t> *out) const;
  const std::string &dir() const { return dir_; }
  bool has(const std::string &name) const;
  std::vector<uint8_t> read(const std::string &name) const;
  // Image lookup: name, then images/name.
  std::string resolveImage(const std::string &name) const;
  std::vector<std::string> names() const;

private:
  std::string dir_;
  std::vector<std::unique_ptr<Archive>> archives_, overlayArchives_;
  std::map<std::string, std::filesystem::path> loose_;
  OverlayRead overlayRead_, rootRead_;
  OverlayNames overlayNames_;
};

// .rpyc: RENPY RPC2 slot 1 holds (data, [statements]).
bool loadRpyc(const std::vector<uint8_t> &file, Value &out);

} // namespace Ddlc
