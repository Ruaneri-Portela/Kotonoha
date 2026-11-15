#include <Kotonoha/routing/SchoolDaysDisplayNarrativeMarkup.hpp>

namespace Kotonoha {
namespace {

bool IsTrailingWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
         c == '\v' || c == '\f';
}

bool IsLxhCredit(std::string_view text) {
  while (!text.empty() && IsTrailingWhitespace(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && IsTrailingWhitespace(text.back()))
    text.remove_suffix(1);
  if (!text.empty() && text.back() == '^')
    text.remove_suffix(1);
  return text == "Juan Ochoa (JNC)- LxH .2021";
}

bool IsDisplayCaret(const std::string &text, std::size_t at) {
  const std::size_t next = at + 1;
  if (next < text.size() &&
      (text[next] == '\n' ||
       (text[next] == '\r' && next + 1 < text.size() &&
        text[next + 1] == '\n') ||
       (text[next] == '\\' && next + 1 < text.size() &&
        (text[next + 1] == 'n' || text[next + 1] == 'N'))))
    return true;

  for (std::size_t i = next; i < text.size(); ++i)
    if (!IsTrailingWhitespace(text[i])) return false;
  return true;
}

} // namespace

std::string SchoolDaysDisplayNarrativeMarkup::Project(const std::string &text) {
  // The locale gate requires a nonempty value for the suppressed credit.
  // SDL_ttf cannot rasterize a line that contains only this placeholder.
  if (text == "\xE2\x80\x8B") return {};

  std::string projected;
  projected.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '^' && IsDisplayCaret(text, i)) continue;
    projected.push_back(text[i]);
  }
  return projected;
}

bool SchoolDaysDisplayNarrativeMarkup::SuppressLxhCredit(
    std::string_view original, std::string_view localized,
    std::string_view locale) {
  // School Days currently targets C++17 (string_view::starts_with is C++20).
  const bool spanish = locale == "es" ||
      locale.substr(0, 3) == "es-" || locale.substr(0, 3) == "es_";
  return !spanish && (IsLxhCredit(original) || IsLxhCredit(localized));
}

} // namespace Kotonoha
