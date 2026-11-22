// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ImageEffects.hpp>
#include <N2System/SceneGraph.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
int main() {
  std::vector<uint8_t> rgba = {
      255,0,0,255, 0,255,0,0,
      0,0,255,255, 255,255,255,255
  };
  N2System::grayscaleRgba(rgba);
  check(rgba[0] == rgba[1] && rgba[1] == rgba[2] && rgba[3] == 255,
        "Grayscale preserves alpha and maps RGB equally");
  check(rgba[7] == 0 && rgba[4] == rgba[5] && rgba[5] == rgba[6],
        "Grayscale transparent pixels keep alpha zero");

  std::vector<uint8_t> hole(3*3*4, 0);
  hole[(4*4)+0] = 240;
  hole[(4*4)+1] = 60;
  hole[(4*4)+2] = 30;
  hole[(4*4)+3] = 255;
  check(N2System::boxBlurRgba(hole,3,3,1),"Box blur accepts RGBA");
  check(hole[3] > 0 && hole[3] < 255,
        "Blur spreads premultiplied alpha");
  check(hole[0] == 240 && hole[1] == 60 && hole[2] == 30,
        "Blur avoids black transparent-pixel fringes");
  check(!N2System::boxBlurRgba(hole,3,3,17),
        "Reject passes outside reference presets");
  check(!N2System::boxBlurRgba(hole,100,100,1),
        "Reject buffer size mismatch");
  N2System::SceneGraph graph;
  graph.createEntity("bg");
  graph.createEntity("overlay");
  graph.setMonochrome("*", true);
  graph.setBoxBlur("bg", 16);
  check(graph.findEntity("bg")->monochrome &&
        graph.findEntity("bg")->boxBlurPasses == 16,
        "Monochrome and heavy shade attached to matching entity");
  check(graph.findEntity("overlay")->monochrome &&
        graph.findEntity("overlay")->boxBlurPasses == 0,
        "Box blur remains scoped to the targeted asset");
  graph.setBoxBlur("bg", 8);
  check(graph.findEntity("bg")->boxBlurPasses == 8,
        "Medium shade maps to eight passes");
  std::cout << "N2System image effects passed\n";
}
