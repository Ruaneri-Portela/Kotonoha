#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Kotonoha::Filesystem {

void InputFile::Buffer::reset(std::unique_ptr<FileHandle> file) {
  file_ = std::move(file);
  setg(nullptr, nullptr, nullptr);
}

InputFile::Buffer::int_type InputFile::Buffer::underflow() {
  if (gptr() && gptr() < egptr())
    return traits_type::to_int_type(*gptr());
  if (!file_) return traits_type::eof();
  const auto n = file_->read(buffer_.data(), buffer_.size());
  if (n <= 0) return traits_type::eof();
  setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
  return traits_type::to_int_type(*gptr());
}

std::streamsize InputFile::Buffer::xsgetn(char_type *data,
                                          std::streamsize count) {
  std::streamsize total = 0;
  while (total < count) {
    if (!gptr() || gptr() == egptr()) {
      if (underflow() == traits_type::eof()) break;
    }
    const auto available = std::min<std::streamsize>(
        count - total, egptr() - gptr());
    traits_type::copy(data + total, gptr(), available);
    gbump(static_cast<int>(available));
    total += available;
  }
  return total;
}

InputFile::Buffer::pos_type InputFile::Buffer::seekoff(
    off_type off, std::ios_base::seekdir dir,
    std::ios_base::openmode mode) {
  if (!file_ || !(mode & std::ios::in))
    return pos_type(off_type(-1));
  SeekOrigin origin = SeekOrigin::Begin;
  if (dir == std::ios::cur) {
    origin = SeekOrigin::Current;
    if (gptr()) off -= static_cast<off_type>(egptr() - gptr());
  } else if (dir == std::ios::end) {
    origin = SeekOrigin::End;
  } else if (dir != std::ios::beg) {
    return pos_type(off_type(-1));
  }
  const auto pos = file_->seek(off, origin);
  if (pos < 0)
    return pos_type(off_type(-1));
  setg(nullptr, nullptr, nullptr);
  return pos_type(pos);
}

InputFile::Buffer::pos_type InputFile::Buffer::seekpos(
    pos_type pos, std::ios_base::openmode mode) {
  return seekoff(static_cast<off_type>(pos), std::ios::beg, mode);
}

InputFile::InputFile() : std::istream(nullptr) { rdbuf(&buffer_); }
InputFile::InputFile(const std::filesystem::path &file,
                     std::ios_base::openmode mode) : InputFile() {
  open(file, mode);
}
InputFile::InputFile(std::unique_ptr<FileHandle> file) : InputFile() {
  buffer_.reset(std::move(file));
  if (!buffer_.isOpen()) setstate(std::ios::failbit);
}
void InputFile::open(const std::filesystem::path &path,
                     std::ios_base::openmode mode) {
  close();
  if (mode & std::ios::out) {
    setstate(std::ios::failbit);
    return;
  }
  try {
    auto file = std::make_unique<SDLFileSource>(path);
    if (mode & std::ios::ate)
      if (file->seek(0, SeekOrigin::End) < 0) {
        setstate(std::ios::failbit);
        return;
      }
    buffer_.reset(std::move(file));
    clear();
  } catch (const std::exception &) {
    setstate(std::ios::failbit);
  }
}
void InputFile::close() {
  buffer_.reset({});
  clear();
}

bool OutputFile::Buffer::open(const std::filesystem::path &path,
                              std::ios_base::openmode mode) {
  close();
  const auto name = path.string();
  stream_ = SDL_IOFromFile(name.c_str(),
                           (mode & std::ios::app) ? "ab" : "wb");
  failure_ = !stream_;
  return !failure_;
}
OutputFile::Buffer::int_type OutputFile::Buffer::overflow(int_type ch) {
  if (ch == traits_type::eof())
    return sync() ? traits_type::eof() : traits_type::not_eof(ch);
  const char byte = traits_type::to_char_type(ch);
  return xsputn(&byte, 1) == 1 ? ch : traits_type::eof();
}
std::streamsize OutputFile::Buffer::xsputn(const char_type *data,
                                           std::streamsize count) {
  if (!stream_ || count < 0) return 0;
  const auto got = SDL_WriteIO(stream_, data,
                               static_cast<std::size_t>(count));
  if (got != static_cast<std::size_t>(count)) failure_ = true;
  return static_cast<std::streamsize>(got);
}
int OutputFile::Buffer::sync() {
  if (!stream_ || !SDL_FlushIO(stream_)) {
    failure_ = true;
    return -1;
  }
  return 0;
}
bool OutputFile::Buffer::close() {
  if (!stream_) return !failure_;
  const bool closed = SDL_CloseIO(stream_);
  stream_ = nullptr;
  if (!closed) failure_ = true;
  return !failure_;
}
OutputFile::OutputFile() : std::ostream(nullptr) { rdbuf(&buffer_); }
OutputFile::OutputFile(const std::filesystem::path &file,
                       std::ios_base::openmode mode) : OutputFile() {
  open(file, mode);
}
void OutputFile::open(const std::filesystem::path &path,
                      std::ios_base::openmode mode) {
  close();
  if ((mode & std::ios::in) || !buffer_.open(path, mode))
    setstate(std::ios::failbit);
  else
    clear();
}
void OutputFile::close() {
  if (buffer_.isOpen()) {
    if (flush().fail() || !buffer_.close())
      setstate(std::ios::badbit);
  }
}
} // namespace Kotonoha::Filesystem
