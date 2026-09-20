#pragma once

namespace planetary::layout {

/**
 * 计算 Dock 图标的鼠标悬停放大比例。
 *
 * 该类不依赖窗口、输入或渲染 API，便于单元测试，也方便未来复用到
 * 键盘导航和触控指针场景。
 */
class MagnificationModel {
public:
    /**
     * @param pointerX 指针在 Dock 坐标系中的横坐标。
     * @param iconCenterX 当前图标中心横坐标。
     * @param radius 影响半径，必须为正数。
     * @param maxScale 最大放大比例，低于 1 时按 1 处理。
     */
    float calculateScale(float pointerX,
                         float iconCenterX,
                         float radius,
                         float maxScale) const;
};

} // namespace planetary::layout
