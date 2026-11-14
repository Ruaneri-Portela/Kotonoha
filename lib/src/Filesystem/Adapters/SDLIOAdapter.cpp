#include <Kotonoha/Filesystem/Adapters/SDLIOAdapter.hpp>
#include <Kotonoha/Filesystem/IMount.hpp>

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>

namespace Kotonoha::Filesystem {
namespace {

struct Context {
  std::unique_ptr<FileHandle> file;
};

Sint64 SDLCALL fileSize(void *data) {
  try {
    return static_cast<Context *>(data)->file->size();
  } catch (...) {
    return -1;
  }
}

Sint64 SDLCALL fileSeek(void *data, Sint64 offset, SDL_IOWhence whence) {
  SeekOrigin origin = SeekOrigin::Begin;
  switch (whence) {
  case SDL_IO_SEEK_SET: break;
  case SDL_IO_SEEK_CUR: origin = SeekOrigin::Current; break;
  case SDL_IO_SEEK_END: origin = SeekOrigin::End; break;
  default: return -1;
  }
  try {
    return static_cast<Context *>(data)->file->seek(offset, origin);
  } catch (...) {
    return -1;
  }
}

size_t SDLCALL fileRead(void *data, void *dst, size_t amount,
                       SDL_IOStatus *status) {
  auto &input = *static_cast<Context *>(data)->file;
  auto *bytes = static_cast<std::uint8_t *>(dst);
  size_t read = 0;
  try {
    while (read < amount) {
      const auto got = input.read(bytes + read, amount - read);
      if (got < 0) {
        *status = SDL_IO_STATUS_ERROR;
        break;
      }
      if (got == 0) {
        *status = SDL_IO_STATUS_EOF;
        break;
      }
      read += static_cast<size_t>(got);
    }
  } catch (const FilesystemError &error) {
    *status = error.code() == ErrorCode::WouldBlock
                  ? SDL_IO_STATUS_NOT_READY : SDL_IO_STATUS_ERROR;
  } catch (...) {
    *status = SDL_IO_STATUS_ERROR;
  }
  return read;
}

size_t SDLCALL fileWrite(void *, const void *, size_t, SDL_IOStatus *status) {
  *status = SDL_IO_STATUS_READONLY;
  return 0;
}

bool SDLCALL fileFlush(void *, SDL_IOStatus *) { return true; }
bool SDLCALL fileClose(void *data) {
  delete static_cast<Context *>(data);
  return true;
}

} // namespace

SDL_IOStream *openSDLIO(std::unique_ptr<FileHandle> file) {
  if (!file)
    return nullptr;
  auto context = std::make_unique<Context>();
  context->file = std::move(file);
  SDL_IOStreamInterface interface{};
  SDL_INIT_INTERFACE(&interface);
  interface.size = fileSize;
  interface.seek = fileSeek;
  interface.read = fileRead;
  interface.write = fileWrite;
  interface.flush = fileFlush;
  interface.close = fileClose;
  SDL_IOStream *stream = SDL_OpenIO(&interface, context.get());
  if (stream)
    context.release();
  return stream;
}

} // namespace Kotonoha::Filesystem
