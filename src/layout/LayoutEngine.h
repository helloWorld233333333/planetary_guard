#pragma once

#include "domain/DockItem.h"

#include <string>
#include <vector>

namespace planetary::layout {

/** 单个 Dock 条目在窗口中的布局结果。 */
struct LayoutItem {
    /** 与领域模型对应的稳定 ID。 */
    std::string id;
    /** 包围盒左边界。 */
    float left = 0.0F;
    /** 包围盒上边界。 */
    float top = 0.0F;
    /** 包围盒右边界。 */
    float right = 0.0F;
    /** 包围盒下边界。 */
    float bottom = 0.0F;
    /** 图标中心横坐标。 */
    float centerX = 0.0F;
    /** 当前的显示比例。 */
    float scale = 1.0F;
};

/** 一次布局计算的不可变快照。 */
struct LayoutSnapshot {
    /** 底板固定高度，放大只向上延伸。 */
    float panelHeight = 0.0F;
    /** 固定玻璃底板位置，透明画布预留悬停放大空间。 */
    float panelLeft = 0.0F;
    float panelWidth = 0.0F;
    /** Dock 客户区宽度。 */
    float width = 0.0F;
    /** Dock 客户区高度。 */
    float height = 0.0F;
    /** 从左到右排列的条目。 */
    std::vector<LayoutItem> items;
};

/**
 * 负责根据 DPI、指针位置和条目列表计算 Dock 布局。
 */
class LayoutEngine {
public:
    LayoutEngine(float baseIconDip, float gapDip, float maxScale);

    /**
     * @param items 已按 order 排序的可见条目。
     * @param pointerX Dock 客户区内的指针横坐标；负值表示没有悬停。
     * @param dpiScale 当前显示器的 DPI 缩放倍数。
     */
    LayoutSnapshot calculate(const std::vector<domain::DockItem>& items,
                             float pointerX,
                             float dpiScale) const;

private:
    float baseIconDip_;
    float gapDip_;
    float maxScale_;
};

} // namespace planetary::layout
