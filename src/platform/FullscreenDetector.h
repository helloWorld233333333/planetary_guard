#pragma once

#include <windows.h>

namespace planetary::platform {

/**
 * 使用 WinEventHook 判断前台窗口是否覆盖所在显示器。
 * 回调只向 UI 线程投递消息，实际窗口判断仍在 Dock 线程完成。
 */
class FullscreenDetector {
public:
    static constexpr UINT kChangedMessage = WM_APP + 42U;

    FullscreenDetector() = default;
    ~FullscreenDetector();

    FullscreenDetector(const FullscreenDetector&) = delete;
    FullscreenDetector& operator=(const FullscreenDetector&) = delete;

    bool initialize(HWND owner);
    void shutdown();
    bool isFullscreen() const;

private:
    static void CALLBACK eventCallback(HWINEVENTHOOK hook,
                                       DWORD event,
                                       HWND hwnd,
                                       LONG objectId,
                                       LONG childId,
                                       DWORD eventThreadId,
                                       DWORD eventTime);
    bool isFullscreenWindow(HWND window) const;

    static HWND callbackTarget_;
    HWND owner_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
};

} // namespace planetary::platform
