#include "platform/IconNormalizer.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <utility>

namespace planetary::platform {

std::uint32_t averageOpaqueColor(const std::vector<std::uint8_t>& pixels,
                                 unsigned int canvasSize) {
    if (canvasSize == 0U ||
        pixels.size() != static_cast<std::size_t>(canvasSize) * canvasSize * 4U) {
        return 0U;
    }
    std::uint64_t red = 0U;
    std::uint64_t green = 0U;
    std::uint64_t blue = 0U;
    std::uint64_t weight = 0U;
    for (std::size_t index = 0U; index < pixels.size(); index += 4U) {
        const std::uint32_t alpha = pixels[index + 3U];
        if (alpha <= 24U) continue;
        blue += static_cast<std::uint64_t>(pixels[index]) * 255U;
        green += static_cast<std::uint64_t>(pixels[index + 1U]) * 255U;
        red += static_cast<std::uint64_t>(pixels[index + 2U]) * 255U;
        weight += alpha;
    }
    if (weight == 0U) return 0U;
    const auto channel = [weight](std::uint64_t sum) {
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(255U, sum / weight));
    };
    return (channel(red) << 16U) | (channel(green) << 8U) | channel(blue);
}

bool normalizeIconCanvas(std::vector<std::uint8_t>& pixels,
                         unsigned int canvasSize,
                         unsigned int targetExtent) {
    if (canvasSize == 0U || targetExtent == 0U || targetExtent > canvasSize ||
        pixels.size() != static_cast<std::size_t>(canvasSize) * canvasSize * 4U) {
        return false;
    }

    unsigned int minX = canvasSize;
    unsigned int minY = canvasSize;
    unsigned int maxX = 0U;
    unsigned int maxY = 0U;
    bool found = false;
    for (unsigned int y = 0U; y < canvasSize; ++y) {
        for (unsigned int x = 0U; x < canvasSize; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * canvasSize + x) * 4U;
            if (pixels[index + 3U] <= 8U) continue;
            found = true;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (!found) return false;

    const unsigned int sourceWidth = maxX - minX + 1U;
    const unsigned int sourceHeight = maxY - minY + 1U;
    const float scale = std::min(static_cast<float>(targetExtent) / sourceWidth,
                                 static_cast<float>(targetExtent) / sourceHeight);
    const unsigned int destinationWidth = std::max(
        1U, static_cast<unsigned int>(std::lround(sourceWidth * scale)));
    const unsigned int destinationHeight = std::max(
        1U, static_cast<unsigned int>(std::lround(sourceHeight * scale)));
    const unsigned int destinationLeft = (canvasSize - destinationWidth) / 2U;
    const unsigned int destinationTop = (canvasSize - destinationHeight) / 2U;
    std::vector<std::uint8_t> normalized(pixels.size(), 0U);

    for (unsigned int y = 0U; y < destinationHeight; ++y) {
        const float sourceY = static_cast<float>(minY) +
                              (static_cast<float>(y) + 0.5F) / scale - 0.5F;
        const float clampedY = std::clamp(sourceY, static_cast<float>(minY), static_cast<float>(maxY));
        const unsigned int y0 = static_cast<unsigned int>(clampedY);
        const unsigned int y1 = std::min(y0 + 1U, maxY);
        const float fy = clampedY - static_cast<float>(y0);
        for (unsigned int x = 0U; x < destinationWidth; ++x) {
            const float sourceX = static_cast<float>(minX) +
                                  (static_cast<float>(x) + 0.5F) / scale - 0.5F;
            const float clampedX = std::clamp(sourceX, static_cast<float>(minX), static_cast<float>(maxX));
            const unsigned int x0 = static_cast<unsigned int>(clampedX);
            const unsigned int x1 = std::min(x0 + 1U, maxX);
            const float fx = clampedX - static_cast<float>(x0);
            const std::size_t destinationIndex =
                (static_cast<std::size_t>(destinationTop + y) * canvasSize +
                 destinationLeft + x) * 4U;
            // 预乘颜色与 Alpha 一起双线性采样，避免斜线锯齿和透明边缘黑边。
            for (unsigned int channel = 0; channel < 4U; ++channel) {
                const auto sample = [&](unsigned int sx, unsigned int sy) {
                    return static_cast<float>(pixels[(static_cast<std::size_t>(sy) * canvasSize + sx) * 4U + channel]);
                };
                const float top = sample(x0, y0) * (1.0F - fx) + sample(x1, y0) * fx;
                const float bottom = sample(x0, y1) * (1.0F - fx) + sample(x1, y1) * fx;
                normalized[destinationIndex + channel] = static_cast<std::uint8_t>(std::lround(top * (1.0F - fy) + bottom * fy));
            }
        }
    }
    pixels = std::move(normalized);
    return true;
}

} // namespace planetary::platform
