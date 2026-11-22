#include <Pmail/LargePonPonAssets.hpp>

#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace Kotonoha::Providers {

namespace {

std::string toUpper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::toupper(c)); });
  return s;
}

std::string trim(std::string s) {
  auto notSpace = [](unsigned char c) {
    return c != ' ' && c != '\t' && c != '\r' && c != '\n';
  };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

std::string findFileCI(const Kotonoha::Filesystem::Filesystem &fs,
                       const std::string &dir, const std::string &upperTarget) {
  try {
    for (const auto &info : fs.list(dir)) {
      if (info.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      std::string base = info.path;
      const auto slash = base.find_last_of('/');
      if (slash != std::string::npos)
        base = base.substr(slash + 1);
      if (toUpper(base) == upperTarget)
        return info.path;
    }
  } catch (const std::exception &) {
  }
  return {};
}

std::string findDirCI(const Kotonoha::Filesystem::Filesystem &fs,
                      const std::string &dir, const std::string &upperTarget) {
  try {
    for (const auto &info : fs.list(dir)) {
      if (info.type != Kotonoha::Filesystem::FileType::Directory)
        continue;
      std::string base = info.path;
      const auto slash = base.find_last_of('/');
      if (slash != std::string::npos)
        base = base.substr(slash + 1);
      if (toUpper(base) == upperTarget)
        return info.path;
    }
  } catch (const std::exception &) {
  }
  return {};
}

// Parse Windows COLORREF hex string "00BBGGRR" from MAINSYS.INI FontColorXX.
SDL_Color parseWinColorRef(const std::string &hex) {
  unsigned long val = std::strtoul(hex.c_str(), nullptr, 16);
  // Note: MAINSYS.INI FontColor00=00000000 (white/default for profile 0 if 0)
  // Actually in Windows GDI COLORREF is 0x00BBGGRR.
  uint8_t r = static_cast<uint8_t>(val & 0xFF);
  uint8_t g = static_cast<uint8_t>((val >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>((val >> 16) & 0xFF);
  if (val == 0)
    return {240, 240, 232, 255};
  return {r, g, b, 255};
}

} // namespace

LargePonPonAssets::LargePonPonAssets(const std::filesystem::path &directory)
    : LargePonPonAssets(
          Kotonoha::Filesystem::Filesystem::mount(directory.string())) {}

LargePonPonAssets::LargePonPonAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> source)
    : source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("LargePonPonAssets requires a mounted source");
  initFromSource();
}

void LargePonPonAssets::initFromSource() {
  const std::string packDir = findDirCI(*source_, "", "PACK");
  auto openPack = [&](GcPackArchive &archive, const char *fileName) {
    std::string path;
    if (!packDir.empty())
      path = findFileCI(*source_, packDir, fileName);
    if (path.empty())
      path = findFileCI(*source_, "", fileName);
    if (!path.empty())
      archive.open(source_->open(path));
  };

  openPack(scriptPack_, "SCRIPT.DAT");
  openPack(graphicPack_, "GRAPHIC.DAT");
  openPack(soundPack_, "SOUND.DAT");

  if (!scriptPack_.isOpen())
    throw std::runtime_error(
        "LargePonPonAssets: failed to open PACK/script.dat");

  parseMainSysIni();
}

void LargePonPonAssets::parseMainSysIni() {
  const std::string iniPath = findFileCI(*source_, "", "MAINSYS.INI");
  if (iniPath.empty())
    return;

  std::vector<uint8_t> raw;
  try {
    raw = Kotonoha::Filesystem::readAll(*source_->open(iniPath));
  } catch (const std::exception &) {
    return;
  }
  if (raw.empty())
    return;

  std::string text;
  try {
    text = Kotonoha::ShiftJisToUtf8(std::string_view(
        reinterpret_cast<const char *>(raw.data()), raw.size()));
  } catch (const std::exception &) {
    text.assign(reinterpret_cast<const char *>(raw.data()), raw.size());
  }

  std::istringstream iss(text);
  std::string line;
  std::string section;
  while (std::getline(iss, line)) {
    line = trim(line);
    if (line.empty() || line[0] == ';')
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = toUpper(line.substr(1, line.size() - 2));
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string val = trim(line.substr(eq + 1));

    if (section == "CONFIGDATA" && key.rfind("Config", 0) == 0 &&
        key.size() == 8) {
      int idx = std::atoi(key.c_str() + 6);
      if (idx >= 0 && idx < 64)
        config_.config[static_cast<size_t>(idx)] =
            static_cast<int16_t>(std::atoi(val.c_str()));
    } else if (section == "NAMEDATA" && key.rfind("Name", 0) == 0 &&
               key.size() == 6) {
      int idx = std::atoi(key.c_str() + 4);
      if (idx >= 0 && idx < 4 && !val.empty())
        config_.names[static_cast<size_t>(idx)] = val;
    } else if (section == "FONTDATA") {
      if (key.rfind("FontName", 0) == 0 && key.size() == 10) {
        int idx = std::atoi(key.c_str() + 8);
        if (idx >= 0 && idx < 21)
          config_.fonts[static_cast<size_t>(idx)].name = val;
      } else if (key.rfind("FontSize", 0) == 0 && key.size() == 10) {
        int idx = std::atoi(key.c_str() + 8);
        if (idx >= 0 && idx < 21)
          config_.fonts[static_cast<size_t>(idx)].weight =
              std::atoi(val.c_str());
      } else if (key.rfind("FontColor", 0) == 0 && key.size() == 11) {
        int idx = std::atoi(key.c_str() + 9);
        if (idx >= 0 && idx < 21)
          config_.fonts[static_cast<size_t>(idx)].color = parseWinColorRef(val);
      }
    }
  }
}

std::string LargePonPonAssets::ensureExt(const std::string &stem,
                                         const char *ext) {
  std::string up = toUpper(stem);
  if (up.find('.') == std::string::npos)
    up += ext;
  return up;
}

std::string LargePonPonAssets::upperExt(const std::string &id) {
  const auto dot = id.find_last_of('.');
  if (dot == std::string::npos)
    return {};
  return toUpper(id.substr(dot));
}

bool LargePonPonAssets::containsAsset(const std::string &id) const {
  if (graphicPack_.contains(id) || soundPack_.contains(id) ||
      scriptPack_.contains(id)) {
    return true;
  }
  try {
    return source_->exists(id) &&
           source_->stat(id).type == Kotonoha::Filesystem::FileType::Regular;
  } catch (const std::exception &) {
    return false;
  }
}

std::vector<Uint8> LargePonPonAssets::readAsset(const std::string &id) const {
  if (graphicPack_.contains(id))
    return graphicPack_.read(id);
  if (soundPack_.contains(id))
    return soundPack_.read(id);
  if (scriptPack_.contains(id))
    return scriptPack_.read(id);
  return Kotonoha::Filesystem::readAll(*source_->open(id));
}

std::vector<std::string>
LargePonPonAssets::listAsset(const std::string &prefix) const {
  const std::string upPrefix = toUpper(prefix);
  std::vector<std::string> out;
  auto collect = [&](const GcPackArchive &pack) {
    for (const auto &e : pack.entries()) {
      if (upPrefix.empty() || toUpper(e.name).rfind(upPrefix, 0) == 0)
        out.push_back(e.name);
    }
  };
  collect(scriptPack_);
  collect(graphicPack_);
  collect(soundPack_);
  std::sort(out.begin(), out.end());
  return out;
}

std::string LargePonPonAssets::localPathAsset(const std::string &) const {
  return {};
}

SurfacePtr LargePonPonAssets::loadSurfaceAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
  if (bytes.empty())
    return nullptr;
  const std::string ext = upperExt(id);
  if (ext == ".CRGB" || ext == ".BGD")
    return decodeCrgbOrBgd(bytes.data(), bytes.size());
  if (ext == ".CHAR")
    return decodeCharStream(bytes.data(), bytes.size(), 800, 600);
  if (ext == ".PCHR") {
    auto frames = decodePchrOrIcon(bytes.data(), bytes.size(), 800, 600);
    return frames.empty() ? nullptr : std::move(frames[0]);
  }
  if (ext == ".ICON") {
    auto frames = decodePchrOrIcon(bytes.data(), bytes.size(), 32, 32);
    return frames.empty() ? nullptr : std::move(frames[0]);
  }
  if (ext == ".PRGB") {
    auto sprites = decodePrgb(bytes.data(), bytes.size());
    return sprites.empty() ? nullptr : std::move(sprites[0]);
  }
  if (ext == ".P256")
    return decodeP256Thumbnail(bytes.data(), bytes.size(), 0, true);
  if (ext == ".ALPF")
    return alphaMaskToSurface(decodeAlpfMask(bytes.data(), bytes.size()), 800,
                              600);
  if (ext == ".ALPM")
    return alphaMaskToSurface(decodeAlpmMask(bytes.data(), bytes.size()), 800,
                              600);
  return nullptr;
}

AudioClip LargePonPonAssets::loadAudioAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
  if (bytes.empty())
    return {};
  const std::string ext = upperExt(id);
  if (ext == ".ADP4") {
    auto decoded = decodeAdp4(bytes.data(), bytes.size());
    if (decoded.samples.empty())
      return {};
    AudioClip clip;
    clip.spec.format = SDL_AUDIO_S16;
    clip.spec.channels = decoded.channels;
    clip.spec.freq = decoded.sampleRate;
    clip.data.resize(decoded.samples.size() * sizeof(int16_t));
    std::memcpy(clip.data.data(), decoded.samples.data(), clip.data.size());
    return clip;
  }
  if (ext == ".SMFD")
    return decodeSmfdToPcm(bytes.data(), bytes.size());
  return {};
}

std::vector<uint8_t>
LargePonPonAssets::loadScriptBytes(const std::string &stemOrName) const {
  return scriptPack_.read(ensureExt(stemOrName, ".OBJ"));
}

SurfacePtr LargePonPonAssets::loadCrgb(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".CRGB"));
  return bytes.empty() ? nullptr : decodeCrgbOrBgd(bytes.data(), bytes.size());
}

SurfacePtr LargePonPonAssets::loadBgd(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".BGD"));
  return bytes.empty() ? nullptr : decodeCrgbOrBgd(bytes.data(), bytes.size());
}

SurfacePtr LargePonPonAssets::loadChar(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".CHAR"));
  return bytes.empty() ? nullptr
                       : decodeCharStream(bytes.data(), bytes.size(), 800, 600);
}

std::vector<SurfacePtr>
LargePonPonAssets::loadPchrFrames(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".PCHR"));
  return bytes.empty() ? std::vector<SurfacePtr>{}
                       : decodePchrOrIcon(bytes.data(), bytes.size(), 800, 600);
}

std::vector<SurfacePtr>
LargePonPonAssets::loadIconFrames(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".ICON"));
  return bytes.empty() ? std::vector<SurfacePtr>{}
                       : decodePchrOrIcon(bytes.data(), bytes.size(), 32, 32);
}

std::vector<SurfacePtr>
LargePonPonAssets::loadPrgbSprites(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".PRGB"));
  return bytes.empty() ? std::vector<SurfacePtr>{}
                       : decodePrgb(bytes.data(), bytes.size());
}

SurfacePtr LargePonPonAssets::loadP256Thumbnail(const std::string &stem,
                                                size_t index,
                                                bool color) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".P256"));
  return bytes.empty()
             ? nullptr
             : decodeP256Thumbnail(bytes.data(), bytes.size(), index, color);
}

CanmData LargePonPonAssets::loadCanm(const std::string &stem) const {
  const auto bytes = graphicPack_.read(ensureExt(stem, ".CANM"));
  return bytes.empty() ? CanmData{} : decodeCanm(bytes.data(), bytes.size());
}

std::vector<uint8_t> LargePonPonAssets::loadAlphaMask(const std::string &stem,
                                                      bool isAlpm) const {
  const auto bytes =
      graphicPack_.read(ensureExt(stem, isAlpm ? ".ALPM" : ".ALPF"));
  if (bytes.empty())
    return {};
  return isAlpm ? decodeAlpmMask(bytes.data(), bytes.size())
                : decodeAlpfMask(bytes.data(), bytes.size());
}

Adp4Decoded LargePonPonAssets::loadAdp4(const std::string &stem) const {
  const auto bytes = soundPack_.read(ensureExt(stem, ".ADP4"));
  return bytes.empty() ? Adp4Decoded{} : decodeAdp4(bytes.data(), bytes.size());
}

AudioClip LargePonPonAssets::loadSmfdAudio(const std::string &stem) const {
  const auto bytes = soundPack_.read(ensureExt(stem, ".SMFD"));
  return bytes.empty() ? AudioClip{}
                       : decodeSmfdToPcm(bytes.data(), bytes.size());
}

} // namespace Kotonoha::Providers
