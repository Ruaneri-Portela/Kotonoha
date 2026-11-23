// Reusable XP3 provider; independent of the KAG runtime.
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/PsbFile.hpp>
#include <Kotonoha/Formats/Tlg.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <zlib.h>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace Kotonoha::Providers {

namespace {

// XP3 magic: "XP3\r\n \n\x1a\x8b\x67\x01"
const std::uint8_t kXp3Magic[11] = {0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20,
                                    0x0A, 0x1A, 0x8B, 0x67, 0x01};

// MDF magic: "mdf\x00" (compressed PSB container used by KirikiriZ .scn files)
const std::uint8_t kMdfMagic[4] = {0x6D, 0x64, 0x66, 0x00};

// PSB magic: "PSB\x00"
const std::uint8_t kPsbMagic[4] = {0x50, 0x53, 0x42, 0x00};

std::string foldedArchivePath(std::string value) {
  for (char &ch : value) {
    if (ch == '\\') {
      ch = '/';
    } else if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

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
  out.reserve(wcharCount * 2);
  bool terminated = false;
  for (std::size_t i = 0; i < wcharCount; ++i) {
    std::uint32_t ch = readU16LE(buf + pos);
    pos += 2;
    if (ch == 0) {
      terminated = true;
      continue;
    }
    if (terminated)
      continue;
    if (ch >= 0xd800 && ch <= 0xdbff) {
      if (++i >= wcharCount)
        throw std::runtime_error("XP3 truncated UTF-16 surrogate pair");
      const auto trail = readU16LE(buf + pos);
      pos += 2;
      if (trail < 0xdc00 || trail > 0xdfff)
        throw std::runtime_error("XP3 invalid UTF-16 surrogate pair");
      ch = 0x10000u + ((ch - 0xd800u) << 10) + trail - 0xdc00u;
    } else if (ch >= 0xdc00 && ch <= 0xdfff) {
      throw std::runtime_error("XP3 unexpected UTF-16 low surrogate");
    }
    if (ch <= 0x7f) {
      out.push_back(static_cast<char>(ch));
    } else if (ch <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (ch >> 6)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    } else if (ch <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (ch >> 12)));
      out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (ch >> 18)));
      out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    }
  }
  return out;
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path &path) {
  Kotonoha::Filesystem::InputFile in(path, std::ios::binary);
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
    const auto previousIn = stream.total_in;
    const auto previousOut = stream.total_out;
    ret = inflate(&stream, Z_NO_FLUSH);
    total = stream.total_out;
    // A truncated stream can return Z_BUF_ERROR forever without making any
    // progress. Stop instead of freezing the game's loading screen.
    if (ret != Z_STREAM_END && stream.total_in == previousIn &&
        stream.total_out == previousOut) {
      inflateEnd(&stream);
      throw std::runtime_error("Truncated or stalled XP3 zlib stream");
    }
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
  if (expectedOriginal != 0 && total != expectedOriginal)
    throw std::runtime_error("XP3 zlib decoded size mismatch");
  out.resize(total);
  return out;
}

// Decompress an MDF container ("mdf\0" header + zlib data) into raw PSB bytes.
std::vector<std::uint8_t> decompressMdf(const std::uint8_t *data,
                                        std::size_t size) {
  return PsbFile::decompressMdf(data, size);
}

std::string stripPsbFontTags(std::string text) {
  std::size_t fpos = 0;
  while ((fpos = text.find("%f", fpos)) != std::string::npos) {
    const auto semi = text.find(';', fpos);
    if (semi != std::string::npos && semi - fpos < 64) {
      text.erase(fpos, semi - fpos + 1);
    } else {
      fpos += 2;
    }
  }
  // Strip outer Japanese corner brackets 「...」 when wrapping the whole line.
  if (text.size() >= 6 && std::memcmp(text.data(), "\xE3\x80\x8C", 3) == 0 &&
      std::memcmp(text.data() + text.size() - 3, "\xE3\x80\x8D", 3) == 0) {
    text = text.substr(3, text.size() - 6);
  }
  return text;
}

std::string escapeKagAttr(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"')
      out.push_back('\'');
    else if (c == '\r' || c == '\n')
      out.push_back(' ');
    else
      out.push_back(c);
  }
  return out;
}

std::pair<std::string, std::string>
resolveNekoparaSpeaker(const std::string &primary, const std::string &combined,
                       const std::string &enDisplay) {
  const std::string key = !combined.empty() ? combined : primary;
  static const std::map<std::string, std::pair<std::string, std::string>> kMap =
      {
          {"ショコラ", {"Chocola", "VNX+/cn_name_chocola.png"}},
          {"バニラ", {"Vanilla", "VNX+/cn_name_vanilla.png"}},
          {"時雨", {"Shigure", "VNX+/cn_name_shigure.png"}},
          {"アズキ", {"Azuki", "VNX+/cn_name_azuki.png"}},
          {"メイプル", {"Maple", "VNX+/cn_name_maple.png"}},
          {"シナモン", {"Cinnamon", "VNX+/cn_name_cinnamon.png"}},
          {"ココナツ", {"Coconut", "VNX+/cn_name_coconut.png"}},
          {"全員", {"Todos", "VNX+/cn_name_all.png"}},
          {"ショコラ＆バニラ",
           {"Chocola & Vanilla", "VNX+/cn_name_chocola_vanilla.png"}},
          {"ショコラ＆時雨",
           {"Chocola & Shigure", "VNX+/cn_name_chocola_shigure.png"}},
          {"メイプル＆アズキ",
           {"Maple & Azuki", "VNX+/cn_name_maple_azuki.png"}},
          {"アズキ＆シナモン",
           {"Azuki & Cinnamon", "VNX+/cn_name_azuki_cinnamon.png"}},
          {"ココナツ＆アズキ",
           {"Coconut & Azuki", "VNX+/cn_name_coconut_azuki.png"}},
          {"ココナツ＆メイプル",
           {"Coconut & Maple", "VNX+/cn_name_coconut_maple.png"}},
          {"ココナツ＆シナモン",
           {"Coconut & Cinnamon", "VNX+/cn_name_coconut_cinnamon.png"}},
          {"メイプル＆シナモン",
           {"Maple & Cinnamon", "VNX+/cn_name_maple_cinnamon.png"}},
          {"メイプル＆時雨",
           {"Maple & Shigure", "VNX+/cn_name_maple_shigure.png"}},
          {"時雨＆ココナツ",
           {"Shigure & Coconut", "VNX+/cn_name_shigure_coconut.png"}},
          {"バニラ＆シナモン",
           {"Vanilla & Cinnamon", "VNX+/cn_name_vanilla_cinnamon.png"}},
          {"ショコラ＆バニラ＆ココナツ",
           {"Chocola, Vanilla & Coconut",
            "VNX+/cn_name_chocola_vanilla_coconut.png"}},
          {"ショコラ＆バニラ＆時雨",
           {"Chocola, Vanilla & Shigure",
            "VNX+/cn_name_chocola_vanilla_shigure.png"}},
  };
  if (auto it = kMap.find(key); it != kMap.end())
    return it->second;
  if (!enDisplay.empty())
    return {enDisplay, {}};
  return {key, {}};
}

// Decompile a PSB v2 KAGEnv scenario (.ks.scn) into structured KAG tags using
// the PSB binary tree parser. Selects language slot 2 (PT-BR in VNX+) when
// present, preserving speaker names, voices, backgrounds, CGs, characters,
// BGM/SE, timed waits, persistent gallery flags, and chapter transitions.
std::string extractPsbScript(const std::vector<std::uint8_t> &psbData) {
  std::string script;
  try {
    const auto psb = PsbFile::parse(psbData);
    const auto &root = psb.root();
    const auto &scenes = root["scenes"];
    if (scenes.isList() && scenes.size() > 0) {
      for (const auto &scene : scenes.asList()) {
        if (!scene.isDict())
          continue;
        std::string label = scene["label"].asString();
        if (label.empty())
          label = "*start";
        if (label.front() != '*')
          label.insert(label.begin(), '*');
        std::string title;
        const auto &titles = scene["title"];
        if (titles.isList() && titles.size() > 0) {
          const std::size_t langIdx =
              titles.size() > 2 && !titles[2].asString().empty()
                  ? 2
                  : (titles.size() > 1 && !titles[1].asString().empty() ? 1
                                                                        : 0);
          title = titles[langIdx].asString();
        }
        script += label;
        if (!title.empty())
          script += "|" + escapeKagAttr(title);
        script += "\n";

        const auto &texts = scene["texts"];
        const auto &lines = scene["lines"];
        auto emitEnvUpdateList = [&](const PsbValue &updateList) {
          if (!updateList.isList())
            return;
          for (const auto &item : updateList.asList()) {
            if (item.isList() && item.size() >= 2) {
              const std::string op = item[0].asString();
              if (op == "init") {
                script += "[freeimage layer=\"base\"]\n";
              } else if (op == "del") {
                const std::string target = item[1].asString();
                if (target == "bg")
                  script += "[freeimage layer=\"base\"]\n";
                else if (target == "bgm")
                  script += "[stopbgm]\n";
                else if (!target.empty())
                  script +=
                      "[chara_hide layer=\"" + escapeKagAttr(target) + "\"]\n";
              }
            } else if (item.isDict()) {
              const std::string objName = item["name"].asString();
              const std::string cls = item["class"].asString();
              if (objName == "bgm" || cls == "bgm") {
                const std::string file = item["replay"]["filename"].asString();
                if (file == "bgm_" || file.empty()) {
                  if (!file.empty())
                    script += "[stopbgm]\n";
                } else {
                  const bool loop = item["replay"]["loop"].toInt(1) != 0;
                  script += "[playbgm file=\"" + escapeKagAttr(file) +
                            "\" loop=\"" + (loop ? "true" : "false") + "\"]\n";
                }
              } else if (objName == "se" || objName == "se1" || cls == "se") {
                if (item["stop"].toInt(0) == 0) {
                  const std::string file =
                      item["replay"]["filename"].asString();
                  if (!file.empty())
                    script += "[playse file=\"" + escapeKagAttr(file) + "\"]\n";
                }
              } else if (objName == "bg" || cls == "stage") {
                const std::string file =
                    item["redraw"]["imageFile"]["file"].asString();
                if (!file.empty())
                  script += "[bg storage=\"" + escapeKagAttr(file) +
                            "\" time=\"0\"]\n";
              } else if (cls == "event" || objName == "ev" ||
                         objName == "ev1" || objName == "eventf" ||
                         objName == "cutin") {
                const std::string file =
                    item["redraw"]["imageFile"]["file"].asString();
                if (!file.empty()) {
                  if (objName == "ev" || objName == "eventf")
                    script += "[bg storage=\"" + escapeKagAttr(file) +
                              "\" time=\"0\"]\n";
                  else
                    script += "[chara_show layer=\"" + escapeKagAttr(objName) +
                              "\" storage=\"" + escapeKagAttr(file) + "\"]\n";
                }
              } else if (cls == "character" && !objName.empty()) {
                const std::string file =
                    item["redraw"]["imageFile"]["file"].asString();
                int xpos = 0, ypos = 0;
                const auto &actions = item["action"];
                if (actions.isList()) {
                  for (const auto &act : actions.asList()) {
                    if (act.isList() && act.size() >= 2) {
                      const std::string prop = act[0].asString();
                      if (prop == "xpos" && act[1].isNumber())
                        xpos = static_cast<int>(act[1].asInt());
                      else if (prop == "ypos" && act[1].isNumber())
                        ypos = static_cast<int>(act[1].asInt());
                    }
                  }
                }
                if (!file.empty()) {
                  script += "[chara_show layer=\"" + escapeKagAttr(objName) +
                            "\" storage=\"" + escapeKagAttr(file) + "\" x=\"" +
                            std::to_string(xpos) + "\" y=\"" +
                            std::to_string(ypos) + "\"]\n";
                }
              }
            }
          }
        };

        if (lines.isList()) {
          for (const auto &line : lines.asList()) {
            if (line.isInt()) {
              const std::int64_t idx = line.asInt();
              if (idx >= 1 && texts.isList() &&
                  static_cast<std::size_t>(idx) <= texts.size()) {
                const auto &tx = texts[static_cast<std::size_t>(idx - 1)];
                if (tx.isList() && tx.size() >= 3) {
                  const std::string primary = tx[0].asString();
                  const std::string combined = tx[1].asString();
                  const auto &langs = tx[2];
                  std::string enDisp;
                  std::string rawText;
                  if (langs.isList() && langs.size() > 0) {
                    if (langs.size() > 1 && langs[1].isList() &&
                        langs[1].size() >= 1)
                      enDisp = langs[1][0].asString();
                    std::size_t chosen = 0;
                    if (langs.size() > 2 && langs[2].isList() &&
                        langs[2].size() >= 2 &&
                        !langs[2][1].asString().empty()) {
                      chosen = 2;
                    } else if (langs.size() > 1 && langs[1].isList() &&
                               langs[1].size() >= 2 &&
                               !langs[1][1].asString().empty()) {
                      chosen = 1;
                    }
                    if (langs[chosen].isList() && langs[chosen].size() >= 2)
                      rawText = langs[chosen][1].asString();
                  }
                  const std::string cleanText = stripPsbFontTags(rawText);
                  const auto [speakerName, nameplate] =
                      resolveNekoparaSpeaker(primary, combined, enDisp);
                  std::string voiceFile;
                  if (tx.size() >= 4 && tx[3].isList() && tx[3].size() > 0) {
                    for (const auto &v : tx[3].asList()) {
                      const std::string vf = v["voice"].asString();
                      if (!vf.empty()) {
                        if (!voiceFile.empty())
                          voiceFile += ",";
                        voiceFile += vf;
                      }
                    }
                  }
                  if (!cleanText.empty()) {
                    script += "[cm";
                    if (!speakerName.empty())
                      script +=
                          " speaker=\"" + escapeKagAttr(speakerName) + "\"";
                    if (!nameplate.empty())
                      script +=
                          " nameplate=\"" + escapeKagAttr(nameplate) + "\"";
                    if (!voiceFile.empty())
                      script += " voice=\"" + escapeKagAttr(voiceFile) + "\"";
                    script += " text=\"" + escapeKagAttr(cleanText) + "\"]\n";
                  }
                }
              }
            } else if (line.isString()) {
              const std::string flag = line.asString();
              if (!flag.empty())
                script +=
                    "[set name=\"" + escapeKagAttr(flag) + "\" value=\"1\"]\n";
            } else if (line.isList() && line.size() > 0 && line[0].isString()) {
              const std::string cmd = line[0].asString();
              if (cmd == "envupdate") {
                for (std::size_t i = 1; i + 1 < line.size(); ++i) {
                  if (line[i].isString() &&
                      (line[i].asString() == "update" ||
                       line[i].asString() == "pretrans")) {
                    emitEnvUpdateList(line[i + 1]);
                  }
                }
              } else if (cmd == "wait") {
                for (std::size_t i = 1; i + 1 < line.size(); ++i) {
                  if (line[i].asString() == "time") {
                    const auto ms = line[i + 1].toInt(0);
                    if (ms > 0)
                      script += "[wait time=\"" + std::to_string(ms) + "\"]\n";
                  }
                }
              } else if (cmd == "msgoff") {
                script += "[msgoff]\n";
              } else if (cmd == "meswinload") {
                script += "[meswinload]\n";
              }
            }
          }
        }

        const auto &nexts = scene["nexts"];
        if (nexts.isList() && nexts.size() > 0 && nexts[0].isDict()) {
          const std::string storage = nexts[0]["storage"].asString();
          const std::string target = nexts[0]["target"].asString();
          const std::int64_t nextType = nexts[0]["type"].toInt(0);
          if (nextType == 0 && !storage.empty()) {
            script += "[jump storage=\"" + escapeKagAttr(storage) + "\"";
            if (!target.empty())
              script += " target=\"" + escapeKagAttr(target) + "\"";
            script += "]\n";
          } else {
            script += "[s]\n";
          }
        }
      }
      if (!script.empty())
        return script;
    }
  } catch (const std::exception &) {
    // Fall back to string scan for minimal synthetic non-KAGEnv PSB fixtures.
  }

  try {
    std::size_t i = 0;
    while (i < psbData.size()) {
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
          unsigned char c = psbData[i];
          if (c == 0)
            break;
          dialogue.push_back(static_cast<char>(c));
          ++i;
        }
        dialogue = stripPsbFontTags(std::move(dialogue));
        if (!dialogue.empty()) {
          script += "[cm text=\"";
          script += escapeKagAttr(dialogue);
          script += "\"]\n";
        }
        continue;
      }
      ++i;
    }
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Kirikiri: extractPsbScript exception: %s", e.what());
  }
  return script;
}

} // namespace

const KirikiriAssets::Xp3Entry *
KirikiriAssets::findXp3Entry(const std::string &id, bool *collision) const {
  if (collision)
    *collision = false;
  const std::string normalized = resolveId(id);
  const std::string folded = foldedArchivePath(normalized);

  // Later patch archives take precedence over earlier data archives. Within
  // each layer prefer the exact spelling, then a unique folded match.
  for (auto archive = archives.rbegin(); archive != archives.rend();
       ++archive) {
    const Xp3Entry *candidate = nullptr;
    bool ambiguous = false;
    // A later index chunk may replace an entry earlier in the same archive.
    // Look backwards so chain updates take precedence just like patch XP3s.
    for (auto item = archive->second.rbegin(); item != archive->second.rend();
         ++item) {
      const auto &entry = *item;
      if (resolveId(entry.name) == normalized)
        return &entry;
      if (foldedArchivePath(entry.name) == folded) {
        if (candidate && resolveId(candidate->name) != resolveId(entry.name))
          ambiguous = true;
        else if (!candidate)
          candidate = &entry;
      }
    }
    if (ambiguous) {
      if (collision)
        *collision = true;
      return nullptr;
    }
    if (candidate)
      return candidate;
  }
  return nullptr;
}

bool KirikiriAssets::containsAsset(const std::string &id) const {
  if (base->contains(id))
    return true;
  const std::string normalized = resolveId(id);
  if (normalized != id && base->contains(normalized))
    return true;
  return findXp3Entry(normalized) != nullptr;
}

std::vector<std::uint8_t>
KirikiriAssets::readAsset(const std::string &id) const {
  if (base->contains(id))
    return base->read(id);
  const std::string normalized = resolveId(id);
  if (normalized != id && base->contains(normalized))
    return base->read(normalized);

  bool collision = false;
  const Xp3Entry *entry = findXp3Entry(normalized, &collision);
  if (collision)
    throw std::runtime_error("Ambiguous XP3 asset path (case collision): " +
                             id);
  if (!entry)
    throw std::runtime_error("Asset not found: " + id);
  return extractXp3Entry(*entry);
}

std::string KirikiriAssets::decodeTextAsset(const std::string &id,
                                            const std::vector<Uint8> &bytes,
                                            const char *encoding) const {
  auto data = bytes;
  // MDF container: "mdf\0" + zlib-compressed PSB.  Decompress for any source.
  if (data.size() >= 8 && std::memcmp(data.data(), kMdfMagic, 4) == 0)
    data = decompressMdf(data.data(), data.size());

  // PSB scenario (.scn): extract readable KAG/script text from E-Mote PSB.
  if (data.size() >= 4 && std::memcmp(data.data(), kPsbMagic, 4) == 0 &&
      id.size() >= 4 && id.substr(id.size() - 4) == ".scn") {
    const auto text = extractPsbScript(data);
    return text;
  }

  return AssetProvider::decodeTextAsset(id, data, encoding);
}

std::vector<std::string>
KirikiriAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result = base->list(prefix);
  const std::string foldedPrefix = foldedArchivePath(prefix);
  for (const auto &[name, entries] : archives) {
    for (const auto &e : entries) {
      if (foldedArchivePath(e.name).rfind(foldedPrefix, 0) == 0)
        result.push_back(resolveId(e.name));
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
  // SDL/FFmpeg do not understand KiriKiri TLG5. Decode directly from the XP3
  // provider, including images whose extension is changed by a patch.
  // For ordinary PNG/JPEG/WebP/BMP, keep the shared decoder pipeline.
  const auto dot = id.find_last_of('.');
  if (dot != std::string::npos) {
    std::string extension = id.substr(dot);
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".tlg" || extension == ".tlg5" || extension == ".tlg6") {
      const auto bytes = read(id);
      const auto img = Kotonoha::Formats::decodeTlgImage(bytes);
      Kotonoha::SurfacePtr surface(SDL_CreateSurface(
          static_cast<int>(img.width), static_cast<int>(img.height),
          SDL_PIXELFORMAT_RGBA32));
      if (surface && !img.rgba.empty())
        std::memcpy(surface->pixels, img.rgba.data(), img.rgba.size());
      return surface;
    }
    if (extension == ".pimg" || extension == ".psb") {
      const auto bytes = read(id);
      if (PsbFile::isMdfBytes(bytes.data(), bytes.size()) ||
          PsbFile::isPsbBytes(bytes.data(), bytes.size())) {
        const auto psb = PsbFile::parse(bytes);
        const auto &root = psb.root();
        const int w = static_cast<int>(root["width"].toInt(0));
        const int h = static_cast<int>(root["height"].toInt(0));
        if (w > 0 && h > 0 && root["pixel"].isChunk()) {
          const auto chunk = psb.chunkBytes(root["pixel"].asChunk().index);
          if (chunk.size() ==
              static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u) {
            Kotonoha::SurfacePtr surface(
                SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
            if (surface) {
              for (int y = 0; y < h; ++y) {
                const auto *src =
                    chunk.data() + static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(w) * 4u;
                auto *dst = static_cast<std::uint8_t *>(surface->pixels) +
                            static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(surface->pitch);
                for (int x = 0; x < w; ++x) {
                  dst[x * 4 + 0] = src[x * 4 + 2];
                  dst[x * 4 + 1] = src[x * 4 + 1];
                  dst[x * 4 + 2] = src[x * 4 + 0];
                  dst[x * 4 + 3] = src[x * 4 + 3];
                }
              }
              return surface;
            }
          }
        }
      }
    }
  }
  return AssetProvider::loadSurfaceAsset(id);
}

std::string KirikiriAssets::resolveId(const std::string &id) const {
  std::string out = id;
  for (auto &c : out)
    if (c == '\\')
      c = '/';
  return out;
}

void KirikiriAssets::mountXp3(const std::filesystem::path &path) {
  if (path.empty())
    throw std::invalid_argument("Empty XP3 source id");
  auto id = path;
  if (id.is_absolute()) {
    if (root.empty())
      throw std::invalid_argument("XP3 sources must have virtual IDs");
    id = id.lexically_relative(root);
    if (id.empty() || *id.begin() == "..")
      throw std::invalid_argument("XP3 source escaped its mounted root");
  }
  const auto logical = id.generic_string();
  const auto name = id.filename().string();
  if (std::any_of(archives.begin(), archives.end(),
                  [&](const auto &archive) { return archive.first == name; }))
    return;
  if (!readXp3(logical))
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Kirikiri: cannot index XP3 source %s", logical.c_str());
}

void KirikiriAssets::mountAllXp3() {
  std::vector<std::string> names;
  for (const auto &entry : source_->list("")) {
    if (entry.type != Kotonoha::Filesystem::FileType::Regular)
      continue;
    const auto extension =
        std::filesystem::path(entry.path).extension().string();
    std::string lower = extension;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower == ".xp3")
      names.push_back(entry.path);
  }
  // HTTP(S) without a manifest has only a PARTIAL directory listing: the
  // detector may already have discovered data.xp3 while voice/emote/etc.
  // remain invisible. Probe known archive IDs even when names is nonempty.
  // Native directory listings are complete, so avoid extra probes there.
#ifdef __EMSCRIPTEN__
  constexpr bool probeKnownArchives = true;
#else
  const bool probeKnownArchives = names.empty();
#endif
  if (probeKnownArchives) {
    for (const char *id :
         {"data.xp3", "emote.xp3", "evimage.xp3", "voice.xp3", "bgimage.xp3",
          "image.xp3", "sound.xp3", "system.xp3", "patch.xp3", "patch2.xp3",
          "patch3.xp3", "startup.xp3", "scenario.xp3", "data/data.xp3",
          "data/patch.xp3"}) {
      const auto alreadyListed = std::any_of(
          names.begin(), names.end(), [id](const std::string &existing) {
            auto lower = existing;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) {
                             return static_cast<char>(std::tolower(c));
                           });
            return lower == id;
          });
      if (alreadyListed)
        continue;
      if (source_->exists(id) &&
          source_->stat(id).type == Kotonoha::Filesystem::FileType::Regular)
        names.emplace_back(id);
    }
  }
  const auto rank = [](std::string name) {
    std::transform(
        name.begin(), name.end(), name.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (name.find("patch") != std::string::npos)
      return 2;
    return name == "data.xp3" ? 0 : 1;
  };
  std::sort(names.begin(), names.end(), [&](const auto &a, const auto &b) {
    const auto an = std::filesystem::path(a).filename().string();
    const auto bn = std::filesystem::path(b).filename().string();
    return std::pair(rank(an), an) < std::pair(rank(bn), bn);
  });
  for (const auto &id : names) {
    try {
      mountXp3(std::filesystem::path(id));
    } catch (const std::exception &error) {
      // One unsupported/corrupt patch must not prevent other XP3 archives
      // from reaching the launcher and its diagnostic title screen.
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: cannot mount XP3 %s: %s", id.c_str(),
                  error.what());
    }
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
  if (result.empty()) {
    // Pure loose-script web games also lack a remote index. This is a
    // bounded set of known KAG entrypoints, not HTTP directory enumeration.
    for (const char *id :
         {"startup.ks", "start.ks", "first.ks", "main.ks", "title.ks",
          "scenario/startup.ks", "scenario/first.ks", "scenario/main.ks"}) {
      if (containsAsset(id))
        result.emplace_back(id);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string KirikiriAssets::startupScenario() const {
  const auto files = scenarioFiles();
  if (files.empty())
    return {};
  static const std::vector<std::string> preferred = {
      "startup.ks",        "start.ks",        "first.ks",
      "main.ks",           "title.ks",        "scenario/startup.ks",
      "scenario/first.ks", "scenario/main.ks"};
  for (const auto &wanted : preferred)
    for (const auto &file : files) {
      auto lower = file;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      });
      if (lower == wanted ||
          std::filesystem::path(lower).filename().string() == wanted)
        return file;
    }
  // Check scenelist.scn (e.g. VNX+/scn/scenelist.scn) for the canonical first
  // chapter entry before falling back to alphabetical scenario sorting.
  for (const char *scnListPath :
       {"scenelist.scn", "scn/scenelist.scn", "VNX+/scn/scenelist.scn"}) {
    if (!containsAsset(scnListPath))
      continue;
    try {
      const auto psb = PsbFile::parse(readAsset(scnListPath));
      const auto &list = psb.root()["list"];
      if (list.isList() && list.size() > 0) {
        const std::string firstStorage = list[0]["storage"].asString();
        if (!firstStorage.empty()) {
          for (const auto &file : files) {
            const auto fname = std::filesystem::path(file).filename().string();
            if (fname == firstStorage || fname == firstStorage + ".scn")
              return file;
          }
        }
      }
    } catch (...) {
    }
  }
  // Translation packs such as VNX+ expose numbered .ks.scn chapters alongside
  // sysmenu.ks. Prefer the first non-sysmenu scenario file.
  for (const auto &file : files) {
    auto lower = std::filesystem::path(file).filename().string();
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower != "sysmenu.ks" && lower != "sysmenu.ks.scn")
      return file;
  }
  return files.front();
}

bool KirikiriAssets::readXp3(const std::string &id) {
  auto f = source_->open(id);
  if (!f || f->size() < 19)
    return false;
  const auto fileSize = static_cast<std::uint64_t>(f->size());
  constexpr std::uint64_t kMaxIndexBytes = 64ull * 1024 * 1024;
  constexpr std::size_t kMaxIndexParts = 64;

  const auto readAt = [&](std::uint64_t pos, void *out, std::size_t size) {
    if (pos > fileSize || size > fileSize - pos ||
        pos > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()) ||
        f->seek(static_cast<std::int64_t>(pos),
                Kotonoha::Filesystem::SeekOrigin::Begin) < 0 ||
        (size && f->read(out, size) != static_cast<std::int64_t>(size)))
      throw std::runtime_error("XP3 truncated or invalid storage range: " + id);
  };
  const auto read64At = [&](std::uint64_t pos) {
    std::uint8_t encoded[8];
    readAt(pos, encoded, sizeof(encoded));
    return readU64LE(encoded);
  };

  std::uint8_t magic[11];
  readAt(0, magic, sizeof(magic));
  if (std::memcmp(magic, kXp3Magic, sizeof(magic)) != 0)
    return false;

  // Standard XP3 files start with one 64-bit absolute index offset. The
  // historical Kotonoha test archive has an additional v2 header, but its
  // first index offset is still correct; do not assume a version field.
  std::uint64_t indexOffset = read64At(11);
  std::set<std::uint64_t> visited;
  std::vector<Xp3Entry> entries;
  for (std::size_t part = 0; part < kMaxIndexParts; ++part) {
    if (indexOffset >= fileSize || !visited.insert(indexOffset).second)
      throw std::runtime_error("XP3 invalid or cyclic index chain: " + id);
    std::uint8_t flags = 0;
    readAt(indexOffset, &flags, 1);
    if (flags & ~std::uint8_t(0x81))
      throw std::runtime_error("XP3 unsupported index flags: " + id);
    const bool compressed = (flags & 1) != 0;
    const bool continued = (flags & 0x80) != 0;
    const auto storedSize = read64At(indexOffset + 1);
    std::uint64_t originalSize = storedSize;
    std::uint64_t payloadOffset = indexOffset + 9;
    if (compressed) {
      originalSize = read64At(indexOffset + 9);
      payloadOffset += 8;
    } else if (indexOffset <= fileSize - 17 && storedSize <= kMaxIndexBytes) {
      // Old synthetic XP3 v2 fixtures included both size fields for raw
      // indices. Accept this variant only when its extra eight bytes precede
      // a valid-looking File chunk; canonical raw indices use just one.
      std::uint8_t first[4] = {};
      std::uint8_t legacy[4] = {};
      if (payloadOffset <= fileSize - sizeof(first))
        readAt(payloadOffset, first, sizeof(first));
      if (indexOffset + 17 <= fileSize - sizeof(legacy))
        readAt(indexOffset + 17, legacy, sizeof(legacy));
      if (std::memcmp(first, "File", 4) != 0 &&
          std::memcmp(legacy, "File", 4) == 0 &&
          read64At(indexOffset + 9) == storedSize)
        payloadOffset += 8;
    }
    if (storedSize > kMaxIndexBytes || originalSize > kMaxIndexBytes ||
        payloadOffset > fileSize || storedSize > fileSize - payloadOffset)
      throw std::runtime_error("XP3 index exceeds size limit: " + id);
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(storedSize));
    readAt(payloadOffset, encoded.data(), encoded.size());
    std::vector<std::uint8_t> tableData =
        compressed ? zlibInflate(encoded.data(), encoded.size(),
                                 static_cast<std::size_t>(originalSize))
                   : std::move(encoded);

    // Each index contains one or more nested File(info, segm, adlr) chunks.
    // The segm section holds N records of 28 bytes each, not one record.
    std::size_t tp = 0;
    while (tp < tableData.size()) {
      if (tableData.size() - tp < 12) {
        // Some packers align a decoded index with a few zero bytes.
        if (std::all_of(tableData.begin() + tp, tableData.end(),
                        [](std::uint8_t value) { return value == 0; }))
          break;
        throw std::runtime_error("XP3 truncated File chunk: " + id);
      }
      const auto tag = readU32LE(tableData.data() + tp);
      const auto chunkSize = readU64LE(tableData.data() + tp + 4);
      tp += 12;
      if (chunkSize > tableData.size() - tp)
        throw std::runtime_error("XP3 File chunk exceeds index: " + id);
      const auto *chunk = tableData.data() + tp;
      tp += static_cast<std::size_t>(chunkSize);
      if (tag != 0x656c6946u) // "File"
        continue;
      Xp3Entry entry;
      entry.archiveId = id;
      std::size_t sp = 0;
      bool hasInfo = false, hasSegments = false;
      std::uint64_t storedTotal = 0, originalTotal = 0;
      std::uint64_t declaredStored = 0;
      while (sp < chunkSize) {
        if (chunkSize - sp < 12)
          throw std::runtime_error("XP3 truncated sub-chunk: " + id);
        const auto subTag = readU32LE(chunk + sp);
        const auto subSize = readU64LE(chunk + sp + 4);
        sp += 12;
        if (subSize > chunkSize - sp)
          throw std::runtime_error("XP3 sub-chunk exceeds File: " + id);
        const auto *sub = chunk + sp;
        sp += static_cast<std::size_t>(subSize);
        if (subTag == 0x6f666e69u) { // "info"
          if (subSize < 22)
            throw std::runtime_error("XP3 truncated info: " + id);
          const auto flags = readU32LE(sub);
          // Protected flags are advisory metadata; some retail packages use
          // them with already-plain contents. Keep the file discoverable.
          (void)flags;
          entry.originalSize = readU64LE(sub + 4);
          declaredStored = readU64LE(sub + 12);
          const auto nameUnits = readU16LE(sub + 20);
          if (static_cast<std::uint64_t>(nameUnits) * 2 > subSize - 22)
            throw std::runtime_error("XP3 truncated Unicode name: " + id);
          std::size_t namePos = 22;
          entry.name = readUtf16Le(sub, namePos, nameUnits);
          hasInfo = true;
          // Compare this declared size with the sum of all stored spans.
        } else if (subTag == 0x6d676573u) { // "segm"
          if (subSize == 0 || subSize % 28 != 0)
            throw std::runtime_error("XP3 invalid segment array: " + id);
          for (std::size_t pos = 0; pos < subSize; pos += 28) {
            Xp3Segment segment;
            const auto method = readU32LE(sub + pos) & 7u;
            if (method > 1)
              throw std::runtime_error("XP3 unknown segment method: " + id);
            segment.compressed = method == 1;
            segment.offset = readU64LE(sub + pos + 4);
            segment.originalSize = readU64LE(sub + pos + 12);
            segment.storedSize = readU64LE(sub + pos + 20);
            if (segment.offset > fileSize ||
                segment.storedSize > fileSize - segment.offset ||
                segment.originalSize >
                    std::numeric_limits<std::uint64_t>::max() - originalTotal ||
                segment.storedSize >
                    std::numeric_limits<std::uint64_t>::max() - storedTotal ||
                (!segment.compressed &&
                 segment.originalSize != segment.storedSize))
              throw std::runtime_error("XP3 invalid segment range: " + id);
            originalTotal += segment.originalSize;
            storedTotal += segment.storedSize;
            entry.segments.push_back(segment);
          }
          hasSegments = true;
        }
      }
      if (hasInfo && hasSegments && !entry.name.empty()) {
        // Protected archives (such as NEKOPARA) may carry advisory size
        // mismatch markers in their info header. Trust the actual segment sum.
        if (originalTotal != entry.originalSize)
          entry.originalSize = originalTotal;
        entries.push_back(std::move(entry));
      }
    }

    if (!continued) {
      std::size_t segmentCount = 0;
      for (const auto &entry : entries)
        segmentCount += entry.segments.size();
      SDL_Log(
          "KiriKiri: indexed %s: %zu assets, %zu segments, %zu index blocks",
          id.c_str(), entries.size(), segmentCount, visited.size());
      archives.emplace_back(std::filesystem::path(id).filename().string(),
                            std::move(entries));
      return true;
    }
    const auto nextPointer = payloadOffset + storedSize;
    if (nextPointer > fileSize || fileSize - nextPointer < 8)
      throw std::runtime_error("XP3 truncated index continuation: " + id);
    indexOffset = read64At(nextPointer);
  }
  throw std::runtime_error("XP3 index chain limit exceeded: " + id);
}

std::vector<std::uint8_t>
KirikiriAssets::extractXp3Entry(const Xp3Entry &entry) const {
  // A storage can be split into interleaved raw/zlib segments. Decode each
  // segment on demand, in declared order, and concatenate its exact bytes.
  constexpr std::uint64_t kMaxDecodedFile = 512ull * 1024 * 1024;
  if (entry.originalSize > kMaxDecodedFile)
    throw std::runtime_error("XP3 asset too large for in-memory read: " +
                             entry.name);
  auto f = source_->open(entry.archiveId);
  if (!f || f->size() < 0)
    throw std::runtime_error("Cannot open XP3 source: " + entry.archiveId);
  const auto fileSize = static_cast<std::uint64_t>(f->size());
  std::vector<std::uint8_t> result;
  result.reserve(static_cast<std::size_t>(entry.originalSize));
  for (const auto &segment : entry.segments) {
    if (segment.offset > fileSize ||
        segment.storedSize > fileSize - segment.offset ||
        segment.storedSize > kMaxDecodedFile ||
        segment.originalSize > kMaxDecodedFile)
      throw std::runtime_error("XP3 segment out of bounds: " + entry.name);
    if (f->seek(static_cast<std::int64_t>(segment.offset),
                Kotonoha::Filesystem::SeekOrigin::Begin) < 0)
      throw std::runtime_error("Cannot seek XP3 segment: " + entry.name);
    std::vector<std::uint8_t> packed(
        static_cast<std::size_t>(segment.storedSize));
    if (!packed.empty() && f->read(packed.data(), packed.size()) !=
                               static_cast<std::int64_t>(packed.size()))
      throw std::runtime_error("Cannot read XP3 segment: " + entry.name);
    auto decoded =
        segment.compressed
            ? zlibInflate(packed.data(), packed.size(),
                          static_cast<std::size_t>(segment.originalSize))
            : std::move(packed);
    if (decoded.size() != segment.originalSize ||
        decoded.size() > entry.originalSize - result.size())
      throw std::runtime_error("XP3 invalid decoded segment size: " +
                               entry.name);
    result.insert(result.end(), decoded.begin(), decoded.end());
  }
  if (result.size() != entry.originalSize)
    throw std::runtime_error("XP3 incomplete segmented asset: " + entry.name);
  return result;
}

} // namespace Kotonoha::Providers
