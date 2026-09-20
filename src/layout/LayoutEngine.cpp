#include "layout/LayoutEngine.h"

#include "layout/MagnificationModel.h"

#include <algorithm>
#include <cmath>

namespace planetary::layout {

LayoutEngine::LayoutEngine(float baseIconDip, float gapDip, float maxScale)
    : baseIconDip_(std::max(baseIconDip, 1.0F)),
      gapDip_(std::max(gapDip, 0.0F)),
      maxScale_(std::max(maxScale, 1.0F)) {}

LayoutSnapshot LayoutEngine::calculate(const std::vector<domain::DockItem>& items,
                                       float pointerX,
                                       float dpiScale) const {
    LayoutSnapshot snapshot;
    const float safeDpiScale = std::max(dpiScale, 0.1F);
    const float baseSize = baseIconDip_ * safeDpiScale;
    const float gap = gapDip_ * safeDpiScale;
    const float sectionGap = 10.0F * safeDpiScale;
    const float horizontalPadding = 10.0F * safeDpiScale;
    const float verticalPadding = 10.0F * safeDpiScale;
    const float radius = baseSize * 2.5F;
    MagnificationModel magnification;

    std::vector<const domain::DockItem*> visibleItems;
    visibleItems.reserve(items.size());
    for (const domain::DockItem& item : items) {
        if (item.enabled) visibleItems.push_back(&item);
    }
    if (visibleItems.empty()) {
        return snapshot;
    }
    // 波峰最多影响约六个槽位。固定画布避免 DWM 在每帧调整模糊窗口。
    const float reserve = std::min(6.0F, static_cast<float>(visibleItems.size())) * baseSize * (maxScale_ - 1.0F);

    snapshot.items.reserve(visibleItems.size());
    std::vector<float> scales;
    scales.reserve(visibleItems.size());

    // 先使用未放大的等距中心计算影响范围，保证指针移动时布局稳定。
    float unscaledLeft = horizontalPadding + reserve * 0.5F;
    for (std::size_t index = 0; index < visibleItems.size(); ++index) {
        const bool startsFolderSection = index > 0U &&
            visibleItems[index]->type == domain::DockItemType::Folder &&
            visibleItems[index - 1U]->type != domain::DockItemType::Folder;
        if (startsFolderSection) unscaledLeft += sectionGap;
        const float center = unscaledLeft + baseSize * 0.5F;
        const float scale = pointerX < 0.0F
                                ? 1.0F
                                : magnification.calculateScale(pointerX,
                                                               center,
                                                               radius,
                                                               maxScale_);
        scales.push_back(scale);
        unscaledLeft += baseSize + gap;
    }

    float width = 0.0F;
    for (std::size_t index = 0; index < scales.size(); ++index) {
        width += baseSize * scales[index];
        if (index > 0U) {
            width += gap;
            if (visibleItems[index]->type == domain::DockItemType::Folder &&
                visibleItems[index - 1U]->type != domain::DockItemType::Folder) {
                width += sectionGap;
            }
        }
    }

    float baseWidth = static_cast<float>(visibleItems.size()) * baseSize +
                      static_cast<float>(visibleItems.size() - 1U) * gap + 2.0F * horizontalPadding;
    for (std::size_t index = 1; index < visibleItems.size(); ++index) {
        if (visibleItems[index]->type == domain::DockItemType::Folder &&
            visibleItems[index - 1U]->type != domain::DockItemType::Folder) baseWidth += sectionGap;
    }
    snapshot.width = baseWidth + reserve;
    snapshot.height = baseSize * maxScale_ + 2.0F * verticalPadding;
    snapshot.panelHeight = baseSize + 2.0F * verticalPadding;
    snapshot.panelLeft = reserve * 0.5F;
    snapshot.panelWidth = baseWidth;

    float left = (snapshot.width - width) * 0.5F;
    for (std::size_t index = 0; index < visibleItems.size(); ++index) {
        if (index > 0U && visibleItems[index]->type == domain::DockItemType::Folder &&
            visibleItems[index - 1U]->type != domain::DockItemType::Folder) {
            left += sectionGap;
        }
        const float size = baseSize * scales[index];
        const float top = snapshot.height - verticalPadding - size;
        LayoutItem layoutItem;
        layoutItem.id = visibleItems[index]->id;
        layoutItem.left = left;
        layoutItem.top = top;
        layoutItem.right = left + size;
        layoutItem.bottom = top + size;
        layoutItem.centerX = left + size * 0.5F;
        layoutItem.scale = scales[index];
        snapshot.items.push_back(layoutItem);
        left = layoutItem.right + (index + 1U < visibleItems.size() ? gap : 0.0F);
    }

    return snapshot;
}

} // namespace planetary::layout
