#include <SnowRadish/SnowAssets.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Kotonoha::Providers {
using namespace SnowRadish;
namespace {

std::filesystem::path FindIgnoreCase(const std::filesystem::path &dir,
                                     const std::string &file) {
  std::error_code ec;
  const std::string want = SnowAssets::Lower(file);
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    if (entry.is_regular_file(ec) &&
        SnowAssets::Lower(entry.path().filename().string()) == want)
      return entry.path();
  return {};
}

std::vector<uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

std::filesystem::path FindPackDirectory(const std::filesystem::path &root) {
  for (const auto &candidate : {root, root / "pack", root / "PACK",
                                root / "data" / "pack", root / "DATA" / "PACK"})
    if (!FindIgnoreCase(candidate, "SCRIPT.DAT").empty() &&
        !FindIgnoreCase(candidate, "GRAPHIC.DAT").empty())
      return candidate;
  return root;
}

std::map<std::string, PackEntry>
ReadDirectory(const std::filesystem::path &path, bool byStem) {
  std::map<std::string, PackEntry> index;
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return index;
  const size_t size = size_t(in.tellg());
  if (size < 12)
    return index;
  uint8_t footer[12];
  in.seekg(std::streamoff(size - 12));
  if (!in.read(reinterpret_cast<char *>(footer), sizeof(footer)))
    return index;
  auto word = [](const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  };
  const uint32_t packedSize = word(footer) ^ 0xf0f0f0f0u;
  const uint32_t rawSize = word(footer + 4) ^ 0xf0f0f0f0u;
  if (!packedSize || packedSize > size - 12 || rawSize < 80 ||
      rawSize > (64u << 20))
    return index;
  const size_t directoryOffset = size - 12 - packedSize;
  std::vector<uint8_t> packed(packedSize), raw(rawSize);
  in.seekg(std::streamoff(directoryOffset));
  if (!in.read(reinterpret_cast<char *>(packed.data()), packed.size()))
    return index;
  lzssDecode(packed.data(), packed.size(), raw.data(), raw.size());
  for (size_t i = 0; i + 80 <= raw.size(); i += 80) {
    const uint8_t *record = raw.data() + i;
    PackEntry entry;
    entry.id = word(record);
    entry.name.assign(reinterpret_cast<const char *>(record + 4),
                      strnlen(reinterpret_cast<const char *>(record + 4), 64));
    entry.offset = word(record + 68);
    entry.compressedSize = word(record + 72);
    entry.rawSize = word(record + 76);
    if (entry.offset > directoryOffset ||
        entry.compressedSize > directoryOffset - entry.offset)
      continue;
    std::string key = SnowAssets::Lower(entry.name);
    if (byStem)
      key = key.substr(0, key.find('.'));
    index[key] = entry;
  }
  return index;
}

Kotonoha::SurfacePtr RgbaSurface(int w, int h, const uint8_t *pixels,
                                 SDL_PixelFormat format, int bpp) {
  if (w <= 0 || h <= 0)
    return {};
  Kotonoha::SurfacePtr surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    throw std::runtime_error(SDL_GetError());
  if (format == SDL_PIXELFORMAT_RGBA32) {
    for (int y = 0; y < h; ++y)
      std::memcpy(static_cast<uint8_t *>(surface->pixels) +
                      size_t(y) * size_t(surface->pitch),
                  pixels + size_t(y) * size_t(w) * size_t(bpp), size_t(w) * 4);
  } else if (!SDL_ConvertPixels(w, h, format, pixels, w * bpp,
                                SDL_PIXELFORMAT_RGBA32, surface->pixels,
                                surface->pitch)) {
    throw std::runtime_error(SDL_GetError());
  }
  return surface;
}

Kotonoha::AudioClip Clip(const std::vector<int16_t> &samples, int channels, int rate) {
  Kotonoha::AudioClip clip;
  clip.spec = {SDL_AUDIO_S16, channels, rate};
  clip.data.resize(samples.size() * sizeof(int16_t));
  if (!clip.data.empty())
    std::memcpy(clip.data.data(), samples.data(), clip.data.size());
  return clip;
}

} // namespace

std::string SnowAssets::Lower(std::string s) {
  for (char &c : s)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

SnowAssets::SnowAssets(std::filesystem::path directory)
    : directory_(FindPackDirectory(directory)) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory_, ec))
    throw std::runtime_error("Snow Radish data directory not found: " +
                             directory_.string());
  scriptPath_ = FindIgnoreCase(directory_, "SCRIPT.DAT");
  graphic_.path = FindIgnoreCase(directory_, "GRAPHIC.DAT");
  sound_.path = FindIgnoreCase(directory_, "SOUND.DAT");
  voice_.path = FindIgnoreCase(directory_, "VOICE.DAT");
  if (scriptPath_.empty() || graphic_.path.empty())
    throw std::runtime_error("SCRIPT.DAT / GRAPHIC.DAT not found in " +
                             directory_.string());
  graphic_.index = ReadDirectory(graphic_.path, false);
  if (graphic_.index.empty())
    throw std::runtime_error("GRAPHIC.DAT has no entries: " +
                             graphic_.path.string());
  if (graphic_.index.count("titl00a.crgb")) sampleRate_ = 44100;
  if (!sound_.path.empty())
    sound_.index = ReadDirectory(sound_.path, true);
  if (!voice_.path.empty())
    voice_.index = ReadDirectory(voice_.path, true);
}

std::string SnowAssets::name() const {
  return std::string(sampleRate_ == 44100 ? "Pure Mail packs (" : "Snow Radish Vacation packs (") + directory_.string() + ")";
}

const SnowAssets::Pack *SnowAssets::PackOf(const std::string &id,
                                           std::string &key) const {
  const auto slash = id.find('/');
  if (slash == std::string::npos)
    return nullptr;
  const std::string kind = id.substr(0, slash);
  key = Lower(id.substr(slash + 1));
  if (kind == "graphic")
    return &graphic_;
  if (kind == "sound")
    return &sound_;
  if (kind == "voice")
    return &voice_;
  return nullptr;
}

bool SnowAssets::containsAsset(const std::string &id) const {
  if (id == "script")
    return true;
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (!pack)
    return false;
  if (const auto hash = key.find('#');
      hash != std::string::npos && pack == &graphic_) {
    const auto &info = AnimationOf(key.substr(0, key.rfind('.', hash)));
    const int frame = std::atoi(key.c_str() + hash + 1);
    return frame >= 0 && size_t(frame) < info.frames.size();
  }
  return pack->index.count(key) != 0 || tryAltGraphicExt(pack, key);
}

bool SnowAssets::tryAltGraphicExt(const Pack *pack,
                                  const std::string &key) const {
  // Pure Mail uses .CRGB where Snow Radish uses .evd/.img: try the alternate
  // extension before giving up.
  const std::string lower = SnowAssets::Lower(key);
  const auto dot = lower.rfind('.');
  if (pack != &graphic_ || dot == std::string::npos)
    return false;
  const std::string ext = lower.substr(dot);
  if ((ext == ".evd" || ext == ".img") && pack == &graphic_) {
    const std::string stem = lower.substr(0, lower.size() - ext.size());
    for (const char *cext : {".CRGB", ".crgb"}) {
      if (pack->index.count(stem + cext) != 0)
        return true;
    }
  }
  return false;
}

std::vector<uint8_t> SnowAssets::Extract(const Pack &pack,
                                         const PackEntry &entry) {
  std::ifstream in(pack.path, std::ios::binary | std::ios::ate);
  if (!in)
    return {};
  const size_t size = size_t(in.tellg());
  if (entry.offset >= size)
    return {};
  const size_t avail =
      std::min<size_t>(entry.compressedSize, size - entry.offset);
  std::vector<uint8_t> packed(avail);
  in.seekg(std::streamoff(entry.offset));
  in.read(reinterpret_cast<char *>(packed.data()), std::streamsize(avail));
  const bool known = entry.rawSize != 0;
  if (known && entry.compressedSize == entry.rawSize)
    return packed; // stored
  std::vector<uint8_t> out(known ? entry.rawSize : avail * 10);
  lzssDecode(packed.data(), avail, out.data(), out.size());
  if (!known)
    while (!out.empty() && out.back() == 0)
      out.pop_back();
  return out;
}

std::vector<uint8_t>
SnowAssets::readGraphicWithAltExt(const Pack *pack,
                                  const std::string &key) const {
  const std::string lower = SnowAssets::Lower(key);
  const auto dot = lower.rfind('.');
  if (pack != &graphic_ || dot == std::string::npos)
    return {};
  const std::string ext = lower.substr(dot);
  if ((ext == ".evd" || ext == ".img") && pack == &graphic_) {
    const std::string stem = lower.substr(0, lower.size() - ext.size());
    for (const char *cext : {".CRGB", ".crgb"}) {
      const auto alt = pack->index.find(stem + cext);
      if (alt != pack->index.end()) {
        auto bytes = Extract(*pack, alt->second);
        // CRGB files are raw EVD type-3 delta data without the 12-byte
        // header: prepend a fake EVD header (type=3, 800x600) so the EVD
        // decoder can process them unchanged.
        std::vector<uint8_t> withHeader(12 + bytes.size());
        struct {
          uint32_t type = 3;
          uint32_t width = 800;
          uint32_t height = 600;
        } hdr;
        std::memcpy(withHeader.data(), &hdr, 12);
        std::memcpy(withHeader.data() + 12, bytes.data(), bytes.size());
        return withHeader;
      }
    }
  }
  return {};
}

std::vector<Uint8> SnowAssets::readAsset(const std::string &id) const {
  if (id == "script") {
    auto bytes = ReadFile(scriptPath_);
    if (bytes.empty())
      throw std::runtime_error("Cannot read " + scriptPath_.string());
    return bytes;
  }
  std::string key;
  const Pack *pack = PackOf(id, key);
  const auto it =
      pack ? pack->index.find(key) : decltype(graphic_.index)::const_iterator{};
  if (!pack || it == pack->index.end()) {
    // Pure Mail uses .CRGB where Snow Radish uses .evd/.img: try the alternate
    // extension before giving up.
    if (auto alt = readGraphicWithAltExt(pack, key); !alt.empty())
      return alt;
    throw std::runtime_error("Snow Radish asset not found: " + id);
  }
  return Extract(*pack, it->second);
}

std::vector<std::string>
SnowAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> ids;
  if (std::string("script").rfind(prefix, 0) == 0)
    ids.push_back("script");
  for (const auto &[kind, pack] :
       {std::pair<const char *, const Pack *>{"graphic/", &graphic_},
        {"sound/", &sound_},
        {"voice/", &voice_}})
    for (const auto &entry : pack->index) {
      std::string id = kind + entry.first;
      if (id.rfind(prefix, 0) == 0)
        ids.push_back(std::move(id));
    }
  return ids;
}

std::vector<uint8_t> SnowAssets::Graphic(const std::string &file) const {
  const auto id = "graphic/" + file;
  return contains(id) ? read(id) : std::vector<uint8_t>{};
}
bool SnowAssets::HasGraphic(const std::string &file) const {
  return contains("graphic/" + file);
}

Kotonoha::SurfacePtr SnowAssets::loadSurfaceAsset(const std::string &id) const {
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (pack != &graphic_)
    return AssetProvider::loadSurfaceAsset(id);
  if (const auto hash = key.find('#'); hash != std::string::npos) {
    Animation anm;
    const int frame = std::atoi(key.c_str() + hash + 1);
    if (!decodeAnm(Graphic(key.substr(0, hash)), anm) || frame < 0 ||
        size_t(frame) >= anm.frames.size())
      throw std::runtime_error("Cannot decode animation frame " + id);
    const auto &f = anm.frames[size_t(frame)];
    return RgbaSurface(int(f.width), int(f.height), f.rgba.data(),
                       SDL_PIXELFORMAT_RGBA32, 4);
  }
  const std::string ext = key.substr(key.rfind('.') + 1);
  if (ext == "evd" || ext == "img" || ext == "crgb" || ext == "CRGB") {
    Image image;
    if (decodeEvd(Graphic(key), image) ||
        decodeEvd(Graphic(key), image, false)) {
      return RgbaSurface(int(image.width), int(image.height), image.rgb.data(),
                         SDL_PIXELFORMAT_RGB24, 3);
    }
    if (ext == "img") {
      const auto data = Graphic(key);
      auto decodeMask = [&](int hdr, int W, int H, bool crop) -> SurfacePtr {
        if (data.size() != size_t(hdr + W * H))
          return {};
        const uint8_t *pix = data.data() + hdr;
        int minx = 0, miny = 0, maxx = W - 1, maxy = H - 1;
        if (crop) {
          minx = W; miny = H; maxx = -1; maxy = -1;
          for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
              if (pix[y * W + x] != 0xFF) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
              }
          if (maxx < minx) { minx = 0; miny = 0; maxx = 0; maxy = 0; }
        }
        const int cw = maxx - minx + 1, ch = maxy - miny + 1;
        std::vector<uint8_t> rgba(size_t(cw) * ch * 4, 0);
        for (int y = 0; y < ch; ++y) {
          for (int x = 0; x < cw; ++x) {
            const uint8_t v = pix[(miny + y) * W + (minx + x)];
            uint8_t *o = &rgba[(size_t(y) * cw + x) * 4];
            if (v == 0xFF) {
              o[0] = o[1] = o[2] = o[3] = 0;
            } else {
              o[0] = o[1] = o[2] = 255;
              o[3] = v;
            }
          }
        }
        return RgbaSurface(cw, ch, rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
      };
      auto decodeGray = [&](int W, int H, int hdr = 0) -> SurfacePtr {
        if (data.size() != size_t(hdr + W * H))
          return {};
        const uint8_t *pix = data.data() + hdr;
        std::vector<uint8_t> rgba(size_t(W) * H * 4, 255);
        for (int i = 0; i < W * H; ++i) {
          uint8_t v = pix[i];
          rgba[i * 4 + 0] = v;
          rgba[i * 4 + 1] = v;
          rgba[i * 4 + 2] = v;
          rgba[i * 4 + 3] = 255;
        }
        return RgbaSurface(W, H, rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
      };
      auto decodeMaskWH = [&](int W, int H, int hdr = 0) -> SurfacePtr {
        if (data.size() != size_t(hdr + W * H))
          return {};
        const uint8_t *pix = data.data() + hdr;
        std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
        for (int i = 0; i < W * H; ++i) {
          const uint8_t v = pix[i];
          uint8_t *o = &rgba[size_t(i) * 4];
          if (v == 0xFF) { o[0]=o[1]=o[2]=o[3]=0; }
          else { o[0]=o[1]=o[2]=255; o[3]=v; }
        }
        return RgbaSurface(W, H, rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
      };
      auto decodeRgb = [&](int W, int H, int hdr = 0) -> SurfacePtr {
        if (data.size() != size_t(hdr + W * H * 3))
          return {};
        const uint8_t *pix = data.data() + hdr;
        return RgbaSurface(W, H, pix, SDL_PIXELFORMAT_RGB24, 3);
      };
      // Pure Mail title-menu buttons (TITL01..03[ a-g].IMG): raw 24-bit BGR,
      // 423x55, pure white is the colour key (pmail.exe blits them 423x55 at
      // x=189, y=326/388/450).
      auto decodeBgrKey = [&](int W, int H) -> SurfacePtr {
        if (data.size() != size_t(W) * H * 3)
          return {};
        std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
        for (int i = 0; i < W * H; ++i) {
          const uint8_t b = data[i * 3 + 0], g = data[i * 3 + 1],
                        r = data[i * 3 + 2];
          const size_t target = size_t(H - 1 - i / W) * W + i % W;
          uint8_t *o = &rgba[target * 4];
          if (b == 0xFF && g == 0xFF && r == 0xFF)
            continue;
          o[0] = r;
          o[1] = g;
          o[2] = b;
          o[3] = 255;
        }
        return RgbaSurface(W, H, rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
      };
      if (auto s = decodeBgrKey(423, 55)) return s; // TITL01..03.IMG

      if (data.size() == 57600) { // cgd00.IMG (raw grayscale, best-effort 240x240)
        if (auto s = decodeGray(240, 240)) return s;
      }
      // Best-effort heuristics for other observed PMAIL IMG sizes (format RE incomplete)
      if (data.size() == 588) { // n***.img glyphs/digits
        if (auto s = decodeMaskWH(21, 28)) return s;
      }
      if (data.size() == 330) { // mnum*.img
        if (auto s = decodeMaskWH(22, 15)) return s;
      }
      if (data.size() == 7857) { // pass_c.IMG
        if (auto s = decodeGray(97, 81)) return s;
      }
      if (data.size() == 9828) { // mailmes_c.IMG
        if (auto s = decodeRgb(126, 26)) return s;
      }
      if (data.size() == 14742) { // mailmes_d.IMG
        if (auto s = decodeRgb(126, 39)) return s;
      }
      if (data.size() == 40950) { // mailmes_u.IMG
        if (auto s = decodeRgb(150, 91)) return s;
      }
    }
    throw std::runtime_error("Cannot decode picture " + id);
  }
  if (ext == "chd" || ext == "char") {
    SnowRadish::Sprite sprite;
    const auto bytes = Graphic(key);
    if (!(ext == "char" ? decodeChar(bytes, sprite) : decodeChd(bytes, sprite)))
      throw std::runtime_error("Cannot decode sprite " + id);
    return RgbaSurface(int(sprite.width), int(sprite.height),
                       sprite.rgba.data(), SDL_PIXELFORMAT_RGBA32, 4);
  }
  return AssetProvider::loadSurfaceAsset(id);
}

Kotonoha::AudioClip SnowAssets::loadAudioAsset(const std::string &id) const {
  std::string key;
  const Pack *pack = PackOf(id, key);
  if (pack == &sound_) {
    const auto entry = pack->index.find(key);
    if (entry != pack->index.end() &&
        Lower(std::filesystem::path(entry->second.name).extension().string()) == ".adp4") {
      const auto pcm = decodeAdp4(read(id));
      if (pcm.samples.empty()) throw std::runtime_error("Cannot decode ADP4 " + id);
      return Clip(pcm.samples, 1, sampleRate_);
    }
    AdpsTrack track;
    if (!Adps(key, track))
      throw std::runtime_error("Cannot decode ADPS " + id);
    std::vector<int16_t> all = std::move(track.intro.samples);
    all.insert(all.end(), track.loop.samples.begin(), track.loop.samples.end());
    return Clip(all, 2, sampleRate_);
  }
  if (pack == &voice_) {
    const Pcm pcm = Voice(key);
    if (pcm.samples.empty()) {
      const auto bytes = read(id);
      if (bytes.size() != 4 || bytes[0] || bytes[1] || bytes[2] || bytes[3])
        throw std::runtime_error("Cannot decode ADP4 " + id);
    }
    return Clip(pcm.samples, 1, sampleRate_);
  }
  return AssetProvider::loadAudioAsset(id);
}

SnowAssets::Placement SnowAssets::Sprite(const std::string &name) const {
  const std::string key = Lower(name);
  if (const auto it = sprites_.find(key); it != sprites_.end())
    return it->second;
  Placement out;
  SnowRadish::Sprite sprite;
  if ((HasGraphic(key + ".char") && decodeChar(Graphic(key + ".char"), sprite)) ||
      (HasGraphic(key + ".chd") && decodeChd(Graphic(key + ".chd"), sprite)))
    out = {float(sprite.x), float(sprite.y), float(sprite.width),
           float(sprite.height), true};
  return sprites_[key] = out;
}

const SnowAssets::AnimationInfo &
SnowAssets::AnimationOf(const std::string &name) const {
  const std::string key = Lower(name);
  if (const auto it = animations_.find(key); it != animations_.end())
    return it->second;
  AnimationInfo info;
  Animation anm;
  const auto file = HasGraphic(key + ".anim") ? key + ".anim" : key + ".anm";
  if (HasGraphic(file) && decodeAnm(Graphic(file), anm)) {
    info.regions = std::move(anm.regions);
    info.cursor = anm.cursor;
    for (const auto &f : anm.frames)
      info.frames.push_back(
          {float(f.x), float(f.y), float(f.width), float(f.height), true});
  }
  return animations_[key] = std::move(info);
}

bool SnowAssets::Alpha(const std::string &file, bool alternate,
                       AlphaMask &out) const {
  return HasGraphic(file) && decodeAlpha(Graphic(file), alternate, out);
}

bool SnowAssets::Adps(const std::string &stem, AdpsTrack &out) const {
  const auto id = "sound/" + stem;
  return contains(id) && decodeAdpsFile(read(id), out);
}
Pcm SnowAssets::Voice(const std::string &stem) const {
  const auto id = "voice/" + stem;
  return contains(id) ? decodeAdp4(read(id)) : Pcm{};
}

} // namespace Kotonoha::Providers
