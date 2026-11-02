#pragma once
#include <SummerRadish/Pack.hpp>
#include <array>
#include <string>
#include <vector>
namespace SummerRadish {
// Native rectangles store exclusive right/bottom coordinates.
struct MapRect {
  int left, top, right, bottom;
};
struct MapPoint {
  int x, y;
};
struct MapLocation {
  MapRect position, source;
  std::array<int, 4> navigation; // up, down, left, right
  int result;
  MapPoint face;
};
struct MapLayout {
  std::string background, hitmap, texture;
  std::vector<MapLocation> locations;
};
const MapLayout &mapLayout(int mode);
int mapNavigate(int mode, int focused, size_t direction);
std::string mapCharacterVariable(int location);
int mapFaceIndex(int character);
int mapFaceIndex(int mode, size_t item, int character);
int byteViewDuration(int wipeSpeed);
// BYTEVIEW is displayed only for cafe locations 10..14.
std::string byteViewPicture(int location);
class ClickMap {
  int width = 0, height = 0;
  Bytes pixels;

public:
  explicit ClickMap(const Bytes &);
  // Original ClickableMap::GetPoint returns zero for empty area, -1 outside.
  int value(int x, int y) const;
  // MapSelect subtracts one from the hitmap value and checks the item count.
  int locationIndex(int x, int y, size_t count) const;
};
} // namespace SummerRadish
