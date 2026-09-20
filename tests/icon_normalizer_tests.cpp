#include "platform/IconNormalizer.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Bounds {
    unsigned int left = 0U;
    unsigned int top = 0U;
    unsigned int right = 0U;
    unsigned int bottom = 0U;
    bool found = false;
};

Bounds alphaBounds(const std::vector<std::uint8_t>& pixels, unsigned int size) {
    Bounds bounds{size, size, 0U, 0U, false};
    for (unsigned int y = 0U; y < size; ++y) {
        for (unsigned int x = 0U; x < size; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4U;
            if (pixels[index + 3U] == 0U) continue;
            bounds.found = true;
            bounds.left = std::min(bounds.left, x);
            bounds.top = std::min(bounds.top, y);
            bounds.right = std::max(bounds.right, x);
            bounds.bottom = std::max(bounds.bottom, y);
        }
    }
    return bounds;
}

void testSmallOffCenterIconIsCenteredAndExpanded() {
    constexpr unsigned int size = 8U;
    std::vector<std::uint8_t> pixels(size * size * 4U, 0U);
    for (unsigned int y = 0U; y < 4U; ++y) {
        for (unsigned int x = 0U; x < 2U; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4U;
            pixels[index] = 80U;
            pixels[index + 1U] = 120U;
            pixels[index + 2U] = 160U;
            pixels[index + 3U] = 255U;
        }
    }

    expectTrue(planetary::platform::normalizeIconCanvas(pixels, size, 6U),
               "visible icon should normalize");
    const Bounds bounds = alphaBounds(pixels, size);
    expectTrue(bounds.found, "normalized icon should remain visible");
    expectTrue(bounds.right - bounds.left + 1U == 3U,
               "aspect ratio should be preserved");
    expectTrue(bounds.bottom - bounds.top + 1U == 6U,
               "long side should use the target extent");
    expectTrue(std::abs(static_cast<int>(bounds.left + bounds.right) -
                        static_cast<int>(size - 1U)) <= 1,
               "icon should be horizontally centered");
    expectTrue(std::abs(static_cast<int>(bounds.top + bounds.bottom) -
                        static_cast<int>(size - 1U)) <= 1,
               "icon should be vertically centered");
}

void testTransparentCanvasIsRejected() {
    std::vector<std::uint8_t> pixels(8U * 8U * 4U, 0U);
    expectTrue(!planetary::platform::normalizeIconCanvas(pixels, 8U, 6U),
               "transparent canvas should not be normalized");
}

void testAverageOpaqueColorUnpremultipliesChannels() {
    std::vector<std::uint8_t> pixels(4U, 0U);
    // 50% alpha 的 RGB(200, 100, 50) 预乘 BGRA。
    pixels[0] = 25U;
    pixels[1] = 50U;
    pixels[2] = 100U;
    pixels[3] = 128U;
    const std::uint32_t color = planetary::platform::averageOpaqueColor(pixels, 1U);
    expectTrue(((color >> 16U) & 0xFFU) >= 198U,
               "red channel should be unpremultiplied");
    expectTrue(((color >> 8U) & 0xFFU) >= 99U,
               "green channel should be unpremultiplied");
    expectTrue((color & 0xFFU) >= 49U,
               "blue channel should be unpremultiplied");
}

} // namespace

int main() {
    try {
        testSmallOffCenterIconIsCenteredAndExpanded();
        testTransparentCanvasIsRejected();
        testAverageOpaqueColorUnpremultipliesChannels();
        std::cout << "All icon normalizer tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Icon normalizer test failure: " << error.what() << '\n';
        return 1;
    }
}
