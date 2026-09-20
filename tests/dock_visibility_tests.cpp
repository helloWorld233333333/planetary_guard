#include "platform/DockWindow.h"

#include <iostream>
#include <stdexcept>

namespace planetary::platform {
/** 只使用自建窗口测试成功事件到窗口隐藏的链路，不加载用户配置或启动外部应用。 */
struct DockVisibilityTestAccess {
    static void expect(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }

    static void run() {
        DockWindow dock(GetModuleHandleW(nullptr));
        const auto makeWindow = [] {
            return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"STATIC", L"Dock visibility test", WS_POPUP,
                -30000, -30000, 100, 50, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        };
        dock.hwnd_ = makeWindow();
        dock.backdropWindow_ = makeWindow();
        dock.edgeWindow_ = makeWindow();
        expect(dock.hwnd_ && dock.backdropWindow_ && dock.edgeWindow_, "test window creation failed");
        dock.backdropAvailable_ = true;
        dock.layout_ = dock.layoutEngine_.calculate(dock.items_, -1, 1);
        dock.targetLayout_ = dock.layout_;

        for (const bool autoHide : {false, true}) {
            dock.autoHideController_ = dock::AutoHideController(autoHide);
            ShowWindow(dock.hwnd_, SW_SHOWNOACTIVATE);
            ShowWindow(dock.backdropWindow_, SW_SHOWNOACTIVATE);
            ShowWindow(dock.edgeWindow_, SW_HIDE);
            dock.autoHideController_.acquireVisibilityLock();
            dock.autoHideController_.acquireVisibilityLock();
            dock.dismissAfterApplicationActivation();
            expect(IsWindowVisible(dock.hwnd_), "must wait for interaction to finish");
            dock.releaseVisibilityLock();
            expect(IsWindowVisible(dock.hwnd_), "nested menu must keep the outer lock");
            dock.releaseVisibilityLock();
            expect(!IsWindowVisible(dock.hwnd_), "icon window must actually hide");
            expect(!IsWindowVisible(dock.backdropWindow_), "material window must actually hide");
            expect(IsWindowVisible(dock.edgeWindow_), "edge must be available after dismissal");
            dock.handleMessage(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
            expect(dock.autoHideController_.state() == dock::AutoHideState::Hidden,
                   "stale pointer message must not undo dismissal");
            // 设置手动隐藏，验证边缘消息不会意外唤出用户主动隐藏的 Dock。
            dock.manuallyHidden_ = true;
            dock.showDockFromEdge();
            expect(!IsWindowVisible(dock.hwnd_), "manual hide must win over edge events");
            dock.manuallyHidden_ = false;
            dock.autoHideController_.onMouseEnter();
            expect(dock.autoHideController_.wantsShow(), "edge must request restoration");
            dock.autoHideController_.onShowCompleted();
            ShowWindow(dock.hwnd_, SW_SHOWNOACTIVATE);
            ShowWindow(dock.backdropWindow_, SW_SHOWNOACTIVATE);
            // 不持锁的成功路径也必须立即隐藏两层窗口。
            dock.dismissAfterApplicationActivation();
            expect(!IsWindowVisible(dock.hwnd_) && !IsWindowVisible(dock.backdropWindow_),
                   "unlocked activation must hide both layers synchronously");
        }
    }
};
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try {
        planetary::platform::DockVisibilityTestAccess::run();
        std::cout << "Native Dock activation visibility tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}
