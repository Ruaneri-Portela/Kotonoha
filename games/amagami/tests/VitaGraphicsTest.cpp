#include <Amagami/VitaGraphics.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
  bool error = false;
  try {
    fn();
  } catch (const std::runtime_error &) {
    error = true;
  }
  check(error, "Malformed Vita picture should be rejected");
}
} // namespace

int main() {
  try {
    using namespace Amagami::Vita;
    const auto background = pictureLayout("bg/000_00.gxt");
    const auto body = pictureLayout("body/b1_Aax.gxt");
    const auto face = pictureLayout("face/f1_Ax.tm2");
    const auto event = pictureLayout("epi/e1_01a0.gxt");
    const auto expression = pictureLayout("eface/f1_Ax.tm2");
    check(background && background->width == 960 && background->height == 720 &&
              background->bgra,
          "Vita background layout");
    check(event && event->width == 960 && event->bgra,
          "Vita event image layout");
    check(body && body->width == 1024 && body->height == 1536 && body->bgra,
          "Vita body layout");
    check(face && face->width == 1024 && face->height == 1024 && !face->bgra &&
              expression && !expression->bgra,
          "Vita face layouts");
    check(!pictureLayout("font/AMV_font18_21.tm2") &&
              !pictureLayout("system/sysgraph.tm2") &&
              !pictureLayout("../body/a.gxt") &&
              !pictureLayout("body/not_image.tm2"),
          "Unsupported resources must not use an incorrect 8-bit layout");

    std::string indexed(1024 + 4, '\0');
    indexed[4] = char(10);
    indexed[5] = char(20);
    indexed[6] = char(30);
    indexed[7] = char(128);
    indexed[8] = char(240);
    indexed[9] = char(64);
    indexed[10] = char(25);
    indexed[11] = char(255);
    indexed[1024] = char(1);
    indexed[1025] = char(2);
    indexed[1026] = char(2);
    indexed[1027] = char(1);
    auto bgra = decodeIndexed8(indexed, {2, 2, true});
    auto rgba = decodeIndexed8(indexed, {2, 2, false});
    check(bgra.width == 2 && bgra.height == 2 && bgra.rgba.size() == 16,
          "RGBA output dimensions");
    check(bgra.rgba[0] == 30 && bgra.rgba[1] == 20 && bgra.rgba[2] == 10 &&
              bgra.rgba[3] == 128 && rgba.rgba[0] == 10 && rgba.rgba[1] == 20 &&
              rgba.rgba[2] == 30 && rgba.rgba[3] == 128,
          "BGRA versus RGBA palette channel order and unchanged alpha");
    check(bgra.rgba[4] == 25 && bgra.rgba[5] == 64 && bgra.rgba[6] == 240 &&
              bgra.rgba[7] == 255 && bgra.rgba[8] == 25 && bgra.rgba[12] == 30,
          "Palette index expansion keeps pixel order");
    rejects([&] { decodeIndexed8(indexed.substr(0, 1027), {2, 2, true}); });
    rejects([&] { decodeIndexed8(indexed + "x", {2, 2, true}); });
    rejects([&] { decodeIndexed8(indexed, {0, 2, false}); });
    rejects([&] { decodeIndexed8(indexed, {65535, 65535, false}); });
    rejects([&] { decodeVitaPicture("face/x.zb", indexed); });
    rejects([&] { decodeVitaPicture("bg/x.gxt", indexed); });
    std::cout << "Amagami Vita indexed image tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
