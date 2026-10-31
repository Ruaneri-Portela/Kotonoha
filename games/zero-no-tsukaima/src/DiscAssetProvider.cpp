#include "DiscAssetProvider.hpp"

#include <ZeroNoTsukaima/Pss.hpp>
#include <ZeroNoTsukaima/Tim2.hpp>

#include <cstring>
#include <stdexcept>

namespace ZeroNoTsukaima {
namespace {

bool Split(const std::string &id, std::string *kind, std::string *key) {
  const auto slash = id.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= id.size()) return false;
  *kind = id.substr(0, slash);
  *key = id.substr(slash + 1);
  return true;
}

bool Number(const std::string &text, int *out) {
  if (text.empty() || text.size() > 9) return false;
  int value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  *out = value;
  return true;
}

std::shared_ptr<SceneSource> OpenSource(const std::string &discPath) {
  auto source = std::make_shared<SceneSource>();
  std::string error;
  if (!source->Open(discPath, &error)) throw std::runtime_error("Zero no Tsukaima disc: " + error);
  return source;
}

void Put32(std::vector<uint8_t> &out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

// Canonical 44-byte header + s16le samples.
std::vector<uint8_t> Wave(const std::vector<int16_t> &pcm, int rate, int channels) {
  const uint32_t bytes = uint32_t(pcm.size() * 2);
  std::vector<uint8_t> out;
  out.reserve(44 + bytes);
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  Put32(out, 36 + bytes);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  Put32(out, 16);
  Put32(out, 1 | uint32_t(channels) << 16);
  Put32(out, uint32_t(rate));
  Put32(out, uint32_t(rate * channels * 2));
  Put32(out, uint32_t(channels * 2) | 16u << 16);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  Put32(out, bytes);
  for (int16_t s : pcm) {
    out.push_back(uint8_t(s));
    out.push_back(uint8_t(uint16_t(s) >> 8));
  }
  return out;
}

bool MovieSoundtrack(const SceneSource &source, int number, std::vector<uint8_t> *out) {
  std::vector<uint8_t> pss;
  PssMovie movie;
  if (!source.LoadMovie(number, &pss) || !DemuxPss(pss.data(), pss.size(), &movie) || movie.pcm.empty() ||
      movie.rate <= 0 || movie.channels <= 0)
    return false;
  *out = Wave(movie.pcm, movie.rate, movie.channels);
  return true;
}

} // namespace

DiscAssetProvider::DiscAssetProvider(const std::string &discPath) : DiscAssetProvider(OpenSource(discPath), discPath) {}

DiscAssetProvider::DiscAssetProvider(std::shared_ptr<SceneSource> source, const std::string &discPath)
    : Kotonoha::DiscImageAssetProvider(source->Disc()), source_(std::move(source)), path_(discPath) {}

std::string DiscAssetProvider::name() const { return "zero-no-tsukaima-disc"; }

bool DiscAssetProvider::Read(const std::string &id, std::vector<uint8_t> *out) const {
  std::string kind, key;
  int n = -1;
  if (!Split(id, &kind, &key)) return false;
  if (kind == "scenedat") return Number(key, &n) && source_->ReadPicture(n, out);
  if (kind == "normal") return source_->ReadNormalPicture(source_->NormalIndex(key), out);
  if (kind == "voice") return Number(key, &n) && n > 0 && source_->LoadVoice(size_t(n), out);
  if (kind == "sound") {
    const int index = source_->SoundIndex(key);
    return index >= 0 && source_->LoadSound(size_t(index), out);
  }
  if (kind == "movie") {
    const auto dot = key.find(".wav");
    if (dot != std::string::npos && dot + 4 == key.size())
      return Number(key.substr(0, dot), &n) && MovieSoundtrack(*source_, n, out);
    return Number(key, &n) && source_->LoadMovie(n, out);
  }
  if (kind == "script") {
    const auto &names = source_->Names();
    for (size_t i = 0; i < names.size(); ++i)
      if (names[i] == key) return source_->LoadScene(i, out);
  }
  return false;
}

bool DiscAssetProvider::contains(const std::string &id) const {
  std::string kind, key;
  int n = -1;
  if (!Split(id, &kind, &key)) return DiscImageAssetProvider::contains(id);
  if (kind == "scenedat") return Number(key, &n) && size_t(n) < source_->PictureCount();
  if (kind == "normal") return source_->NormalIndex(key) >= 0;
  if (kind == "voice") return Number(key, &n) && n > 0 && size_t(n) < source_->VoiceCount();
  if (kind == "sound") return source_->SoundIndex(key) >= 0;
  if (kind == "movie") {
    const auto dot = key.find(".wav");
    if (dot != std::string::npos && dot + 4 == key.size()) key.resize(dot);
    if (Number(key, &n)) return size_t(n) < source_->MovieCount();
  }
  if (kind == "script") {
    for (const auto &name : source_->Names())
      if (name == key) return true;
  }
  return DiscImageAssetProvider::contains(id);
}

std::vector<Uint8> DiscAssetProvider::read(const std::string &id) const {
  std::vector<uint8_t> out;
  if (Read(id, &out)) return out;
  if (DiscImageAssetProvider::contains(id)) return DiscImageAssetProvider::read(id);
  throw std::runtime_error("Asset not found on the disc: " + id);
}

std::vector<std::string> DiscAssetProvider::list(const std::string &prefix) const {
  std::vector<std::string> ids;
  auto add = [&](const std::string &id) {
    if (id.compare(0, prefix.size(), prefix) == 0) ids.push_back(id);
  };
  for (size_t i = 0; i < source_->PictureCount(); ++i) add("scenedat/" + std::to_string(i));
  for (size_t i = 0; i < source_->MovieCount(); ++i) {
    add("movie/" + std::to_string(i));
    add("movie/" + std::to_string(i) + ".wav");
  }
  for (size_t i = 1; i < source_->VoiceCount(); ++i) add("voice/" + std::to_string(i));
  for (const auto &entry : source_->NormalNames()) add("normal/" + entry.first);
  for (const auto &entry : source_->SoundNames()) add("sound/" + entry.first);
  for (const auto &name : source_->Names()) add("script/" + name);
  for (auto &path : DiscImageAssetProvider::list(prefix)) ids.push_back(std::move(path));
  return ids;
}

Kotonoha::SurfacePtr DiscAssetProvider::loadSurface(const std::string &id) const {
  const std::vector<uint8_t> data = read(id);
  Image image;
  if (!DecodeTim2(data.data(), data.size(), &image)) throw std::runtime_error("Invalid TIM2 picture: " + id);
  Kotonoha::SurfacePtr surface{SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32)};
  if (!surface) throw std::runtime_error(SDL_GetError());
  for (int y = 0; y < image.height; ++y)
    std::memcpy(static_cast<uint8_t *>(surface->pixels) + size_t(y) * size_t(surface->pitch),
                image.rgba.data() + size_t(y) * size_t(image.width) * 4, size_t(image.width) * 4);
  return surface;
}

} // namespace ZeroNoTsukaima
