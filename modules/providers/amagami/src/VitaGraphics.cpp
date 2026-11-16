// Adapted from palette layout findings by hokagocafe/open-amagami (MIT).
// Licence: third_party/open-amagami-LICENSE.txt.
#include <Amagami/VitaGraphics.hpp>
#include <array>
#include <stdexcept>

namespace Amagami::Vita {
namespace {
bool startsWith(const std::string &s, const char *prefix) {
  return s.rfind(prefix, 0) == 0;
}
bool endsWith(const std::string &s, const char *suffix) {
  const std::string ext(suffix);
  return s.size() > ext.size() &&
         s.compare(s.size() - ext.size(), ext.size(), ext) == 0;
}
} // namespace

std::optional<PictureLayout> pictureLayout(const std::string &name) {
  if ((startsWith(name, "bg/") || startsWith(name, "epi/")) &&
      endsWith(name, ".gxt"))
    return PictureLayout{960, 720, true};
  if (startsWith(name, "body/") && endsWith(name, ".gxt"))
    return PictureLayout{1024, 1536, true};
  if ((startsWith(name, "face/") || startsWith(name, "eface/")) &&
      endsWith(name, ".tm2"))
    return PictureLayout{1024, 1024, false};
  return std::nullopt;
}

Picture decodeIndexed8(std::string_view data, PictureLayout layout) {
  const auto count = uint64_t(layout.width) * layout.height;
  if (!layout.width || !layout.height || count > 4u * 1024 * 1024 ||
      data.size() != 1024ull + count)
    throw std::runtime_error("Amagami Vita: invalid indexed picture length");
  Picture result;
  result.width = layout.width;
  result.height = layout.height;
  result.rgba.resize(size_t(count) * 4);
  std::array<std::array<uint8_t, 4>, 256> palette{};
  for (unsigned i = 0; i < 256; ++i) {
    const auto pos = i * 4;
    palette[i] = {
        uint8_t(data[pos + (layout.bgra ? 2 : 0)]), uint8_t(data[pos + 1]),
        uint8_t(data[pos + (layout.bgra ? 0 : 2)]), uint8_t(data[pos + 3])};
  }
  for (size_t i = 0; i < count; ++i) {
    const auto &pixel = palette[uint8_t(data[1024 + i])];
    for (unsigned component = 0; component < 4; ++component)
      result.rgba[i * 4 + component] = pixel[component];
  }
  return result;
}

Picture decodeVitaPicture(const std::string &resourceName,
                          std::string_view data) {
  const auto layout = pictureLayout(resourceName);
  if (!layout)
    throw std::runtime_error("Amagami Vita: unsupported palette resource: " +
                             resourceName);
  return decodeIndexed8(data, *layout);
}
} // namespace Amagami::Vita
