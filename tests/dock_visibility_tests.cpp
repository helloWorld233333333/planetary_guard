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
        expect(dock.registerWindowClass(), "native window class registration failed");
        dock.hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"PlanetaryGuardDockWindow", L"Dock visibility test", WS_POPUP,
            -30000, -30000, 100, 50, nullptr, nullptr, GetModuleHandleW(nullptr), &dock);
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
            expect(!IsWindowVisible(dock.edgeWindow_), "reveal must not intercept taskbar input");
            const RECT activationBounds = dock.revealBounds_;
            RECT originalBounds{};
            GetWindowRect(dock.hwnd_, &originalBounds);
            expect(activationBounds.top <= originalBounds.top,
                   "hovering the original Dock area must be able to restore it");
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(dock.targetMonitor(), &monitor);
            expect(activationBounds.bottom == monitor.rcMonitor.bottom,
                   "reveal area must reach physical bottom below the taskbar");
            dock.handleMessage(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
            expect(dock.autoHideController_.state() == dock::AutoHideState::Hidden,
                   "stale pointer message must not undo dismissal");
            // 设置手动隐藏，验证边缘消息不会意外唤出用户主动隐藏的 Dock。
            dock.manuallyHidden_ = true;
            dock.showDockFromEdge();
            expect(!IsWindowVisible(dock.hwnd_), "manual hide must win over edge events");
            dock.manuallyHidden_ = false;
            dock.updateRevealPointer({activationBounds.left - 1, activationBounds.top}, 1000);
            const POINT hover{activationBounds.left + 5, activationBounds.top + 5};
            dock.updateRevealPointer(hover, 1100);
            expect(!IsWindowVisible(dock.hwnd_), "hover delay must be honored");
            dock.updateRevealPointer(hover, 1181);
            expect(IsWindowVisible(dock.hwnd_) && IsWindowVisible(dock.backdropWindow_),
                   "hover must actually restore both native windows");
            expect(dock.autoHideController_.state() == dock::AutoHideState::Visible,
                   "native reveal must finish in visible state");
            dock.handleForegroundChanged(dock.revealedForeground_);
            expect(IsWindowVisible(dock.hwnd_), "same foreground event must not dismiss revealed Dock");
            // 不持锁的成功路径也必须立即隐藏两层窗口。
            dock.dismissAfterApplicationActivation();
            expect(!IsWindowVisible(dock.hwnd_) && !IsWindowVisible(dock.backdropWindow_),
                   "unlocked activation must hide both layers synchronously");
            // 不移动真实鼠标：将测试热区放到当前位置，验证真正的 WM_TIMER 分发。
            POINT current{};
            expect(GetCursorPos(&current), "cursor read failed");
            dock.revealBounds_ = {current.x - 100, current.y - 100, current.x + 100, current.y + 100};
            dock.hoverRevealController_.begin(false);
            dock.hoverRevealController_.update(true, 0, 80);
            SendMessageW(dock.hwnd_, WM_TIMER, 0x5051U, 0);
            expect(IsWindowVisible(dock.hwnd_) && IsWindowVisible(dock.backdropWindow_),
                   "main window timer must reach native reveal path");
        }

        // 同一应用保持前台，不能依赖前台切换事件；外部按下消息必须收起。
        for (int repeat = 0; repeat < 3; ++repeat) {
            dock.showDockFromEdge();
            expect(dock.outsideClickObserver_.hook_ != nullptr, "visible dock must observe outside clicks");
            RECT bounds{};
            GetWindowRect(dock.hwnd_, &bounds);
            const POINT inside{bounds.left + 5, bounds.top + 5};
            const POINT outside{bounds.left - 10, bounds.top};
            const auto dispatchPress = [&](POINT point) {
                // 注入自建窗口的监听入口，不生成系统输入，也不点击用户应用。
                dock.outsideClickObserver_.notifyPointerDown(point);
                MSG message{};
                while (PeekMessageW(&message, dock.hwnd_, OutsideClickObserver::kPressedMessage,
                                    OutsideClickObserver::kPressedMessage, PM_REMOVE)) {
                    DispatchMessageW(&message);
                }
            };
            dispatchPress(inside);
            expect(IsWindowVisible(dock.hwnd_), "inside click must not interrupt icon handling");
            dock.menuOpen_ = true;
            dispatchPress(outside);
            expect(IsWindowVisible(dock.hwnd_), "open menu must not be dismissed by outside observer");
            dock.menuOpen_ = false;
            const auto oldGeneration = dock.outsideClickObserver_.generation_;
            dispatchPress(outside);
            expect(!IsWindowVisible(dock.hwnd_) && !IsWindowVisible(dock.backdropWindow_),
                   "clicking the same foreground app after hover reveal must dismiss every time");
            expect(dock.outsideClickObserver_.hook_ == nullptr, "hidden dock must stop observing clicks");
            dock.showDockFromEdge();
            SendMessageW(dock.hwnd_, OutsideClickObserver::kPressedMessage, oldGeneration, 0);
            expect(IsWindowVisible(dock.hwnd_), "old click message must not dismiss a new reveal");
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
