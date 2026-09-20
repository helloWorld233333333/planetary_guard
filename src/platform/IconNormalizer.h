#pragma once

#include <cstdint>
#include <vector>

namespace planetary::platform {

/**
 * 根据 Alpha 边界缩放并居中预乘 BGRA 图标，统一不同软件图标的视觉重量。
 */
bool normalizeIconCanvas(std::vector<std::uint8_t>& pixels,
                         unsigned int canvasSize,
                         unsigned int targetExtent);

/** 计算预乘 BGRA 图标中可见像素的加权平均 RGB（0xRRGGBB）。 */
std::uint32_t averageOpaqueColor(const std::vector<std::uint8_t>& pixels,
                                 unsigned int canvasSize);

} // namespace planetary::platform
