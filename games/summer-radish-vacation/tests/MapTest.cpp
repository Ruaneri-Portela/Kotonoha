#include <SummerRadish/Map.hpp>
#include <stdexcept>
namespace SummerRadish {
void testMap() {
  auto require = [](bool ok) {
    if (!ok)
      throw std::runtime_error("Native map test failed");
  };
  ClickMap map({2, 0, 0, 0, 2, 0, 0, 0, 0, 1, 2, 3});
  require(map.value(-1, 0) == -1 && map.value(2, 0) == -1 &&
          map.value(0, 2) == -1);
  require(map.value(0, 0) == 0 && map.value(1, 0) == 1 &&
          map.value(0, 1) == 2 && map.value(1, 1) == 3);
  require(map.locationIndex(0, 0, 3) == -1 && map.locationIndex(1, 0, 3) == 0 &&
          map.locationIndex(0, 1, 3) == 1);
  require(map.locationIndex(1, 1, 2) == -1);
  require(mapLayout(0).locations.size() == 5 &&
          mapLayout(1).locations.size() == 10 &&
          mapLayout(2).locations.size() == 5);
  require(mapLayout(2).locations[0].result == 11 &&
          mapLayout(2).locations[1].result == 10);
  require(mapLayout(1).locations[9].result == 9);
  require(mapNavigate(0, -1, 0) == 0 && mapNavigate(1, -1, 2) == 2);
  require(mapFaceIndex(1, 0, 3) == -1 && byteViewDuration(500) == 2000);
  require(mapFaceIndex(14) == 7 && mapFaceIndex(3) == 1 &&
          mapFaceIndex(0) == -2);
  require(mapCharacterVariable(12) == "CHAR_倉庫" &&
          !byteViewPicture(10).empty() && byteViewPicture(9).empty());
}
} // namespace SummerRadish
