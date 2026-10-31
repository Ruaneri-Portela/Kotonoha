#include <Kotonoha/parsers/AssStyle.hpp>
#include <ass/ass.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {
AssStyle AssStyle::parse(const std::string &ass) {
  if (ass.empty() || ass.size() > 16 * 1024 * 1024 || ass.find('\0') != std::string::npos)
    throw std::invalid_argument("ASS style file must contain 1 byte to 16 MiB of text");
  AssStyle result;
  std::istringstream input(ass);
  std::string line, section;
  bool info = false, styles = false, style = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.compare(0, 3, "\xef\xbb\xbf") == 0)
      line.erase(0, 3);
    const auto first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line[first] == '[') {
      const auto last = line.find_last_not_of(" \t");
      section = line.substr(first, last - first + 1);
      info = info || section == "[Script Info]";
      styles = styles || section == "[V4+ Styles]";
    }
    if (section == "[Script Info]" || section == "[V4+ Styles]") {
      result.header += line + '\n';
      style = style || (section == "[V4+ Styles]" &&
          first != std::string::npos && line.compare(first, 6, "Style:") == 0);
    }
  }
  if (!info || !styles || !style)
    throw std::runtime_error("ASS style file requires [Script Info] and [V4+ Styles]");
  std::unique_ptr<ASS_Library, decltype(&ass_library_done)> library(
      ass_library_init(), ass_library_done);
  if (!library)
    throw std::runtime_error("Cannot initialize ASS style parser");
  std::string bytes = result.header;
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track(
      ass_read_memory(library.get(), bytes.data(), bytes.size(), nullptr), ass_free_track);
  if (!track || track->n_styles < 2 || track->PlayResX <= 0 || track->PlayResY <= 0)
    throw std::runtime_error("Invalid ASS styles or missing PlayResX / PlayResY");
  for (int i = 1; i < track->n_styles; ++i) {
    const std::string name = track->styles[i].Name;
    if (name.empty() || name.find_first_of(",\r\n") != std::string::npos)
      throw std::runtime_error("Invalid ASS style name");
    if (std::find(result.names.begin(), result.names.end(), name) == result.names.end())
      result.names.push_back(name);
  }
  return result;
}

AssStyle AssStyle::load(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open ASS style file: " + path);
  std::string text(16 * 1024 * 1024 + 1, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  const auto size = input.gcount();
  if (input.bad() || size > 16 * 1024 * 1024)
    throw std::runtime_error("Cannot read ASS style file or file exceeds 16 MiB: " + path);
  text.resize(static_cast<std::size_t>(size));
  return parse(text);
}
} // namespace Kotonoha
