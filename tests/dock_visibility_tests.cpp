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
        {
            // 无 HWND、不读写用户配置：刷新运行状态不得覆盖用户混合排列。
            DockWindow ordered(GetModuleHandleW(nullptr));
            ordered.items_ = {
                {"folder-first", domain::DockItemType::Folder, L"Z folder", L"C:\\dock-order-test"},
                {"file-second", domain::DockItemType::File, L"B file", L"C:\\dock-order-test.txt"},
                {"apifox-last", domain::DockItemType::Application, L"Apifox", L"C:\\dock-order-test.exe"},
            };
            for (int repeat = 0; repeat < 3; ++repeat) {
                ordered.refreshRunningState();
                expect(ordered.items_.size() >= 3 && ordered.items_[0].id == "folder-first" &&
                       ordered.items_[1].id == "file-second" && ordered.items_[2].id == "apifox-last",
                       "running refresh must preserve manual order, not regroup pinned apps before folders");
            }
        }
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
            dock.showDock();
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
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(dock.targetMonitor(), &monitor);
            expect(activationBounds.bottom == monitor.rcMonitor.bottom,
                   "reveal area must reach physical bottom below the taskbar");
            expect(activationBounds.top == monitor.rcMonitor.bottom - 2,
                   "only physical bottom two pixels should activate");
            const POINT inputArea{originalBounds.left + 10, originalBounds.top + 10};
            dock.updateRevealPointer(inputArea, 100);
            dock.updateRevealPointer(inputArea, 900);
            expect(!IsWindowVisible(dock.hwnd_), "input area must not reveal even after a long dwell");
            dock.handleMessage(WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
            expect(dock.autoHideController_.state() == dock::AutoHideState::Hidden,
                   "stale pointer message must not undo dismissal");
            // 设置手动隐藏，验证边缘消息不会意外唤出用户主动隐藏的 Dock。
            dock.manuallyHidden_ = true;
            dock.showDockFromEdge();
            expect(!IsWindowVisible(dock.hwnd_), "manual hide must win over edge events");
            dock.manuallyHidden_ = false;
            dock.updateRevealPointer({activationBounds.left - 1, activationBounds.top}, 1000);
            const POINT hover{activationBounds.left + 5, activationBounds.bottom - 1};
            dock.updateRevealPointer(hover, 1100);
            expect(!IsWindowVisible(dock.hwnd_), "hover delay must be honored");
            dock.updateRevealPointer(hover, 1399);
            expect(!IsWindowVisible(dock.hwnd_), "299ms must still stay hidden");
            dock.updateRevealPointer(hover, 1400);
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
            dock.updateRevealPointer(hover, 2000, true);
            dock.updateRevealPointer(hover, 2500, true);
            dock.updateRevealPointer(hover, 2800, false);
            expect(!IsWindowVisible(dock.hwnd_), "drag and release at bottom must not reveal");
            dock.updateRevealPointer(inputArea, 2900);
            dock.updateRevealPointer(hover, 3000);
            dock.updateRevealPointer(hover, 3300);
            expect(IsWindowVisible(dock.hwnd_), "hover should recover after leaving dragged edge");
            dock.dismissAfterApplicationActivation();
            // 不移动真实鼠标：检验真实定时器会刷新几何，而不是沿用过期热区。
            POINT current{};
            expect(GetCursorPos(&current), "cursor read failed");
            dock.revealBounds_ = {current.x - 100, current.y - 100, current.x + 100, current.y + 100};
            dock.hoverRevealController_.begin(false);
            dock.hoverRevealController_.update(true, 0, 300);
            SendMessageW(dock.hwnd_, WM_TIMER, 0x5051U, 0);
            GetMonitorInfoW(MonitorFromPoint(current, MONITOR_DEFAULTTONEAREST), &monitor);
            expect(dock.revealBounds_.top == monitor.rcMonitor.bottom - 2 &&
                   dock.revealBounds_.bottom == monitor.rcMonitor.bottom,
                   "main timer must refresh physical edge even when taskbar changes");
        }

        // 真实显示器几何 + 自建 HWND：不移动系统鼠标，不修改用户配置。
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));
        dock.settings_.behavior.hideInFullscreen = false;
        ULONGLONG tick = 10000;
        for (const HMONITOR monitor : monitors) {
            MONITORINFO info{sizeof(MONITORINFO)};
            GetMonitorInfoW(monitor, &info);
            const POINT edge{(info.rcMonitor.left + info.rcMonitor.right) / 2, info.rcMonitor.bottom - 1};
            dock.dismissAfterApplicationActivation();
            dock.updateRevealPointer({edge.x, edge.y - 100}, tick);
            dock.updateRevealPointer(edge, tick + 100);
            expect(!IsWindowVisible(dock.hwnd_), "cross-screen reveal must wait");
            dock.updateRevealPointer(edge, tick + 400);
            expect(IsWindowVisible(dock.hwnd_), "each connected screen must reveal dock");
            expect(MonitorFromWindow(dock.hwnd_, MONITOR_DEFAULTTONEAREST) == monitor,
                   "actual dock HWND must move to candidate monitor");
            expect(MonitorFromWindow(dock.backdropWindow_, MONITOR_DEFAULTTONEAREST) == monitor,
                   "glass HWND must move with dock");
            tick += 1000;
        }
        std::cout << "Native monitor migration tested on " << monitors.size() << " display(s).\n";
        if (monitors.size() > 1) {
            const HMONITOR destination = monitors.front();
            MONITORINFO info{sizeof(MONITORINFO)};
            GetMonitorInfoW(destination, &info);
            const POINT edge{(info.rcMonitor.left + info.rcMonitor.right) / 2, info.rcMonitor.bottom - 1};
            dock.updateRevealPointer(edge, tick);
            expect(MonitorFromWindow(dock.hwnd_, MONITOR_DEFAULTTONEAREST) != destination,
                   "visible dock must not jump before dwell");
            dock.updateRevealPointer(edge, tick + 300);
            expect(IsWindowVisible(dock.hwnd_) &&
                   MonitorFromWindow(dock.hwnd_, MONITOR_DEFAULTTONEAREST) == destination,
                   "visible dock must also migrate after cross-screen dwell");
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
