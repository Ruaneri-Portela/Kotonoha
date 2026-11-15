#pragma once

#include <string>
#include <string_view>

namespace Kotonoha {

// Project only the School Days narrative text shown to players. Keep the
// source ORS/JSON and its translation keys untouched.
struct SchoolDaysDisplayNarrativeMarkup {
  static std::string Project(const std::string &text);
  static bool SuppressLxhCredit(std::string_view original,
                                std::string_view localized,
                                std::string_view locale);
};

} // namespace Kotonoha
