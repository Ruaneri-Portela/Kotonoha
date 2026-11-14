#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <Kotonoha/Filesystem/IMount.hpp>

#include <SDL3/SDL.h>
#include <limits>

namespace Kotonoha::Filesystem {

SDLFileSource::SDLFileSource(const std::filesystem::path &file) {
  io_ = SDL_IOFromFile(file.string().c_str(), "rb");
  if (!io_)
    throw FilesystemError(ErrorCode::IOError,
                          "Cannot open local file: " + file.string() +
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
