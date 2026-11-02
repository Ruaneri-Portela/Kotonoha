#include <SummerRadish/Map.hpp>
#include <stdexcept>
namespace SummerRadish {
const MapLayout &mapLayout(int mode) {
  // SummerRadish.sdll 0x100154b0; consumed by native 0x10001f30 / 0x10001510.
  static const std::array<MapLayout, 3> layouts{{
      {"GRAPHIC/MAP/MAP_HIRU.CGD",
       "GRAPHIC/MAP/MAP_MAP.CMAP",
       "GRAPHIC/MAP/MAP_CHIP.TEXB",
       {
           {{75, 103, 246, 218}, {0, 0, 171, 115}, {2, 3, 0, 3}, 0, {94, 83}},
           {{190, 129, 371, 274},
            {172, 0, 353, 145},
            {3, 4, 3, 4},
            1,
            {197, 212}},
           {{200, 36, 403, 160},
            {354, 0, 557, 124},
            {2, 3, 0, 3},
            2,
            {279, 15}},
           {{146, 82, 451, 243},
            {558, 0, 863, 161},
            {2, 1, 0, 4},
            3,
            {333, 72}},
           {{257, 59, 574, 304},
            {0, 162, 317, 407},
            {3, 4, 1, 4},
            4,
            {392, 182}},
       }},
      {"GRAPHIC/MAP/MAP_YORU.CGD",
       "GRAPHIC/MAP/MAP_YORU.CMAP",
       "GRAPHIC/MAP/MAP_CHIP.TEXB",
       {
           {{75, 103, 246, 218}, {0, 0, 171, 115}, {2, 2, 2, 2}, 0, {94, 83}},
           {{190, 129, 371, 274},
            {172, 0, 353, 145},
            {3, 4, 3, 4},
            1,
            {197, 212}},
           {{200, 36, 403, 160},
            {354, 0, 557, 124},
            {2, 3, 2, 3},
            2,
            {279, 15}},
           {{146, 82, 451, 243},
            {558, 0, 863, 161},
            {2, 1, 3, 4},
            3,
            {333, 72}},
           {{257, 59, 574, 304},
            {0, 162, 317, 407},
            {3, 8, 1, 4},
            4,
            {392, 182}},
           {{343, 398, 548, 501},
            {318, 162, 523, 265},
            {8, 7, 8, 9},
            5,
            {423, 369}},
           {{385, 280, 610, 415},
            {524, 162, 749, 297},
            {2, 2, 2, 2},
            6,
            {445, 262}},
           {{391, 413, 640, 560},
            {750, 162, 999, 309},
            {5, 7, 5, 9},
            7,
            {485, 410}},
           {{225, 330, 474, 477},
            {0, 408, 249, 555},
            {4, 5, 8, 5},
            8,
            {297, 330}},
           {{515, 345, 740, 480},
            {250, 408, 475, 543},
            {4, 7, 5, 9},
            9,
            {579, 330}},
       }},
      {"GRAPHIC/MAP/TENNAI.CGD",
       "GRAPHIC/MAP/TENNAI_MAP.CMAP",
       "GRAPHIC/MAP/TENNAI.TEXB",
       {
           {{221, 345, 370, 450},
            {0, 0, 149, 105},
            {2, 4, 0, 1},
            11,
            {244, 320}},
           {{365, 165, 694, 428},
            {150, 0, 479, 263},
            {3, 4, 0, 1},
            10,
            {494, 225}},
           {{219, 171, 296, 270},
            {0, 106, 77, 205},
            {3, 0, 2, 1},
            12,
            {213, 145}},
           {{251, 75, 682, 170},
            {0, 264, 431, 359},
            {3, 1, 2, 3},
            13,
            {424, 41}},
           {{223, 458, 706, 561},
            {0, 360, 483, 463},
            {1, 4, 4, 4},
            14,
            {422, 435}},
       }},
  }};
  if (mode < 0 || mode >= int(layouts.size()))
    throw std::runtime_error("Invalid MAPSELECT mode");
  return layouts[size_t(mode)];
}
int mapNavigate(int mode, int focused, size_t direction) {
  const auto &layout = mapLayout(mode);
  if (direction >= 4 || focused < -1 || focused >= int(layout.locations.size()))
    throw std::runtime_error("Invalid map focus/navigation");
  // Original navigation table has a sentinel row indexed by focus+1.
  if (focused == -1)
    return mode == 1 ? 2 : 0;
  int next = layout.locations[size_t(focused)].navigation[direction];
  return next >= 0 && next < int(layout.locations.size()) ? next : focused;
}
std::string mapCharacterVariable(int location) {
  static const std::array<const char *, 15> names{
      "CHAR_寮",     "CHAR_夏蕪", "CHAR_駅",   "CHAR_道路",   "CHAR_海",
      "CHAR_ロビー", "CHAR_瞬",   "CHAR_神楽", "CHAR_踊子",   "CHAR_舞",
      "CHAR_フロア", "CHAR_レジ", "CHAR_倉庫", "CHAR_カフェ", "CHAR_店前",
  };
  if (location < 0 || location >= int(names.size()))
    throw std::runtime_error("Invalid map location");
  return names[size_t(location)];
}
int mapFaceIndex(int character) {
  // Native 0x10001f30: actor14 has special summer heroine badge7.
  return character == 14 ? 7 : character - 2;
}
int mapFaceIndex(int mode, size_t item, int character) {
  const auto &layout = mapLayout(mode);
  if (item >= layout.locations.size())
    throw std::runtime_error("Invalid map face item");
  return mode == 1 && item == 0 ? -1 : mapFaceIndex(character);
}
int byteViewDuration(int wipeSpeed) {
  if (wipeSpeed < 0 || wipeSpeed > 1000)
    throw std::runtime_error("Invalid wipe speed");
  return ((1000 - wipeSpeed) * 2000) / 500;
}
std::string byteViewPicture(int location) {
  static const std::array<const char *, 5> names{
      "GRAPHIC/BYTE/FLOOR.CGD",   "GRAPHIC/BYTE/REGISTER.CGD",
      "GRAPHIC/BYTE/SOUKO.CGD",   "GRAPHIC/BYTE/OPENCAFE.CGD",
      "GRAPHIC/BYTE/MISEMAE.CGD",
  };
  return location >= 10 && location <= 14 ? names[size_t(location - 10)] : "";
}
ClickMap::ClickMap(const Bytes &data) {
  // ClickableMap.mod 0x100010c0 copies one byte per top-down map pixel.
  width = int(word(data, 0));
  height = int(word(data, 4));
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
      size_t(width) * height > data.size() - 8)
    throw std::runtime_error("Invalid CMAP dimensions or truncated pixels");
  pixels.assign(data.begin() + 8, data.begin() + 8 + size_t(width) * height);
}
int ClickMap::value(int x, int y) const {
  return x >= 0 && y >= 0 && x < width && y < height
             ? pixels[size_t(y) * width + x]
             : -1;
}
int ClickMap::locationIndex(int x, int y, size_t count) const {
  int index = value(x, y) - 1;
  return index >= 0 && size_t(index) < count ? index : -1;
}
} // namespace SummerRadish
