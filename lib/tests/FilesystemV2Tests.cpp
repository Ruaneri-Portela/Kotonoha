#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Mounts/FallbackMount.hpp>
#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <Kotonoha/Filesystem/Adapters/SDLIOAdapter.hpp>
#include <Kotonoha/Filesystem/Adapters/BrowserBlobAdapter.hpp>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <Kotonoha/Filesystem/IMount.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace VFS = Kotonoha::Filesystem;

void require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}

struct Temp {
  fs::path root;
  Temp() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::temp_directory_path() /
           ("kotonoha-v2-" + std::to_string(tick));
    fs::create_directories(root);
  }
  ~Temp() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

void writeFile(const fs::path &file, const std::vector<std::uint8_t> &bytes) {
  fs::create_directories(file.parent_path());
  auto *output = SDL_IOFromFile(file.string().c_str(), "wb");
  require(output != nullptr, "Cannot create test fixture");
  const bool ok = bytes.empty() ||
                  SDL_WriteIO(output, bytes.data(), bytes.size()) == bytes.size();
  const bool closed = SDL_CloseIO(output);
  require(ok && closed, "Cannot write test fixture");
}

void writeFile(const fs::path &file, const std::string &text) {
  writeFile(file, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string readText(std::unique_ptr<VFS::FileHandle> file) {
  require(file != nullptr, "FileHandle is null");
  auto data = VFS::readAll(*file);
  return {data.begin(), data.end()};
}

template <class Action>
void expectCode(VFS::ErrorCode code, Action run) {
  bool caught = false;
  try {
    run();
  } catch (const VFS::FilesystemError &error) {
    caught = error.code() == code;
  }
  require(caught, "Incorrect filesystem error");
}

void putLE32(std::vector<std::uint8_t> &bytes, std::size_t at,
             std::uint32_t value) {
  for (int index = 0; index != 4; ++index)
    bytes[at + index] = static_cast<std::uint8_t>(value >> (index * 8));
}

void makeISO(const fs::path &file) {
  constexpr std::size_t block = 2048;
  std::vector<std::uint8_t> image(block * 22, 0);
  auto *pvd = image.data() + block * 16;
  pvd[0] = 1;
  std::memcpy(pvd + 1, "CD001", 5);
  putLE32(image, block * 16 + 156 + 2, 20);
  putLE32(image, block * 16 + 156 + 10, block);

  auto *terminator = image.data() + block * 17;
  terminator[0] = 255;
  std::memcpy(terminator + 1, "CD001", 5);

  auto *directory = image.data() + block * 20;
  const char name[] = "HELLO.TXT;1";
  directory[0] = 33 + sizeof(name) - 1;
  putLE32(image, block * 20 + 2, 21);
  putLE32(image, block * 20 + 10, 5);
  directory[25] = 0;
  directory[32] = sizeof(name) - 1;
  std::memcpy(directory + 33, name, sizeof(name) - 1);
  std::memcpy(image.data() + block * 21, "hello", 5);
  writeFile(file, image);
}

void testLocalAndOverlays() {
  Temp temp;
  const auto base = temp.root / "base";
  const auto patch = temp.root / "patch";
  const auto other = temp.root / "other";
  writeFile(base / "DATA/data.pak", "original-package");
  writeFile(base / "DATA/PHOTO.crgb", "original-logical");
  writeFile(base / "DATA/BaseOnly.crgb", "original-only");
  writeFile(base / "MASK/child.bin", "lower-child");
  writeFile(base / "empty.dat", "");
  writeFile(patch / "DATA/data.pak", "replacement-package");
  writeFile(patch / "DATA/PHOTO.crgb", "replacement-logical");
  writeFile(patch / "MASK", "higher-file");
  writeFile(other / "DATA/data.pak", "highest-package");

  auto files = VFS::Filesystem::mount(base.string());
  require(!files->tryOpenLogicalOverride("DATA/PHOTO.crgb"),
          "Base loose file must not masquerade as an overlay");
  require(readText(files->open("data/DATA.PAK")) == "original-package",
          "Case-insensitive base read failed");
  require(files->stat("DATA").type == VFS::FileType::Directory,
          "Base directory not indexed");
  require(files->list("").size() == 3, "Unexpected base root listing");

  auto independent = files->open("DATA/data.pak");
  auto same = files->open("DATA/data.pak");
  char a = 0, b = 0;
  require(independent->read(&a, 1) == 1 &&
              independent->read(&a, 1) == 1 &&
              same->read(&b, 1) == 1 && b == 'o',
          "Open handles shared a cursor");
  require(independent->seek(0, VFS::SeekOrigin::Begin) == 0 &&
              independent->tell() == 0,
          "Seek/tell failure");
  require(independent->seek(-1, VFS::SeekOrigin::End) == 15,
          "Seek relative to end failed");
  require(independent->seek(-100, VFS::SeekOrigin::Begin) < 0,
          "Invalid seek accepted");
  require(readText(files->open("empty.dat")).empty(),
          "Empty asset read failed");

  files->addOverlay(patch.string());
  require(files->generation() == 1, "Overlay generation not updated");
  require(readText(files->open("DATA/data.pak")) == "replacement-package",
          "Source-package override lost");
  require(readText(files->tryOpenLogicalOverride("DATA/PHOTO.crgb")) ==
              "replacement-logical", "Logical override lost");
  require(!files->tryOpenLogicalOverride("DATA/BaseOnly.crgb"),
          "Base data must never be an override");
  require(readText(files->open("DATA/BaseOnly.crgb")) == "original-only",
          "Layer fallback failed");
  require(files->list("DATA").size() == 3, "Layer merge is not unique");
  require(!files->exists("MASK/child.bin"),
          "Higher file must hide lower directory contents");
  expectCode(VFS::ErrorCode::NotDirectory,
             [&] { files->list("MASK"); });

  files->addOverlay(other.string(), 200);
  require(files->generation() == 2 &&
              readText(files->open("DATA/data.pak")) == "highest-package",
          "Priority override lost");

  expectCode(VFS::ErrorCode::InvalidPath,
             [&] { files->exists("../outside"); });
  expectCode(VFS::ErrorCode::InvalidPath,
             [&] { files->open("/absolute.txt"); });
  expectCode(VFS::ErrorCode::InvalidPath,
             [&] { files->open("a\\..\\outside"); });

  auto retained = files->open("DATA/data.pak");
  files.reset();
  require(readText(std::move(retained)) == "highest-package",
          "Open handle did not survive mount lifetime");
}

void testISO() {
  Temp temp;
  const auto image = temp.root / "sample.iso";
  makeISO(image);
  auto iso = VFS::Filesystem::mount(image.string());
  require(iso->exists("hello.txt"), "ISO entry lookup failed");
  require(iso->stat("HELLO.TXT").size == 5, "Incorrect ISO size");
  require(iso->list("").size() == 1, "Incorrect ISO root listing");
  auto first = iso->open("HELLO.TXT");
  auto second = iso->open("hello.txt");
  char bytes[8]{};
  require(first->read(bytes, 2) == 2 && std::string(bytes, 2) == "he",
          "ISO stream read failed");
  require(second->read(bytes, 5) == 5 &&
              std::string(bytes, 5) == "hello",
          "ISO handles share a cursor");
  require(first->seek(1, VFS::SeekOrigin::Current) == 3 &&
              first->read(bytes, 2) == 2 &&
              std::string(bytes, 2) == "lo" &&
              first->read(bytes, 2) == 0,
          "ISO seek or EOF failed");
}


void putOctal(std::array<char, 512> &header, std::size_t at,
              std::size_t width, std::uint64_t value) {
  auto current = value;
  for (std::size_t i = 0; i < width - 1; ++i) {
    header[at + width - 2 - i] =
        static_cast<char>('0' + (current & 7));
    current >>= 3;
  }
  require(current == 0, "TAR fixture overflow");
  header[at + width - 1] = '\0';
}

void addTar(std::vector<std::uint8_t> &out,
            const std::string &name,
            const std::string &data,
            char type = '0') {
  std::array<char, 512> header{};
  require(name.size() < 100, "TAR test path too long");
  std::memcpy(header.data(), name.data(), name.size());
  putOctal(header, 100, 8, 0644);
  putOctal(header, 108, 8, 0);
  putOctal(header, 116, 8, 0);
  putOctal(header, 124, 12, data.size());
  putOctal(header, 136, 12, 0);
  std::memset(header.data() + 148, ' ', 8);
  header[156] = type;
  std::memcpy(header.data() + 257, "ustar", 5);
  std::uint64_t checksum = 0;
  for (auto value : header)
    checksum += static_cast<std::uint8_t>(value);
  putOctal(header, 148, 8, checksum);
  out.insert(out.end(), header.begin(), header.end());
  out.insert(out.end(), data.begin(), data.end());
  out.resize((out.size() + 511) / 512 * 512, 0);
}

void testTAR() {
  Temp temp;
  const auto file = temp.root / "sample.tar";
  std::vector<std::uint8_t> bytes;
  addTar(bytes, "DATA/", "", '5');
  addTar(bytes, "DATA/example.txt", "tar-content");
  addTar(bytes, "empty.txt", "");
  bytes.resize(bytes.size() + 1024, 0);
  writeFile(file, bytes);
  auto tar = VFS::Filesystem::mount(file.string());
  require(tar->stat("DATA").type == VFS::FileType::Directory,
          "Missing TAR directory");
  require(tar->list("DATA").size() == 1, "Bad TAR listing");
  require(readText(tar->open("data/EXAMPLE.TXT")) == "tar-content",
          "TAR random-access file failed");
  require(readText(tar->open("empty.txt")).empty(),
          "Empty TAR entry failed");
  auto first = tar->open("DATA/example.txt");
  require(first->seek(-7, VFS::SeekOrigin::End) == 4,
          "TAR seek from end failed");
  char suffix[8]{};
  require(first->read(suffix, 7) == 7 &&
              std::string(suffix, 7) == "content",
          "TAR slice cursor failed");

  bytes[148] ^= 1; // corrupt checksum
  writeFile(file, bytes);
  expectCode(VFS::ErrorCode::CorruptImage, [&] {
    VFS::Filesystem::mount(file.string());
  });
}


void testRemoteManifest() {
  auto remote = VFS::FetchAssetProvider::fromManifest(
      "https://example.invalid/virtual",
      R"({"entries":[{"path":"DATA/REMOTE.BIN","size":4},{"path":"EMPTY","size":0}]})");
  require(remote->exists("data/remote.bin") &&
              remote->stat("DATA/remote.bin").size == 4,
          "Remote manifest lookup failed");
  require(remote->list(std::string_view{"DATA"}).size() == 1,
          "Remote manifest directory list failed");
  auto files = VFS::Filesystem::mount(
      std::static_pointer_cast<VFS::IMount>(remote));
  auto handle = files->open("DATA/REMOTE.BIN");
  char chunk[4];
  expectCode(VFS::ErrorCode::WouldBlock, [&] {
    handle->read(chunk, 4);
  });
  require(files->open("EMPTY")->read(chunk, 4) == 0,
          "Manifest empty remote file is not EOF");
  expectCode(VFS::ErrorCode::InvalidPath, [&] {
    VFS::FetchAssetProvider::fromManifest(
        "https://example.invalid/",
        R"({"entries":[{"path":"../escape","size":2}]})");
  });
  expectCode(VFS::ErrorCode::InvalidPath, [&] {
    VFS::FetchAssetProvider::fromManifest(
        "https://example.invalid/",
        R"({"entries":[{"path":"A.TXT","size":2},{"path":"a.txt","size":3}]})");
  });
}

void testSDLAdapters() {
  Temp tmp;
  auto path = tmp.root / "adapter.bin";
  {
    VFS::OutputFile out(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(out), "SDL output adapter did not open");
    out.write("virtual-data", 12);
    out.close();
    require(static_cast<bool>(out), "SDL output adapter failed to close");
  }
  {
    VFS::InputFile input(path, std::ios::binary | std::ios::ate);
    require(input.tellg() == 12, "SDL input adapter ate/size failed");
    input.seekg(-4, std::ios::end);
    char suffix[4]{};
    input.read(suffix, 4);
    require(std::string(suffix, 4) == "data",
            "SDL adapter seek and read failed");
  }
  auto file = std::make_unique<VFS::SDLFileSource>(path);
  SDL_IOStream *io = VFS::openSDLIO(std::move(file));
  require(io && SDL_GetIOSize(io) == 12, "Custom SDL_IOStream size failed");
  require(SDL_SeekIO(io, 8, SDL_IO_SEEK_SET) == 8,
          "Custom SDL_IOStream seek failed");
  char tail[5]{};
  require(SDL_ReadIO(io, tail, 4) == 4 &&
              std::string(tail, 4) == "data",
          "Custom SDL_IOStream read failed");
  require(SDL_CloseIO(io), "Custom SDL_IOStream failed to close");
#ifndef __EMSCRIPTEN__
  expectCode(VFS::ErrorCode::UnsupportedMount, [&] {
    VFS::BrowserBlobAdapter blob(
        std::make_unique<VFS::SDLFileSource>(path), "video/webm");
  });
#endif
}


void testLocalHttpFallback() {
  Temp temp;
  const auto assets = temp.root / "game-assets";
  writeFile(assets / "valid.txt", "local");
  writeFile(assets / "wrong.txt", "x");
  writeFile(assets / "extra.txt", "original");
  const auto remote = VFS::FetchAssetProvider::fromManifest(
      "https://example.invalid/game-assets/",
      R"({"entries":[{"path":"valid.txt","size":5},{"path":"wrong.txt","size":7},{"path":"missing.txt","size":4}]})");
  auto merged = VFS::Filesystem::mount(
      std::make_shared<VFS::FallbackMount>(
          std::make_shared<VFS::DirectoryMount>(assets), remote));
  require(readText(merged->open("VALID.TXT")) == "local",
          "Valid local file was not preferred");
  require(readText(merged->open("extra.txt")) == "original",
          "Local-only file went missing");
  require(merged->stat("wrong.txt").size == 7,
          "Corrupt local file was not replaced by manifest entry");
  require(merged->exists("missing.txt") &&
          merged->list("").size() == 4,
          "Remote-only file was not included in merged directory");
  char byte{};
  expectCode(VFS::ErrorCode::WouldBlock, [&] {
    merged->open("wrong.txt")->read(&byte, 1);
  });
  VFS::setRemoteGameAssets(remote);
  auto selected = VFS::mountGameAssets(assets);
  require(readText(selected->open("valid.txt")) == "local" &&
          selected->stat("missing.txt").size == 4,
          "Browser-style local/HTTP selection failed");
  auto remoteOnly = VFS::mountGameAssets(temp.root / "missing");
  require(remoteOnly->exists("missing.txt"),
          "Missing directory did not fall back to remote manifest");
  VFS::setRemoteGameAssets({});
  expectCode(VFS::ErrorCode::NotFound, [&] {
    VFS::mountGameAssets(temp.root / "missing");
  });
}

void testDirectorySafety() {
  Temp temp;
  auto base = temp.root / "base";
  writeFile(base / "abc.bin", "one");
  writeFile(base / "ABC.bin", "two");
  if (!fs::equivalent(base / "abc.bin", base / "ABC.bin")) {
    expectCode(VFS::ErrorCode::InvalidPath, [&] {
      VFS::Filesystem::mount(base.string());
    });
  }

  auto outside = temp.root / "outside.bin";
  writeFile(outside, "secret");
  std::error_code error;
  fs::create_symlink(outside, base / "escape.bin", error);
  if (!error) {
    expectCode(VFS::ErrorCode::InvalidPath, [&] {
      VFS::Filesystem::mount(base.string());
    });
  }
}

} // namespace

int main() {
  try {
    testLocalAndOverlays();
    testISO();
    testTAR();
    testRemoteManifest();
    testLocalHttpFallback();
    testSDLAdapters();
    testDirectorySafety();
    std::cout << "Filesystem V2 tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << "Filesystem V2 tests failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
