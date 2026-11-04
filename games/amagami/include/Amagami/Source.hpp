#pragma once
#include <Amagami/Formats.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <array>
#include <map>
#include <memory>
#include <optional>

namespace Amagami {
class Source {
public:
  explicit Source(const std::string &path);
  std::shared_ptr<Kotonoha::DiscImage> disc() const { return disc_; }
  struct Archive {
    Kotonoha::DiscImage::Entry file;
    std::vector<ArchiveEntry> entries;
  };
  const Archive &archive(const std::string &name) const;
  Bytes read(const std::string &archive, size_t index) const;
  Script script(size_t index) const;
  int find(const std::string &archive, const std::string &name) const;
  struct GraphicLocation {
    std::string archive;
    size_t index;
  };
  std::optional<GraphicLocation> graphic(const std::string &name) const;
  std::string eventPicture(unsigned character, unsigned number,
                           unsigned variant) const;
  std::string itemPicture(unsigned number) const;
  int musicIndex(unsigned number) const;
  int breathDirection(unsigned character, unsigned pose) const;
  SDL_FPoint actorOrigin(unsigned character, unsigned pose) const;
  std::pair<std::string, std::string>
  actorAssets(unsigned character, unsigned cloth, unsigned pose) const;
  Bytes voice(const std::string &code, unsigned number) const;
  Bytes ambience(unsigned index) const;
  Script systemClass(size_t index) const;
  const std::vector<std::string> &scenarios() const { return scenarios_; }
  const std::vector<ClassFile> &classes() const { return classes_; }
  size_t scenarioIndex(const std::string &name) const;

private:
  std::shared_ptr<Kotonoha::DiscImage> disc_;
  Bytes scenarioArchive_;
  mutable std::map<std::string, std::optional<GraphicLocation>>
      graphicLocations_;
  std::map<std::string, Archive> archives_;
  std::vector<std::string> scenarios_;
  std::map<std::string, size_t> scenarioByName_;
  std::vector<ClassFile> classes_;
  std::vector<std::pair<std::string, std::string>> actorAssets_;
  std::vector<std::vector<int>> breathDirections_;
  std::vector<std::vector<SDL_FPoint>> actorOrigins_;
  struct VoiceEntry {
    uint64_t offset, size;
  };
  std::map<std::string, VoiceEntry> voices_;
  std::map<unsigned, size_t> music_;
};
} // namespace Amagami
