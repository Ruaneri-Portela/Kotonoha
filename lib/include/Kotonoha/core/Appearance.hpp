#pragma once
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>
namespace Kotonoha {
inline std::vector<std::string> VNFontOptions() {
  std::vector<std::string> result{""};
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(
           FileResolver::fontDirectory(), error)) {
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (entry.is_regular_file(error) &&
        (extension == ".ttf" || extension == ".otf" || extension == ".ttc"))
      result.push_back(entry.path().filename().string());
  }
  std::sort(result.begin() + 1, result.end());
  return result;
}
inline const std::array<std::pair<const char *, SDL_Color>, 12> VNColorOptions{
    {{"White", {255, 255, 255, 255}},
     {"Black", {0, 0, 0, 255}},
     {"Gold", {255, 215, 100, 255}},
     {"Cream", {255, 244, 210, 255}},
     {"Gray", {192, 192, 192, 255}},
     {"Red", {255, 100, 100, 255}},
     {"Pink", {255, 170, 210, 255}},
     {"Orange", {255, 180, 100, 255}},
     {"Yellow", {255, 255, 140, 255}},
     {"Green", {150, 255, 170, 255}},
     {"Blue", {140, 200, 255, 255}},
     {"Purple", {210, 170, 255, 255}}}};
} // namespace Kotonoha
