#include <Kotonoha/SchoolDaysAbcTexture.hpp>

#include <limits>

namespace Kotonoha {

std::size_t PrepareStraightAlphaForLinearFiltering(
    std::uint8_t* rgba, int width, int height, int pitch) {
    if (rgba == nullptr || width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() / 4 ||
        pitch < width * 4) return 0;

    std::size_t changed = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* target = rgba + static_cast<std::size_t>(y) * pitch + x * 4;
            if (target[3] != 0) continue;

            const std::uint8_t* nearest = nullptr;
            int nearestDistance = 3;
            int nearestAlpha = -1;
            for (int dy = -1; dy <= 1; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= height) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= width || (dx == 0 && dy == 0))
                        continue;
                    const auto* source = rgba +
                        static_cast<std::size_t>(ny) * pitch + nx * 4;
                    const int alpha = source[3];
                    if (alpha == 0) continue;
                    const int distance = dx * dx + dy * dy;
                    if (distance < nearestDistance ||
                        (distance == nearestDistance && alpha > nearestAlpha)) {
                        nearest = source;
                        nearestDistance = distance;
                        nearestAlpha = alpha;
                    }
                }
            }
            if (nearest == nullptr) continue;
            target[0] = nearest[0];
            target[1] = nearest[1];
            target[2] = nearest[2];
            ++changed;
        }
    }
    return changed;
}

} // namespace Kotonoha
