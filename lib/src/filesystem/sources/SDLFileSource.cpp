#include <Kotonoha/Filesystem/IMount.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <SDL3/SDL.h>
#include <cstdio>
#include <limits>

namespace Kotonoha::Filesystem {

SDLFileSource::SDLFileSource(const std::filesystem::path &file) {
#ifdef __ANDROID__
  // SDL falls back to the Java asset manager when fopen fails, including for
  // absolute paths. A native launcher Activity has no SDLActivity yet.
  if (file.is_absolute()) {
    auto *input = std::fopen(file.string().c_str(), "rb");
    if (!input)
      throw FilesystemError(ErrorCode::IOError,
                            "Cannot open local file: " + file.string());
    std::fclose(input);
  }
#endif
  const auto filename = file.u8string();
  io_ = SDL_IOFromFile(filename.c_str(), "rb");
  if (!io_)
    throw FilesystemError(ErrorCode::IOError,
                          "Cannot open local file: " + filename +
                              ": " + SDL_GetError());
}

SDLFileSource::~SDLFileSource() {
  if (io_)
    SDL_CloseIO(io_);
}

std::int64_t SDLFileSource::read(void *destination, std::size_t count) {
  if (count == 0)
    return 0;
  if (!destination || count > static_cast<std::size_t>(INT64_MAX))
    return -1;
  const auto amount = SDL_ReadIO(io_, destination, count);
  if (amount)
    return static_cast<std::int64_t>(amount);
  return SDL_GetIOStatus(io_) == SDL_IO_STATUS_EOF ? 0 : -1;
}

std::int64_t SDLFileSource::seek(std::int64_t offset, SeekOrigin origin) {
  // SDL's Windows file backend may accept a negative absolute seek by
  // clamping it to zero. A FileHandle must instead reject out-of-range
  // offsets, without changing its current cursor.
  std::int64_t base = 0;
  if (origin == SeekOrigin::Current)
    base = SDL_TellIO(io_);
  else if (origin == SeekOrigin::End)
    base = SDL_GetIOSize(io_);
  if (base < 0 || offset < -base ||
      offset > std::numeric_limits<std::int64_t>::max() - base)
    return -1;
  SDL_IOWhence whence = SDL_IO_SEEK_SET;
  if (origin == SeekOrigin::Current)
    whence = SDL_IO_SEEK_CUR;
  else if (origin == SeekOrigin::End)
    whence = SDL_IO_SEEK_END;
  return SDL_SeekIO(io_, offset, whence);
}

std::int64_t SDLFileSource::tell() const { return SDL_TellIO(io_); }
std::int64_t SDLFileSource::size() const { return SDL_GetIOSize(io_); }

} // namespace Kotonoha::Filesystem
