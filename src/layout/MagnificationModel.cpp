#include "layout/MagnificationModel.h"

#include <algorithm>
#include <cmath>

namespace planetary::layout {

float MagnificationModel::calculateScale(float pointerX,
                                          float iconCenterX,
                                          float radius,
                                          float maxScale) const {
    const float safeRadius = std::max(radius, 0.0001F);
    const float safeMaxScale = std::max(maxScale, 1.0F);
    const float distance = std::fabs(pointerX - iconCenterX);
    if (distance >= safeRadius) {
        return 1.0F;
    }

    // smoothstep 比线性插值更柔和，避免鼠标经过图标边缘时出现跳变。
    const float normalized = 1.0F - distance / safeRadius;
    const float influence = normalized * normalized * (3.0F - 2.0F * normalized);
    return 1.0F + (safeMaxScale - 1.0F) * influence;
}

} // namespace planetary::layout
