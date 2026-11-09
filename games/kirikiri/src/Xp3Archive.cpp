#include <Kirikiri/KirikiriAssets.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace Kirikiri {

namespace {

// XP3 magic: "XP3\r\n \n\x1a\x8b\x67\x01"
const std::uint8_t kXp3Magic[11] = {0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20,
                                    0x0A, 0x1A, 0x8B, 0x67, 0x01};

// MDF magic: "mdf\x00" (compressed PSB container used by KirikiriZ .scn files)
const std::uint8_t kMdfMagic[4] = {0x6D, 0x64, 0x66, 0x00};

// PSB magic: "PSB\x00"
const std::uint8_t kPsbMagic[4] = {0x50, 0x53, 0x42, 0x00};

std::uint16_t readU16LE(const std::uint8_t *p) {
  return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
std::uint32_t readU32LE(const std::uint8_t *p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t readU64LE(const std::uint8_t *p) {
  return std::uint64_t(readU32LE(p)) | (std::uint64_t(readU32LE(p + 4)) << 32);
}

std::string readUtf16Le(const std::uint8_t *buf, std::size_t &pos,
                        std::size_t wcharCount) {
  std::string out;
  out.reserve(wcharCount);
  for (std::size_t i = 0; i < wcharCount; ++i) {
    std::uint16_t wc = readU16LE(buf + pos);
    pos += 2;
    if (wc == 0)
      break;
    if (wc < 0x80) {
      out.push_back(static_cast<char>(wc));
    } else {
      out.push_back(static_cast<char>(0xC0 | (wc >> 6)));
      out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
    }
  }
  return out;
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot open " + path.string());
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> zlibInflate(const std::uint8_t *data,
                                      std::size_t size,
                                      std::size_t expectedOriginal) {
  z_stream stream{};
  stream.next_in = const_cast<std::uint8_t *>(data);
  stream.avail_in = static_cast<uInt>(size);
  if (inflateInit(&stream) != Z_OK)
    throw std::runtime_error("zlib inflateInit failed");
  std::vector<std::uint8_t> out;
  out.resize(expectedOriginal ? expectedOriginal : 65536);
  std::size_t total = 0;
  int ret;
  do {
    if (total + out.size() - stream.total_out < 4096)
      out.resize(out.size() * 2);
    stream.next_out = out.data() + total;
    stream.avail_in =
        size > stream.total_in ? static_cast<uInt>(size - stream.total_in) : 0;
    stream.avail_out = static_cast<uInt>(out.size() - total);
    ret = inflate(&stream, Z_NO_FLUSH);
    total = stream.total_out;
    if (ret == Z_STREAM_END)
      break;
    if (ret != Z_OK && ret != Z_BUF_ERROR) {
      inflateEnd(&stream);
      throw std::runtime_error("zlib inflate failed");
    }
    if (stream.avail_out == 0 && ret != Z_STREAM_END)
      out.resize(out.size() * 2);
  } while (ret != Z_STREAM_END);
  inflateEnd(&stream);
  out.resize(total);
  return out;
}

// Decompress an MDF container ("mdf\0" header + zlib data) into raw PSB bytes.
std::vector<std::uint8_t> decompressMdf(const std::uint8_t *data,
                                        std::size_t size) {
  if (size < 8 || std::memcmp(data, kMdfMagic, 4) != 0)
    throw std::runtime_error("Not an MDF file");
  // Skip mdf magic(4) + 4 header bytes, zlib data follows.
  const std::uint8_t *zlibData = data + 8;
  std::size_t zlibSize = size - 8;
  return zlibInflate(zlibData, zlibSize, 0);
}

// Extract readable KAG/script text from a decompressed PSB (E-Mote) binary.
// PSB stores dialogue as null-terminated strings with "%f<font>;<text>" markers
// and Japanese brackets「 」.  We collect dialogue strings wrapped as KAG cm
// tags plus any bare KAG labels (starting with '*').
std::string extractPsbScript(const std::vector<std::uint8_t> &psbData) {
  std::string script;
  try {
    std::size_t i = 0;
    while (i < psbData.size()) {
      // Japanese opening bracket "「" (UTF-8: e3 80 8c) -> dialogue text.
      if (i + 3 <= psbData.size() && psbData[i] == 0xE3 &&
          psbData[i + 1] == 0x80 && psbData[i + 2] == 0x8C) {
        i += 3;
        std::string dialogue;
        while (i < psbData.size()) {
          if (i + 3 <= psbData.size() && psbData[i] == 0xE3 &&
              psbData[i + 1] == 0x80 && psbData[i + 2] == 0x8D) {
            i += 3;
            break;
          }
          if (i >= psbData.size())
            break;
          unsigned char c = psbData[i];
          if (c >= 0x80) {
            dialogue.push_back(static_cast<char>(c));
            i++;
            while (i < psbData.size() && (psbData[i] & 0xC0) == 0x80) {
              dialogue.push_back(static_cast<char>(psbData[i]));
              i++;
            }
          } else if (c == 0) {
            break;
          } else {
            dialogue.push_back(static_cast<char>(c));
            i++;
          }
        }
        // Strip trailing font-reset marker "%fSourceHanSansCN-M;" if present.
        static const char *kFontReset = "%fSourceHanSansCN-M;";
        if (dialogue.size() > strlen(kFontReset) &&
            dialogue.compare(dialogue.size() - strlen(kFontReset),
                             strlen(kFontReset), kFontReset) == 0)
          dialogue.resize(dialogue.size() - strlen(kFontReset));
        // Strip leading "%f;" if present.
        if (dialogue.size() >= 3 && dialogue[0] == '%' && dialogue[1] == 'f' &&
            dialogue[2] == ';')
          dialogue.erase(0, 3);
        if (!dialogue.empty()) {
          script += "[cm text=\"";
          script += dialogue;
          script += "\"]\n";
        }
        continue;
      }
      i++;
    }
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Kirikiri: extractPsbScript exception: %s", e.what());
  }
  return script;
}

} // namespace

bool KirikiriAssets::containsAsset(const std::string &id) const {
  if (base->contains(id))
    return true;
  auto resolved = resolveId(id);
  if (resolved != id && base->contains(resolved))
    return true;
  // Check XP3 archives.
  for (const auto &[name, entries] : archives) {
    for (const auto &e : entries)
      if (e.name == id || e.name == resolved)
        return true;
  }
  return false;
}

std::vector<std::uint8_t>
KirikiriAssets::readAsset(const std::string &id) const {
  std::vector<std::uint8_t> data;
  if (base->contains(id)) {
    data = base->read(id);
  } else {
    auto resolved = resolveId(id);
    if (resolved != id && base->contains(resolved))
      data = base->read(resolved);
    else {
      // Search XP3 archives.
      for (const auto &[name, entries] : archives) {
        for (const auto &e : entries) {
          if (e.name == id || e.name == resolved) {
            data = extractXp3Entry(e);
            break;
          }
        }
        if (!data.empty())
          break;
      }
    }
  }
  if (data.empty())
    throw std::runtime_error("Asset not found: " + id);

  // MDF container: "mdf\0" + zlib-compressed PSB.  Decompress for any source.
  if (data.size() >= 8 && std::memcmp(data.data(), kMdfMagic, 4) == 0)
    data = decompressMdf(data.data(), data.size());

  // PSB scenario (.scn): extract readable KAG/script text from E-Mote PSB.
  if (data.size() >= 4 && std::memcmp(data.data(), kPsbMagic, 4) == 0 &&
      id.size() >= 4 && id.substr(id.size() - 4) == ".scn")
    return std::vector<std::uint8_t>(extractPsbScript(data).begin(),
                                     extractPsbScript(data).end());

  return data;
}

std::vector<std::string>
KirikiriAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result = base->list(prefix);
  for (const auto &[name, entries] : archives) {
    for (const auto &e : entries) {
      if (e.name.rfind(prefix, 0) == 0)
        result.push_back(e.name);
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string KirikiriAssets::localPathAsset(const std::string &id) const {
  if (base->contains(id))
    return base->localPath(id);
  return {};
}

Kotonoha::SurfacePtr
KirikiriAssets::loadSurfaceAsset(const std::string &id) const {
  return base->loadSurface(id);
}

std::string KirikiriAssets::resolveId(const std::string &id) const {
  std::string out = id;
  for (auto &c : out)
    if (c == '\\')
      c = '/';
  return out;
}

void KirikiriAssets::mountXp3(const std::filesystem::path &path) {
  std::filesystem::path archivePath = path;
  if (!archivePath.is_absolute())
    archivePath = root / archivePath;
  auto name = archivePath.filename().string();
  if (archives.count(name))
    return; // already mounted
  if (!readXp3(archivePath)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Kirikiri: failed to parse XP3 archive %s",
                archivePath.string().c_str());
    return;
  }
}

std::vector<std::string> KirikiriAssets::scenarioFiles() const {
  std::vector<std::string> result;
  auto all = listAsset("");
  for (const auto &f : all) {
    auto lower = f;
    for (auto &c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Accept both plain .ks and KirikiriZ-compiled .ks.scn (MDF-wrapped PSB).
    if ((lower.size() >= 3 && lower.substr(lower.size() - 3) == ".ks") ||
        (lower.size() >= 7 && lower.substr(lower.size() - 7) == ".ks.scn"))
      result.push_back(f);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool KirikiriAssets::readXp3(const std::filesystem::path &path) {
  // Read just enough to validate magic and find the table offset.
  std::FILE *f = std::fopen(path.string().c_str(), "rb");
  if (!f)
    return false;
  auto closer = [&]() { std::fclose(f); };

  std::uint8_t magic[11];
  if (std::fread(magic, 1, 11, f) != 11 ||
      std::memcmp(magic, kXp3Magic, 11) != 0) {
    closer();
    return false;
  }

  std::uint64_t infoOffset = 0;
  if (std::fread(&infoOffset, 1, 8, f) != 8) {
    closer();
    return false;
  }

  std::uint32_t version = 0;
  if (std::fread(&version, 1, 4, f) != 4) {
    closer();
    return false;
  }

  std::uint64_t tableOffset = 0;
  if (version == 1) {
    // Skip table_size(8) + flags(1), read table_offset(8).
    if (std::fseek(f, 9, SEEK_CUR) != 0) {
      closer();
      return false;
    }
    if (std::fread(&tableOffset, 1, 8, f) != 8) {
      closer();
      return false;
    }
  } else {
    tableOffset = infoOffset;
  }

  // Follow info_offset chain: KirikiriZ marks with 0x80 sentinel.
  std::size_t metaPos = static_cast<std::size_t>(tableOffset);
  std::uint8_t first = 0;
  if (std::fseek(f, static_cast<long>(metaPos), SEEK_SET) != 0) {
    closer();
    return false;
  }
  if (std::fread(&first, 1, 1, f) != 1) {
    closer();
    return false;
  }
  if (first == 0x80) {
    // Skip 8 bytes, read true info offset.
    if (std::fseek(f, 8, SEEK_CUR) != 0) {
      closer();
      return false;
    }
    std::uint64_t trueOffset = 0;
    if (std::fread(&trueOffset, 1, 8, f) != 8) {
      closer();
      return false;
    }
    metaPos = static_cast<std::size_t>(trueOffset);
  }

  // File table metadata: compressed flag (1), compressed_size (8),
  // original_size (8).
  std::uint8_t compressedFlag = 0;
  if (std::fseek(f, static_cast<long>(metaPos), SEEK_SET) != 0) {
    closer();
    return false;
  }
  if (std::fread(&compressedFlag, 1, 1, f) != 1) {
    closer();
    return false;
  }
  bool compressed = compressedFlag != 0;
  std::uint64_t compSize = 0, origSize = 0;
  if (std::fread(&compSize, 1, 8, f) != 8 ||
      std::fread(&origSize, 1, 8, f) != 8) {
    closer();
    return false;
  }
  std::size_t tableDataStart = static_cast<std::size_t>(metaPos) + 17;

  std::vector<std::uint8_t> tableData;
  if (compressed) {
    std::vector<std::uint8_t> compressedData(compSize);
    if (std::fseek(f, static_cast<long>(tableDataStart), SEEK_SET) != 0) {
      closer();
      return false;
    }
    if (std::fread(compressedData.data(), 1, compSize, f) != compSize) {
      closer();
      return false;
    }
    tableData = zlibInflate(compressedData.data(), compSize, origSize);
  } else {
    tableData.resize(origSize);
    if (std::fseek(f, static_cast<long>(tableDataStart), SEEK_SET) != 0) {
      closer();
      return false;
    }
    if (std::fread(tableData.data(), 1, origSize, f) != origSize) {
      closer();
      return false;
    }
  }
  closer();

  // Parse the file table: sequence of chunks (4-byte tag + 8-byte length).
  std::vector<Xp3Entry> entries;
  std::size_t tp = 0;
  while (tp + 12 <= tableData.size()) {
    std::uint32_t tag = readU32LE(tableData.data() + tp);
    tp += 4;
    std::uint64_t chunkSize = readU64LE(tableData.data() + tp);
    tp += 8;
    if (tp + chunkSize > tableData.size())
      break;
    const std::uint8_t *chunk = tableData.data() + tp;
    tp += static_cast<std::size_t>(chunkSize);

    if (tag == 0x656C6946) { // "File"
      // Parse sub-chunks inside the File entry.
      std::size_t sp = 0;
      Xp3Entry entry;
      entry.archivePath = path;
      bool hasInfo = false, hasSegm = false;
      std::uint64_t segOffset = 0, segCompSize = 0, segOrigSize = 0;
      bool segCompressed = false;
      while (sp + 12 <= chunkSize) {
        std::uint32_t subTag = readU32LE(chunk + sp);
        sp += 4;
        std::uint64_t subSize = readU64LE(chunk + sp);
        sp += 8;
        if (sp + subSize > chunkSize)
          break;
        const std::uint8_t *sub = chunk + sp;
        sp += static_cast<std::size_t>(subSize);

        if (subTag == 0x6F666E69) { // "info" (little-endian: 69 6E 66 6F)
          if (subSize >= 22) {
            std::uint32_t flags = readU32LE(sub);
            std::uint64_t orig = readU64LE(sub + 4);
            std::uint64_t arch = readU64LE(sub + 12);
            std::uint16_t nameLen = readU16LE(sub + 20);
            entry.originalSize = orig;
            entry.compressedSize = arch;
            entry.compressed = (flags & 0x80000000u) != 0;
            if (subSize >= 22 + nameLen * 2) {
              std::size_t u16pos = 22;
              entry.name = readUtf16Le(sub, u16pos, nameLen);
            }
            hasInfo = true;
          }
        } else if (subTag == 0x6D676573) { // "segm"
          if (subSize >= 28) {
            segCompressed = (readU32LE(sub) & 7u) != 0;
            segOffset = readU64LE(sub + 4);
            segOrigSize = readU64LE(sub + 12);
            segCompSize = readU64LE(sub + 20);
            hasSegm = true;
          }
        } else if (subTag == 0x726C6461) { // "adlr"
          // adlr key is used to match File entries; we rely on info name.
        }
      }
      if (hasInfo && hasSegm) {
        entry.offset = segOffset;
        entry.compressedSize = segCompSize;
        entry.originalSize = segOrigSize;
        entry.compressed = segCompressed;
        if (!entry.name.empty())
          entries.push_back(entry);
      }
    }
  }

  archives.emplace(path.filename().string(), std::move(entries));
  return true;
}

std::vector<std::uint8_t>
KirikiriAssets::extractXp3Entry(const Xp3Entry &entry) const {
  std::FILE *f = std::fopen(entry.archivePath.string().c_str(), "rb");
  if (!f)
    throw std::runtime_error("Cannot open XP3 archive: " +
                             entry.archivePath.string());
  auto closer = [&]() { std::fclose(f); };

  if (std::fseek(f, static_cast<long>(entry.offset), SEEK_SET) != 0) {
    closer();
    throw std::runtime_error("Cannot seek in XP3 archive: " +
                             entry.archivePath.string());
  }

  std::vector<std::uint8_t> raw(entry.compressedSize);
  if (std::fread(raw.data(), 1, entry.compressedSize, f) !=
      entry.compressedSize) {
    closer();
    throw std::runtime_error("Cannot read entry data: " + entry.name);
  }
  closer();

  // Decompress XP3 segment if needed.
  if (entry.compressed)
    return zlibInflate(raw.data(), raw.size(), entry.originalSize);
  return raw;
}

} // namespace Kirikiri
