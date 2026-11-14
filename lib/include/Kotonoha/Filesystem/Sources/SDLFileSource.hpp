#pragma once
#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <filesystem>
struct SDL_IOStream;
namespace Kotonoha::Filesystem {
class SDLFileSource final : public FileHandle {
  SDL_IOStream *io_ = nullptr;
public:
  explicit SDLFileSource(const std::filesystem::path &file);
  ~SDLFileSource() override;
  SDLFileSource(const SDLFileSource &) = delete;
  SDLFileSource &operator=(const SDLFileSource &) = delete;
  std::int64_t read(void *destination, std::size_t count) override;
  std::int64_t seek(std::int64_t offset, SeekOrigin origin) override;
  std::int64_t tell() const override;
  std::int64_t size() const override;
};
} // namespace Kotonoha::Filesystem
