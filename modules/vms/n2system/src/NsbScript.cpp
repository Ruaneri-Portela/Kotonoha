// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/NsbScript.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace N2System {
namespace {

std::uint16_t readLe16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}

std::uint32_t readLe32(const std::uint8_t *p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void appendUtf8(std::string &out, std::uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0x10FFFFu) {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

std::string trim(std::string_view sv) {
  std::size_t b = 0;
  while (b < sv.size() &&
         (sv[b] == ' ' || sv[b] == '\t' || sv[b] == '\r' || sv[b] == '\n'))
    ++b;
  std::size_t e = sv.size();
  while (e > b && (sv[e - 1] == ' ' || sv[e - 1] == '\t' || sv[e - 1] == '\r' ||
                   sv[e - 1] == '\n'))
    --e;
  return std::string(sv.substr(b, e - b));
}

std::string normalizePathLower(std::string_view raw) {
  std::string out(raw);
  std::replace(out.begin(), out.end(), '\\', '/');
  while (!out.empty() && out.front() == '/')
    out.erase(out.begin());
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + ('a' - 'A'));
  }
  return out;
}

} // namespace

std::string NsbScript::decodeUtf16Le(const std::uint8_t *data,
                                     std::size_t byteLen) {
  std::string out;
  out.reserve(byteLen);
  std::size_t i = 0;
  // Skip UTF-16LE BOM if present.
  if (byteLen >= 2 && data[0] == 0xFF && data[1] == 0xFE)
    i = 2;
  while (i + 1 < byteLen) {
    const std::uint16_t u1 = readLe16(data + i);
    i += 2;
    if (u1 >= 0xD800u && u1 <= 0xDBFFu && i + 1 < byteLen) {
      const std::uint16_t u2 = readLe16(data + i);
      if (u2 >= 0xDC00u && u2 <= 0xDFFFu) {
        i += 2;
        const std::uint32_t cp =
            0x10000u + ((std::uint32_t(u1 - 0xD800u) << 10) | (u2 - 0xDC00u));
        appendUtf8(out, cp);
        continue;
      }
    }
    appendUtf8(out, u1);
  }
  return out;
}

std::string NsbScript::nssToNsbPath(std::string_view nssPath) {
  std::string p = normalizePathLower(nssPath);
  if (p.size() >= 4 && p.compare(p.size() - 4, 4, ".nss") == 0) {
    p.replace(p.size() - 4, 4, ".nsb");
  } else if (p.size() < 4 || p.compare(p.size() - 4, 4, ".nsb") != 0) {
    p += ".nsb";
  }
  return p;
}

std::string NsbScript::nssToMapPath(std::string_view nssPath) {
  std::string p = normalizePathLower(nssPath);
  if (p.size() >= 4 && (p.compare(p.size() - 4, 4, ".nss") == 0 ||
                        p.compare(p.size() - 4, 4, ".nsb") == 0)) {
    p.replace(p.size() - 4, 4, ".map");
  } else if (p.size() < 4 || p.compare(p.size() - 4, 4, ".map") != 0) {
    p += ".map";
  }
  return p;
}

SystemIniConfig
SystemIniConfig::parseUtf16Le(const std::vector<std::uint8_t> &bytes) {
  if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    return parseUtf8(NsbScript::decodeUtf16Le(bytes.data(), bytes.size()));
  }
  // Heuristic: if second byte is 0, treat as UTF-16LE without BOM.
  if (bytes.size() >= 2 && bytes[1] == 0) {
    return parseUtf8(NsbScript::decodeUtf16Le(bytes.data(), bytes.size()));
  }
  return parseUtf8(std::string_view(
      reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

SystemIniConfig SystemIniConfig::parseUtf8(std::string_view text) {
  SystemIniConfig cfg;
  std::string section;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const auto nl = text.find('\n', pos);
    const auto end = (nl == std::string_view::npos) ? text.size() : nl;
    const std::string line = trim(text.substr(pos, end - pos));
    pos = (nl == std::string_view::npos) ? text.size() : nl + 1;

    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(std::string_view(line).substr(1, line.size() - 2));
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = trim(std::string_view(line).substr(0, eq));
    const std::string val = trim(std::string_view(line).substr(eq + 1));

    if (key == "name")
      cfg.productName = val;
    else if (key == "version")
      cfg.version = val;
    else if (key == "start")
      cfg.startScript = val;
    else if (key == "close")
      cfg.closeScript = val;
    else if (key == "menu")
      cfg.menuScript = val;
    else if (key == "title")
      cfg.windowTitle = val;
    else if (key == "script" && !val.empty())
      cfg.backlogScript = val;
    else if (key == "bgm_volume") {
      try {
        cfg.bgmVolume = std::stoi(val);
      } catch (...) {
      }
    } else if (key == "se_volume") {
      try {
        cfg.seVolume = std::stoi(val);
      } catch (...) {
      }
    } else if (key == "voice_volume") {
      try {
        cfg.voiceVolume = std::stoi(val);
      } catch (...) {
      }
    }
  }
  return cfg;
}

NsbScript NsbScript::parse(std::string scriptPath,
                           const std::vector<std::uint8_t> &nsbBytes,
                           const std::vector<std::uint8_t> &mapBytes) {
  NsbScript script;
  script.path_ = normalizePathLower(scriptPath);

  std::unordered_map<std::uint32_t, std::size_t> offsetToIndex;
  std::size_t pos = 0;
  while (pos + 8 <= nsbBytes.size()) {
    NsbInstruction insn;
    insn.byteOffset = static_cast<std::uint32_t>(pos);
    insn.line = readLe32(nsbBytes.data() + pos);
    insn.opcode = readLe16(nsbBytes.data() + pos + 4);
    const std::uint16_t argc = readLe16(nsbBytes.data() + pos + 6);
    pos += 8;

    insn.args.reserve(argc);
    for (std::uint16_t a = 0; a < argc; ++a) {
      if (pos + 4 > nsbBytes.size())
        throw std::runtime_error("N2System: truncated .nsb argument length");
      const std::uint32_t argLen = readLe32(nsbBytes.data() + pos);
      pos += 4;
      if (pos + argLen > nsbBytes.size())
        throw std::runtime_error("N2System: truncated .nsb argument payload");
      insn.args.push_back(decodeUtf16Le(nsbBytes.data() + pos, argLen));
      pos += argLen;
    }

    const std::size_t idx = script.instructions_.size();
    offsetToIndex.emplace(insn.byteOffset, idx);

    // Also index declarations directly from bytecode in case .map is omitted.
    if ((insn.opcode == 0x094 || insn.opcode == 0x095 || insn.opcode == 0x096 ||
         insn.opcode == 0x097) &&
        !insn.args.empty()) {
      script.symbols_.emplace(insn.args[0], idx);
    }

    script.instructions_.push_back(std::move(insn));
  }
  offsetToIndex.emplace(static_cast<std::uint32_t>(nsbBytes.size()),
                        script.instructions_.size());

  // Parse .map symbol table: [u32 byteOffset][u16 nameByteLen][utf16le name]
  std::size_t mpos = 0;
  while (mpos + 6 <= mapBytes.size()) {
    const std::uint32_t off = readLe32(mapBytes.data() + mpos);
    const std::uint16_t nlen = readLe16(mapBytes.data() + mpos + 4);
    mpos += 6;
    if (mpos + nlen > mapBytes.size())
      break;
    std::string sym = decodeUtf16Le(mapBytes.data() + mpos, nlen);
    mpos += nlen;

    if (sym.compare(0, 8, "include.") == 0) {
      script.includes_.push_back(normalizePathLower(sym.substr(8)));
    } else if (auto it = offsetToIndex.find(off); it != offsetToIndex.end()) {
      script.symbols_[sym] = it->second;
    }
  }

  return script;
}

std::size_t NsbScript::findSymbol(std::string_view name) const {
  auto it = symbols_.find(std::string(name));
  return it == symbols_.end() ? std::string::npos : it->second;
}

std::size_t NsbScript::findChapter(std::string_view chapterName) const {
  if (auto idx = findSymbol(chapterName); idx != std::string::npos)
    return idx;
  return findSymbol("chapter." + std::string(chapterName));
}

std::size_t NsbScript::findScene(std::string_view sceneName) const {
  if (auto idx = findSymbol(sceneName); idx != std::string::npos)
    return idx;
  return findSymbol("scene." + std::string(sceneName));
}

std::size_t NsbScript::findFunction(std::string_view functionName) const {
  if (auto idx = findSymbol(functionName); idx != std::string::npos)
    return idx;
  return findSymbol("function." + std::string(functionName));
}

} // namespace N2System
