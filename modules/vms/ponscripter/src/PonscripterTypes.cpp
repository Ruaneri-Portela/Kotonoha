// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterTypes.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace Ponscripter {
namespace {

std::string trim(std::string_view sv) {
  std::size_t start = 0;
  while (start < sv.size() &&
         std::isspace(static_cast<unsigned char>(sv[start])))
    ++start;
  std::size_t end = sv.size();
  while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
    --end;
  return std::string(sv.substr(start, end - start));
}

std::string normalizeAssetPath(std::string_view raw) {
  std::string out(raw);
  std::replace(out.begin(), out.end(), '\\', '/');
  while (!out.empty() && out.front() == '/')
    out.erase(out.begin());
  return out;
}

int parseIntOr(std::string_view sv, int fallback) {
  sv = std::string_view(sv);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    sv.remove_prefix(1);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
    sv.remove_suffix(1);
  if (sv.empty())
    return fallback;
  int value = fallback;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  (void)ptr;
  return (ec == std::errc{}) ? value : fallback;
}

} // namespace

bool parseHexColor24(std::string_view text, std::uint32_t &outRgb24) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  if (!text.empty() && text.front() == '#')
    text.remove_prefix(1);
  if (text.size() < 6)
    return false;
  std::uint32_t rgb = 0;
  for (int i = 0; i < 6; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::uint32_t digit = 0;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F')
      digit = 10 + (c - 'A');
    else
      return false;
    rgb = (rgb << 4) | digit;
  }
  outRgb24 = rgb;
  return true;
}

TaggedImageSpec parseTaggedString(std::string_view raw,
                                  TransMode defaultTrans) {
  TaggedImageSpec spec;
  spec.rawTag = std::string(raw);
  std::string trimmed = trim(raw);
  if (trimmed.empty()) {
    spec.trans = TransMode::None;
    return spec;
  }

  // Direct color keywords or #RRGGBB
  std::string lower = trimmed;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "black") {
    spec.trans = TransMode::DirectColor;
    spec.color = 0x000000u;
    return spec;
  }
  if (lower == "white") {
    spec.trans = TransMode::DirectColor;
    spec.color = 0xFFFFFFu;
    return spec;
  }
  if (trimmed.front() == '#') {
    std::uint32_t rgb = 0;
    if (parseHexColor24(trimmed, rgb)) {
      spec.trans = TransMode::DirectColor;
      spec.color = rgb;
      return spec;
    }
  }

  if (trimmed.front() != ':') {
    spec.trans = defaultTrans;
    spec.imagePath = normalizeAssetPath(trimmed);
    return spec;
  }

  // Starts with ':'
  if (trimmed.size() < 2) {
    spec.trans = defaultTrans;
    return spec;
  }

  const char modeCh =
      static_cast<char>(std::tolower(static_cast<unsigned char>(trimmed[1])));
  if (modeCh == 's') {
    // String sprite: `:s[/w,h,s][;][#RRGGBB][#hoverRRGGBB]^<text>` or `"text"`
    spec.trans = TransMode::String;
    std::size_t pos = 2;
    if (pos < trimmed.size() && trimmed[pos] == '/') {
      ++pos;
      const std::size_t semiOrHash = trimmed.find_first_of(";#^\"", pos);
      const std::string_view metrics =
          (semiOrHash == std::string::npos)
              ? std::string_view(trimmed).substr(pos)
              : std::string_view(trimmed).substr(pos, semiOrHash - pos);
      const std::size_t c1 = metrics.find(',');
      const std::size_t c2 = (c1 == std::string_view::npos)
                                 ? std::string_view::npos
                                 : metrics.find(',', c1 + 1);
      if (c1 == std::string_view::npos) {
        spec.fontWidth = parseIntOr(metrics, 20);
        spec.fontHeight = spec.fontWidth;
      } else {
        spec.fontWidth = parseIntOr(metrics.substr(0, c1), 20);
        if (c2 == std::string_view::npos) {
          spec.fontHeight = parseIntOr(metrics.substr(c1 + 1), spec.fontWidth);
        } else {
          spec.fontHeight =
              parseIntOr(metrics.substr(c1 + 1, c2 - (c1 + 1)), spec.fontWidth);
          spec.fontSpacing = parseIntOr(metrics.substr(c2 + 1), 0);
        }
      }
      pos = (semiOrHash == std::string::npos) ? trimmed.size() : semiOrHash;
    }

    // Optional ';'
    if (pos < trimmed.size() && trimmed[pos] == ';')
      ++pos;

    // Optional colors #RRGGBB[#hoverRRGGBB]
    int colorCount = 0;
    while (pos < trimmed.size() && trimmed[pos] == '#') {
      if (pos + 7 <= trimmed.size()) {
        std::uint32_t rgb = 0;
        if (parseHexColor24(std::string_view(trimmed).substr(pos, 7), rgb)) {
          if (colorCount == 0) {
            spec.color = rgb;
          } else if (colorCount == 1) {
            spec.hoverColor = rgb;
            spec.hasHoverColor = true;
            spec.numCells = 2;
            spec.loopMode = 3;
          }
          ++colorCount;
          pos += 7;
          continue;
        }
      }
      break;
    }

    // Optional '^' or '"'
    if (pos < trimmed.size() && (trimmed[pos] == '^' || trimmed[pos] == '"')) {
      const char delim = trimmed[pos++];
      spec.text = trimmed.substr(pos);
      if (!spec.text.empty() && spec.text.back() == delim)
        spec.text.pop_back();
    } else if (pos < trimmed.size()) {
      spec.text = trimmed.substr(pos);
    }
    return spec;
  }

  switch (modeCh) {
  case 'a':
    spec.trans = TransMode::Alpha;
    break;
  case 'l':
    spec.trans = TransMode::TopLeft;
    break;
  case 'r':
    spec.trans = TransMode::TopRight;
    break;
  case 'c':
    spec.trans = TransMode::Copy;
    break;
  case 'm':
    spec.trans = TransMode::MaskFile;
    break;
  default:
    spec.trans = defaultTrans;
    break;
  }

  std::size_t pos = 2;
  if (spec.trans == TransMode::MaskFile) {
    // :m;mask_path;image_path
    if (pos < trimmed.size() && trimmed[pos] == ';')
      ++pos;
    const std::size_t nextSemi = trimmed.find(';', pos);
    if (nextSemi != std::string::npos) {
      spec.maskPath = normalizeAssetPath(
          std::string_view(trimmed).substr(pos, nextSemi - pos));
      spec.imagePath =
          normalizeAssetPath(std::string_view(trimmed).substr(nextSemi + 1));
    } else {
      spec.imagePath =
          normalizeAssetPath(std::string_view(trimmed).substr(pos));
    }
    return spec;
  }

  // Optional `/cells,duration,loopMode`
  if (pos < trimmed.size() && trimmed[pos] == '/') {
    ++pos;
    const std::size_t semi = trimmed.find(';', pos);
    const std::string_view animSpec =
        (semi == std::string::npos)
            ? std::string_view(trimmed).substr(pos)
            : std::string_view(trimmed).substr(pos, semi - pos);
    const std::size_t c1 = animSpec.find(',');
    const std::size_t c2 = (c1 == std::string_view::npos)
                               ? std::string_view::npos
                               : animSpec.find(',', c1 + 1);
    if (c1 == std::string_view::npos) {
      spec.numCells = std::max(1, parseIntOr(animSpec, 1));
    } else {
      spec.numCells = std::max(1, parseIntOr(animSpec.substr(0, c1), 1));
      if (c2 == std::string_view::npos) {
        spec.durationMs = std::max(0, parseIntOr(animSpec.substr(c1 + 1), 0));
      } else {
        spec.durationMs =
            std::max(0, parseIntOr(animSpec.substr(c1 + 1, c2 - (c1 + 1)), 0));
        spec.loopMode =
            std::clamp(parseIntOr(animSpec.substr(c2 + 1), 0), 0, 3);
      }
    }
    pos = (semi == std::string::npos) ? trimmed.size() : semi;
  }

  if (pos < trimmed.size() && trimmed[pos] == ';')
    ++pos;

  std::string pathPart = trim(std::string_view(trimmed).substr(pos));
  if (pathPart.size() >= 2 && pathPart.front() == '"' &&
      pathPart.back() == '"') {
    pathPart = pathPart.substr(1, pathPart.size() - 2);
  }
  if (!pathPart.empty() && pathPart.front() == '#') {
    std::uint32_t rgb = 0;
    if (parseHexColor24(pathPart, rgb)) {
      spec.trans = TransMode::DirectColor;
      spec.color = rgb;
      return spec;
    }
  }
  spec.imagePath = normalizeAssetPath(pathPart);
  return spec;
}

} // namespace Ponscripter
