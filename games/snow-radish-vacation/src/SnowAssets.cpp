#include "SnowAssets.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace SnowRadish {
namespace {

constexpr int kSampleRate = 22050; // WAVEFORMATEX of every ADPS / ADP4 stream

std::filesystem::path FindIgnoreCase(const std::filesystem::path &dir, const std::string &file) {
  std::error_code ec;
  const std::string want = SnowAssets::Lower(file);
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    if (entry.is_regular_file(ec) && SnowAssets::Lower(entry.path().filename().string()) == want) return entry.path();
  return {};
}

std::vector<uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// The directory sits at the start of the pack: scan the first 2 MB, then all of it.
std::map<std::string, PackEntry> ReadDirectory(const std::filesystem::path &path, bool byStem) {
  std::map<std::string, PackEntry> index;
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return index;
  const size_t size = size_t(in.tellg());
  std::vector<uint8_t> head(std::min<size_t>(size, 2 * 1024 * 1024));
  in.seekg(0);
  in.read(reinterpret_cast<char *>(head.data()), std::streamsize(head.size()));
  auto entries = readDirectory(head);
  if (entries.empty() && head.size() < size) {
    head.resize(size);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(head.data()), std::streamsize(size));
    entries = readDirectory(head);
  }
  for (auto &entry : entries) {
    std::string key = SnowAssets::Lower(entry.name);
    if (byStem) key = key.substr(0, key.find('.'));
    index[key] = entry;
  }
  return index;
}

Kotonoha::SurfacePtr RgbaSurface(int w, int h, const uint8_t *pixels, SDL_PixelFormat format, int bpp) {
  if (w <= 0 || h <= 0) return {};
  Kotonoha::SurfacePtr surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  if (!surface) throw std::runtime_error(SDL_GetError());
  if (format == SDL_PIXELFORMAT_RGBA32) {
    for (int y = 0; y < h; ++y)
      std::memcpy(static_cast<uint8_t *>(surface->pixels) + size_t(y) * size_t(surface->pitch),
                  pixels + size_t(y) * size_t(w) * size_t(bpp), size_t(w) * 4);
  } else if (!SDL_ConvertPixels(w, h, format, pixels, w * bpp, SDL_PIXELFORMAT_RGBA32, surface->pixels,
                                surface->pitch)) {
    throw std::runtime_error(SDL_GetError());
  }
  return surface;
}

Kotonoha::AudioClip Clip(const std::vector<int16_t> &samples, int channels) {
  Kotonoha::AudioClip clip;
  clip.spec = {SDL_AUDIO_S16, channels, kSampleRate};
  clip.data.resize(samples.size() * sizeof(int16_t));
  std::memcpy(clip.data.data(), samples.data(), clip.data.size());
  return clip;
}

} // namespace

std::string SnowAssets::Lower(std::string s) {
  for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

SnowAssets::SnowAssets(std::filesystem::path directory) : directory_(std::move(directory)) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory_, ec))
    throw std::runtime_error("Snow Radish data directory not found: " + directory_.string());
  scriptPath_ = FindIgnoreCase(directory_, "SCRIPT.DAT");
  graphic_.path = FindIgnoreCase(directory_, "GRAPHIC.DAT");
  sound_.path = FindIgnoreCase(directory_, "SOUND.DAT");
  voice_.path = FindIgnoreCase(directory_, "VOICE.DAT");
  if (scriptPath_.empty() || graphic_.path.empty())
    throw std::runtime_error("SCRIPT.DAT / GRAPHIC.DAT not found in " + directory_.string());
  graphic_.index = ReadDirectory(graphic_.path, false);
  if (graphic_.index.empty()) throw std::runtime_error("GRAPHIC.DAT has no entries: " + graphic_.path.string());
  if (!sound_.path.empty()) sound_.index = ReadDirectory(sound_.path, true);
  if (!voice_.path.empty()) voice_.index = ReadDirectory(voice_.path, true);
}

std::string SnowAssets::name() const { return "Snow Radish Vacation packs (" + directory_.string() + ")"; }

const SnowAssets::Pack *SnowAssets::PackOf(const std::string &id, std::string &key) const {
  const auto slash = id.find('/');
  if (slash == std::string::npos) return nullptr;
  const std::string kind = id.substr(0, slash);
  key = Lower(id.substr(slash + 1));
  if (kind == "graphic") return &graphic_;
  if (kind == "sound") return &sound_;
  if (kind == "voice") return &voice_;
  return nullptr;
}

bool SnowAssets::contains(const std::string &id) const {
  if (id == "script") return true;
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (!pack) return false;
  if (const auto hash = key.find('#'); hash != std::string::npos && pack == &graphic_) {
    const auto &info = AnimationOf(key.substr(0, key.rfind(".anm")));
    const int frame = std::atoi(key.c_str() + hash + 1);
    return frame >= 0 && size_t(frame) < info.frames.size();
  }
  return pack->index.count(key) != 0;
}

std::vector<uint8_t> SnowAssets::Extract(const Pack &pack, const PackEntry &entry) {
  std::ifstream in(pack.path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  const size_t size = size_t(in.tellg());
  if (entry.offset >= size) return {};
  const size_t avail = std::min<size_t>(entry.compressedSize, size - entry.offset);
  std::vector<uint8_t> packed(avail);
  in.seekg(std::streamoff(entry.offset));
  in.read(reinterpret_cast<char *>(packed.data()), std::streamsize(avail));
  const bool known = entry.rawSize != 0;
  if (known && entry.compressedSize == entry.rawSize) return packed; // stored
  std::vector<uint8_t> out(known ? entry.rawSize : avail * 10);
  lzssDecode(packed.data(), avail, out.data(), out.size());
  if (!known)
    while (!out.empty() && out.back() == 0) out.pop_back();
  return out;
}

std::vector<Uint8> SnowAssets::read(const std::string &id) const {
  if (id == "script") {
    auto bytes = ReadFile(scriptPath_);
    if (bytes.empty()) throw std::runtime_error("Cannot read " + scriptPath_.string());
    return bytes;
  }
  std::string key;
  const Pack *pack = PackOf(id, key);
  const auto it = pack ? pack->index.find(key) : decltype(graphic_.index)::const_iterator{};
  if (!pack || it == pack->index.end()) throw std::runtime_error("Snow Radish asset not found: " + id);
  return Extract(*pack, it->second);
}

std::vector<std::string> SnowAssets::list(const std::string &prefix) const {
  std::vector<std::string> ids;
  if (std::string("script").rfind(prefix, 0) == 0) ids.push_back("script");
  for (const auto &[kind, pack] : {std::pair<const char *, const Pack *>{"graphic/", &graphic_},
                                   {"sound/", &sound_},
                                   {"voice/", &voice_}})
    for (const auto &entry : pack->index) {
      std::string id = kind + entry.first;
      if (id.rfind(prefix, 0) == 0) ids.push_back(std::move(id));
    }
  return ids;
}

std::vector<uint8_t> SnowAssets::Graphic(const std::string &file) const {
  const auto it = graphic_.index.find(Lower(file));
  return it == graphic_.index.end() ? std::vector<uint8_t>{} : Extract(graphic_, it->second);
}

bool SnowAssets::HasGraphic(const std::string &file) const { return graphic_.index.count(Lower(file)) != 0; }

Kotonoha::SurfacePtr SnowAssets::loadSurface(const std::string &id) const {
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (pack != &graphic_) return AssetProvider::loadSurface(id);
  if (const auto hash = key.find('#'); hash != std::string::npos) {
    Animation anm;
    const int frame = std::atoi(key.c_str() + hash + 1);
    if (!decodeAnm(Graphic(key.substr(0, hash)), anm) || frame < 0 || size_t(frame) >= anm.frames.size())
      throw std::runtime_error("Cannot decode animation frame " + id);
    const auto &f = anm.frames[size_t(frame)];
    return RgbaSurface(int(f.width), int(f.height), f.rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
  }
  const std::string ext = key.substr(key.rfind('.') + 1);
  if (ext == "evd" || ext == "img") {
    Image image;
    if (!decodeEvd(Graphic(key), image)) throw std::runtime_error("Cannot decode picture " + id);
    return RgbaSurface(int(image.width), int(image.height), image.rgb.data(), SDL_PIXELFORMAT_RGB24, 3);
  }
  if (ext == "chd") {
    SnowRadish::Sprite sprite;
    if (!decodeChd(Graphic(key), sprite)) throw std::runtime_error("Cannot decode sprite " + id);
    return RgbaSurface(int(sprite.width), int(sprite.height), sprite.rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
  }
  return AssetProvider::loadSurface(id);
}

Kotonoha::AudioClip SnowAssets::loadAudio(const std::string &id) const {
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (pack == &sound_) {
    AdpsTrack track;
    if (!Adps(key, track)) throw std::runtime_error("Cannot decode ADPS " + id);
    std::vector<int16_t> all = std::move(track.intro.samples);
    all.insert(all.end(), track.loop.samples.begin(), track.loop.samples.end());
    return Clip(all, 2);
  }
  if (pack == &voice_) {
    const Pcm pcm = Voice(key);
    if (pcm.samples.empty()) throw std::runtime_error("Cannot decode ADP4 " + id);
    return Clip(pcm.samples, 1);
  }
  return AssetProvider::loadAudio(id);
}

SnowAssets::Placement SnowAssets::Sprite(const std::string &name) const {
  const std::string key = Lower(name);
  if (const auto it = sprites_.find(key); it != sprites_.end()) return it->second;
  Placement out;
  SnowRadish::Sprite sprite;
  if (decodeChd(Graphic(key + ".chd"), sprite))
    out = {float(sprite.x), float(sprite.y), float(sprite.width), float(sprite.height), true};
  return sprites_[key] = out;
}

const SnowAssets::AnimationInfo &SnowAssets::AnimationOf(const std::string &name) const {
  const std::string key = Lower(name);
  if (const auto it = animations_.find(key); it != animations_.end()) return it->second;
  AnimationInfo info;
  Animation anm;
  if (decodeAnm(Graphic(key + ".anm"), anm)) {
    info.regions = std::move(anm.regions);
    info.cursor = anm.cursor;
    for (const auto &f : anm.frames)
      info.frames.push_back({float(f.x), float(f.y), float(f.width), float(f.height), true});
  }
  return animations_[key] = std::move(info);
}

bool SnowAssets::Alpha(const std::string &file, bool alternate, AlphaMask &out) const {
  return HasGraphic(file) && decodeAlpha(Graphic(file), alternate, out);
}

bool SnowAssets::Adps(const std::string &stem, AdpsTrack &out) const {
  const auto it = sound_.index.find(Lower(stem));
  return it != sound_.index.end() && decodeAdpsFile(Extract(sound_, it->second), out);
}

Pcm SnowAssets::Voice(const std::string &stem) const {
  const auto it = voice_.index.find(Lower(stem));
  return it == voice_.index.end() ? Pcm{} : decodeAdp4(Extract(voice_, it->second));
}

} // namespace SnowRadish
