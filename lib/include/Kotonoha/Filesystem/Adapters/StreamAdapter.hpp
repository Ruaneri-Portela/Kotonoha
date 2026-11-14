#pragma once

#include <Kotonoha/Filesystem/FileHandle.hpp>

#include <filesystem>
#include <ios>
#include <istream>
#include <iterator>
#include <memory>
#include <ostream>
#include <streambuf>
#include <array>

struct SDL_IOStream;

namespace Kotonoha::Filesystem {

// Migration adapter: standard C++ *parsing*, but never std::ifstream.
// Accepts virtual FileHandles, or physical paths through the SDL3 backend.
// The path constructor is transitional; providers should pass fs.open(id).
class InputFile final : public std::istream {
  class Buffer final : public std::streambuf {
    std::unique_ptr<FileHandle> file_;
    std::array<char, 8192> buffer_{};
  protected:
    int_type underflow() override;
    std::streamsize xsgetn(char_type *data, std::streamsize count) override;
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode mode) override;
    pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override;
  public:
    void reset(std::unique_ptr<FileHandle> file);
    bool isOpen() const noexcept { return static_cast<bool>(file_); }
  } buffer_;

public:
  InputFile();
  explicit InputFile(const std::filesystem::path &path,
                     std::ios_base::openmode mode = std::ios::in);
  explicit InputFile(std::unique_ptr<FileHandle> file);
  ~InputFile() override = default;
  InputFile(const InputFile &) = delete;
  InputFile &operator=(const InputFile &) = delete;
  void open(const std::filesystem::path &path,
            std::ios_base::openmode mode = std::ios::in);
  void close();
  bool is_open() const noexcept { return buffer_.isOpen(); }
};

// Persistence adapter for legacy save/configuration writers. This is NOT a
// writable virtual asset mount and is not used by package/ISO readers.
class OutputFile final : public std::ostream {
  class Buffer final : public std::streambuf {
    SDL_IOStream *stream_ = nullptr;
    bool failure_ = false;
  protected:
    int_type overflow(int_type ch) override;
    std::streamsize xsputn(const char_type *data, std::streamsize count) override;
    int sync() override;
  public:
    bool open(const std::filesystem::path &path,
              std::ios_base::openmode mode);
    bool close();
    bool isOpen() const noexcept { return stream_ != nullptr; }
    ~Buffer() override { close(); }
  } buffer_;

public:
  OutputFile();
  explicit OutputFile(const std::filesystem::path &path,
                      std::ios_base::openmode mode = std::ios::out);
  ~OutputFile() override { close(); }
  OutputFile(const OutputFile &) = delete;
  OutputFile &operator=(const OutputFile &) = delete;
  void open(const std::filesystem::path &path,
            std::ios_base::openmode mode = std::ios::out);
  void close();
  bool is_open() const noexcept { return buffer_.isOpen(); }
};

} // namespace Kotonoha::Filesystem
