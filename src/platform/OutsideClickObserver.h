#pragma once

#include <windows.h>

namespace planetary::platform {

/** Dock 可见期间检测外部按下；不吞事件、不记录轨迹，隐藏后卸载。 */
class OutsideClickObserver {
public:
    static constexpr UINT kPressedMessage = WM_APP + 44U;

    OutsideClickObserver() = default;
    ~OutsideClickObserver();
    OutsideClickObserver(const OutsideClickObserver&) = delete;
    OutsideClickObserver& operator=(const OutsideClickObserver&) = delete;
    bool start(HWND owner);
    void stop();
    bool isCurrentNotification(WPARAM generation) const;

private:
    friend struct DockVisibilityTestAccess;
    static LRESULT CALLBACK mouseProc(int code, WPARAM message, LPARAM data);
    void notifyPointerDown(POINT point);

    static OutsideClickObserver* active_;
    HWND owner_ = nullptr;
    HHOOK hook_ = nullptr;
    ULONG_PTR generation_ = 0;
};
}
