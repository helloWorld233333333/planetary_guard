#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdlib>

namespace planetary::platform {

/** 为候选显示器计算居中的触底区，不必先移动或显示真实窗口。 */
inline RECT centeredBottomRevealBounds(const RECT& monitor, LONG width) {
    width = std::clamp(width, 1L, std::max(1L, monitor.right - monitor.left));
    const LONG left = monitor.left + (monitor.right - monitor.left - width) / 2;
    return {left, monitor.bottom - 2, left + width, monitor.bottom};
}

/** 隐藏热区只占物理屏幕底部两像素，横向限制为 Dock 所在范围。 */
inline RECT bottomRevealBounds(const RECT& monitor, const RECT& dock) {
    return {std::max(monitor.left, dock.left), monitor.bottom - 2,
            std::min(monitor.right, dock.right), monitor.bottom};
}

/** 普通最大化窗口在任务栏自动隐藏时可能贴边，不能据此认定为全屏。 */
inline bool isFullscreenCoverage(const RECT& window, const RECT& monitor,
                                 bool maximized, LONG_PTR style) {
    if (maximized && (style & (WS_CAPTION | WS_THICKFRAME)) != 0) return false;
    constexpr LONG tolerance = 2;
    return std::abs(window.left - monitor.left) <= tolerance &&
           std::abs(window.top - monitor.top) <= tolerance &&
           std::abs(window.right - monitor.right) <= tolerance &&
           std::abs(window.bottom - monitor.bottom) <= tolerance;
}

/** 仅调整 Dock 自己的位置，为弹出的系统任务栏让位，不修改系统工作区。 */
inline RECT dockWorkArea(const RECT& monitor, RECT work, LONG autoHideTaskbarHeight) {
    if (autoHideTaskbarHeight > 0 && autoHideTaskbarHeight < (monitor.bottom - monitor.top) / 4)
        work.bottom = std::min(work.bottom, monitor.bottom - autoHideTaskbarHeight);
    return work;
}
}
