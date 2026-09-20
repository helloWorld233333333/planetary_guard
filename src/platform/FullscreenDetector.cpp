#include "platform/FullscreenDetector.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace planetary::platform {

HWND FullscreenDetector::callbackTarget_ = nullptr;

FullscreenDetector::~FullscreenDetector() {
    shutdown();
}

bool FullscreenDetector::initialize(HWND owner) {
    shutdown();
    owner_ = owner;
    callbackTarget_ = owner_;
    constexpr DWORD kHookFlags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                       EVENT_SYSTEM_FOREGROUND,
                                       nullptr,
                                       &FullscreenDetector::eventCallback,
                                       0U,
                                       0U,
                                       kHookFlags);
    // MVP 只监听前台变化，避免对象位置事件在桌面动画期间产生事件洪峰。
    return foregroundHook_ != nullptr;
}

void FullscreenDetector::shutdown() {
    if (foregroundHook_ != nullptr) UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    if (callbackTarget_ == owner_) {
        callbackTarget_ = nullptr;
    }
    owner_ = nullptr;
}

bool FullscreenDetector::isFullscreen() const {
    if (owner_ == nullptr) return false;
    return isFullscreenWindow(GetForegroundWindow());
}

void CALLBACK FullscreenDetector::eventCallback(HWINEVENTHOOK /*hook*/,
                                                DWORD /*event*/,
                                                HWND hwnd,
                                                LONG /*objectId*/,
                                                LONG /*childId*/,
                                                DWORD /*eventThreadId*/,
                                                DWORD /*eventTime*/) {
    if (callbackTarget_ != nullptr) {
        PostMessageW(callbackTarget_, kChangedMessage, reinterpret_cast<WPARAM>(hwnd), 0);
    }
}

bool FullscreenDetector::isFullscreenWindow(HWND window) const {
    if (window == nullptr || !IsWindowVisible(window) || IsIconic(window)) {
        return false;
    }
    if (window == owner_ || GetAncestor(window, GA_ROOT) == owner_) {
        return false;
    }

    wchar_t className[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (lstrcmpW(className, L"Progman") == 0 ||
        lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
        lstrcmpW(className, L"WorkerW") == 0) {
        return false;
    }

    RECT windowRect{};
    if (FAILED(DwmGetWindowAttribute(window,
                                     DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &windowRect,
                                     sizeof(windowRect)))) {
        if (!GetWindowRect(window, &windowRect)) return false;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo)) return false;

    const RECT& monitorRect = monitorInfo.rcMonitor;
    constexpr LONG kTolerance = 2L;
    return std::abs(windowRect.left - monitorRect.left) <= kTolerance &&
           std::abs(windowRect.top - monitorRect.top) <= kTolerance &&
           std::abs(windowRect.right - monitorRect.right) <= kTolerance &&
           std::abs(windowRect.bottom - monitorRect.bottom) <= kTolerance;
}

} // namespace planetary::platform
