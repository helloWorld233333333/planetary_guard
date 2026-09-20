#include "platform/OutsideClickObserver.h"

namespace planetary::platform {

OutsideClickObserver* OutsideClickObserver::active_ = nullptr;

OutsideClickObserver::~OutsideClickObserver() { stop(); }

bool OutsideClickObserver::start(HWND owner) {
    stop();
    if (!owner || !IsWindow(owner) || active_) return false;
    owner_ = owner;
    active_ = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, &OutsideClickObserver::mouseProc,
                             GetModuleHandleW(nullptr), 0);
    if (!hook_) {
        active_ = nullptr;
        owner_ = nullptr;
    }
    return hook_ != nullptr;
}

void OutsideClickObserver::stop() {
    ++generation_;
    if (active_ == this) active_ = nullptr;
    if (hook_) UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    owner_ = nullptr;
}

bool OutsideClickObserver::isCurrentNotification(WPARAM generation) const {
    return hook_ && generation == generation_;
}

/** 回调只做轻量命中判断并投递消息，原点击始终传给目标应用。 */
LRESULT CALLBACK OutsideClickObserver::mouseProc(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && active_ &&
        (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
         message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN)) {
        active_->notifyPointerDown(reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt);
    }
    return CallNextHookEx(nullptr, code, message, data);
}

void OutsideClickObserver::notifyPointerDown(POINT point) {
    if (!owner_ || !IsWindowVisible(owner_)) return;
    RECT bounds{};
    if (!GetWindowRect(owner_, &bounds) || PtInRect(&bounds, point)) return;
    const HWND clicked = WindowFromPoint(point);
    // 设置、文件选择器等拥有者为 Dock 的窗口仍属于本次交互，不算外部点击。
    if (clicked && GetAncestor(clicked, GA_ROOTOWNER) == owner_) return;
    PostMessageW(owner_, kPressedMessage, generation_, 0);
}
}
