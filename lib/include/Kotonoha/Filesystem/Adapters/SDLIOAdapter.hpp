#pragma once
#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <memory>

struct SDL_IOStream;
namespace Kotonoha::Filesystem {

// SDL3 owns and closes this stream. Closing it deletes the backing FileHandle
// exactly once; never pass the handle to another owner after this call.
SDL_IOStream *openSDLIO(std::unique_ptr<FileHandle> file);

} // namespace Kotonoha::Filesystem
