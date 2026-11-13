#include <SnowRadish/SnowAssets.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: PmailGraphicsCheck PMAILDVD");
    Kotonoha::Providers::SnowAssets assets(argv[1]);
    size_t characters = 0, animations = 0, frames = 0;
    for (const auto &id : assets.list("graphic/")) {
      if (id.size() >= 5 && id.substr(id.size() - 5) == ".char") {
        const auto surface = assets.loadSurfaceAsset(id);
        const auto stem = id.substr(8, id.size() - 8 - 5);
        const auto placement = assets.Sprite(stem);
        if (!surface || !placement.valid || surface->w != int(placement.w) ||
            surface->h != int(placement.h))
          throw std::runtime_error("Invalid CHAR: " + id);
        ++characters;
      } else if (id.size() >= 5 && id.substr(id.size() - 5) == ".anim") {
        const auto stem = id.substr(8, id.size() - 8 - 5);
        const auto &animation = assets.AnimationOf(stem);
        if (animation.frames.empty())
          throw std::runtime_error("Invalid ANIM: " + id);
        for (size_t i = 0; i < animation.frames.size(); ++i) {
          auto surface = assets.loadSurfaceAsset(id + "#" + std::to_string(i));
          if (!surface || surface->w != int(animation.frames[i].w) ||
              surface->h != int(animation.frames[i].h))
            throw std::runtime_error("Invalid ANIM frame: " + id);
          ++frames;
        }
        ++animations;
      }
    }
    SnowRadish::Animation malformed;
    auto truncated = assets.Graphic("osboot.anim");
    truncated.resize(0x2083);
    if (SnowRadish::decodeAnm(truncated, malformed) ||
        !malformed.frames.empty())
      throw std::runtime_error("Truncated ANIM header was accepted");
    auto invalidOffset = assets.Graphic("osboot.anim");
    for (size_t i = 4; i < 8; ++i)
      invalidOffset[i] = 255;
    if (SnowRadish::decodeAnm(invalidOffset, malformed) ||
        !malformed.frames.empty())
      throw std::runtime_error("Out-of-range ANIM offset was accepted");
    const auto waku = assets.Sprite("waku");
    if (waku.x != 37 || waku.y != 450 || waku.w != 732 || waku.h != 141)
      throw std::runtime_error("Waku original canvas placement changed");
    auto title = assets.loadSurfaceAsset("graphic/TITL01.IMG");
    const auto bytes = assets.Graphic("TITL01.IMG");
    if (!title || title->w != 423 || title->h != 55)
      throw std::runtime_error("Wrong title geometry");
    for (int y = 0; y < 55; ++y)
      for (int x = 0; x < 423; ++x) {
        const auto *raw = &bytes[(size_t(54 - y) * 423 + x) * 3];
        const auto *pixel = static_cast<const uint8_t *>(title->pixels) +
                            y * title->pitch + x * 4;
        const bool white = raw[0] == 255 && raw[1] == 255 && raw[2] == 255;
        if (pixel[3] != (white ? 0 : 255) ||
            (!white &&
             (pixel[0] != raw[2] || pixel[1] != raw[1] || pixel[2] != raw[0])))
          throw std::runtime_error("Title orientation/color key mismatch");
      }
    std::cout << "Verified " << characters << " CHAR assets, " << animations
              << " ANIM assets, " << frames
              << " frames; original waku placement and bottom-up BGR title "
                 "verified.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
