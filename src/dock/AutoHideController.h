#pragma once

#include <cstddef>

namespace planetary::dock {

/** 自动隐藏的可测试状态。窗口动画和 Win32 定时器由上层负责。 */
enum class AutoHideState {
    Visible,
    HidePending,
    Hiding,
    Hidden,
    Showing,
    Suspended
};

/**
 * Dock 自动隐藏状态机。
 *
 * 该类不依赖 HWND、计时器或鼠标 API，只表达状态转换，便于在 Windows
 * 之外做回归测试。上层收到 wantsHide()/wantsShow() 后再执行窗口动作。
 */
class AutoHideController {
public:
    explicit AutoHideController(bool enabled = false);

    void setEnabled(bool enabled);
    bool enabled() const;

    void onMouseLeave();
    void onMouseEnter();
    /** 用户启动或切换应用后收起；不修改鼠标离开自动隐藏的偏好。 */
    void onApplicationActivated();
    void onHideTimer();
    void onHideCompleted();
    void onShowCompleted();
    void setFullscreen(bool fullscreen);
    void forceShow();

    void acquireVisibilityLock();
    void releaseVisibilityLock();
    std::size_t visibilityLockCount() const;

    AutoHideState state() const;
    bool wantsHide() const;
    bool wantsShow() const;

private:
    bool canHide() const;

    bool enabled_ = false;
    bool fullscreen_ = false;
    std::size_t visibilityLockCount_ = 0U;
    AutoHideState state_ = AutoHideState::Visible;
};

} // namespace planetary::dock
