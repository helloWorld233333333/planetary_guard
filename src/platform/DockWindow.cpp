#include "platform/DockWindow.h"
#include "platform/WindowCatalog.h"
#include "platform/DesktopGeometry.h"

#include <commdlg.h>
#include <commctrl.h>
#include <combaseapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace planetary::platform {

namespace {

constexpr wchar_t kWindowClassName[] = L"PlanetaryGuardDockWindow";
constexpr wchar_t kEdgeWindowClassName[] = L"PlanetaryGuardDockEdgeWindow";
constexpr wchar_t kWindowTitle[] = L"Planetary Guard Dock";
constexpr int kMinimumDockWidth = 160;
constexpr std::size_t kMaximumDockItems = 200U;
constexpr int kEdgeActivationHeight = 3;
constexpr UINT kHideTimerId = 0x5047U;
constexpr UINT kShowTimerId = 0x5048U;
constexpr UINT kRunningStateTimerId = 0x5049U;
constexpr UINT kAnimationTimerId = 0x5050U;
constexpr UINT kRevealTimerId = 0x5051U;
constexpr UINT kContextOpenCommand = 1001U;
constexpr UINT kContextRemoveCommand = 1002U;
constexpr UINT kContextAddCommand = 1003U;
constexpr UINT kContextChooseIconCommand = 1004U;
constexpr UINT kContextResetIconCommand = 1005U;
constexpr UINT kContextAddApplicationCommand = 1006U;
constexpr UINT kContextPinCommand = 1007U;
constexpr UINT kContextRecycleCommand = 1008U;
constexpr UINT kContextEmptyRecycleCommand = 1009U;

enum class AccentState : int {
    Disabled = 0,
    BlurBehind = 2,
};

struct AccentPolicy {
    AccentState state = AccentState::Disabled;
    DWORD flags = 0U;
    DWORD gradientColor = 0U;
    DWORD animationId = 0U;
};

struct WindowCompositionAttributeData {
    int attribute = 0;
    void* data = nullptr;
    SIZE_T size = 0U;
};

using SetWindowCompositionAttributeFunction = BOOL(WINAPI*)(
    HWND,
    WindowCompositionAttributeData*);

constexpr int kWindowCompositionAccentPolicy = 19;

/** 材质窗口只让 DWM 合成背景，禁止 STATIC 控件绘制文字和不透明底色。 */
LRESULT CALLBACK materialWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

DockWindow::DockWindow(HINSTANCE instance, bool inspectWindow)
    : instance_(instance),
      inspectWindow_(inspectWindow),
      layoutEngine_(48.0F, 8.0F, 1.4F),
      items_({
          {"notepad", domain::DockItemType::Application, L"记事本", L"notepad.exe"},
          {"explorer", domain::DockItemType::Application, L"文件", L"explorer.exe"},
          {"terminal", domain::DockItemType::Application, L"终端", L"cmd.exe"},
      }) {}

DockWindow::~DockWindow() {
    outsideClickObserver_.stop();
    iconLoader_.stop();
    trayController_.remove();
    if (edgeWindow_ != nullptr && IsWindow(edgeWindow_)) {
        DestroyWindow(edgeWindow_);
        edgeWindow_ = nullptr;
    }
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    if (backdropWindow_ != nullptr && IsWindow(backdropWindow_)) DestroyWindow(backdropWindow_);
}

bool DockWindow::registerWindowClass() {
    if (classRegistered_) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &DockWindow::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0U) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    classRegistered_ = true;
    return true;
}

bool DockWindow::registerEdgeWindowClass() {
    if (edgeClassRegistered_) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &DockWindow::edgeWindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kEdgeWindowClassName;
    if (RegisterClassExW(&windowClass) == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    edgeClassRegistered_ = true;
    return true;
}

bool DockWindow::create() {
    creationError_.clear();
    if (!registerWindowClass() || !registerEdgeWindowClass()) {
        creationError_ = L"无法注册 Dock 窗口类。";
        return false;
    }

    hwnd_ = CreateWindowExW((inspectWindow_ ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW) | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                                WS_EX_LAYERED,
                            kWindowClassName,
                            kWindowTitle,
                            WS_POPUP,
                            0,
                            0,
                            240,
                            88,
                            nullptr,
                            nullptr,
                            instance_,
                            this);
    if (hwnd_ == nullptr) {
        creationError_ = L"无法创建 Dock 主窗口。";
        return false;
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    loadSettings();
    loadItems();
    // 系统模糊放在独立非 Layered 窗口；图标层保留逐像素 Alpha。
    WNDCLASSW materialClass{};
    materialClass.lpfnWndProc = materialWindowProc;
    materialClass.hInstance = instance_;
    materialClass.lpszClassName = L"PlanetaryGuardMaterial";
    RegisterClassW(&materialClass);
    backdropWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        materialClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, instance_, nullptr);
    applyBackdropEffect();
    DragAcceptFiles(hwnd_, TRUE);
    dpiScale_ = readDpiScale();
    if (!renderer_.initialize(hwnd_)) {
        creationError_ = L"Direct2D/DirectWrite 渲染器初始化失败。";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    renderer_.applyAppearance(settings_.appearance);
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&commonControls);
    tooltipWindow_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_, nullptr, instance_, nullptr);
    if (tooltipWindow_ != nullptr) {
        TOOLINFOW tool{sizeof(TOOLINFOW)};
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.hwnd = hwnd_;
        tool.uId = 1;
        tool.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(tooltipWindow_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    iconLoader_.start(hwnd_);
    requestIcons();
    edgeWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  kEdgeWindowClassName,
                                  kWindowTitle,
                                  WS_POPUP,
                                  0,
                                  0,
                                  1,
                                  kEdgeActivationHeight,
                                  nullptr,
                                  nullptr,
                                  instance_,
                                  this);
    if (edgeWindow_ == nullptr) {
        creationError_ = L"无法创建自动隐藏边缘窗口。";
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    trayController_.initialize(hwnd_);
    fullscreenDetector_.initialize(hwnd_);
    autoHideController_.setEnabled(settings_.behavior.autoHide);
    const float maxScale = settings_.appearance.reduceMotion
                               ? 1.0F
                               : std::clamp(
                                     settings_.appearance.maxIconSizeDip /
                                         std::max(settings_.appearance.iconSizeDip, 1.0F),
                                     1.0F,
                                     2.0F);
    layoutEngine_ = layout::LayoutEngine(settings_.appearance.iconSizeDip, 8.0F, maxScale);
    const std::wstring previousMonitorId = settings_.placement.monitorId;
    updateLayout();
    refreshRunningState();
    SetTimer(hwnd_, kRunningStateTimerId, 2000U, nullptr);
    if (settings_.placement.monitorId != previousMonitorId) {
        // 初次定位也记录下来；保存失败不影响 Dock 正常运行。
        settingsStore_.save(settings_, nullptr);
    }
    return true;
}

void DockWindow::show() {
    if (hwnd_ == nullptr) {
        return;
    }
    showDock();
}

HWND DockWindow::handle() const {
    return hwnd_;
}

const std::wstring& DockWindow::creationError() const {
    return creationError_;
}

void DockWindow::positionWindow() {
    if (hwnd_ == nullptr) {
        return;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = targetMonitor();
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }

    // MONITORINFOEXW 与 MONITORINFO 共享前缀，单独获取设备名避免扩大现有结构。
    MONITORINFOEXW monitorInfoEx{};
    monitorInfoEx.cbSize = sizeof(monitorInfoEx);
    if (GetMonitorInfoW(monitor, &monitorInfoEx) != FALSE) {
        const std::wstring activeMonitorId = monitorInfoEx.szDevice;
        if (!activeMonitorId.empty()) {
            settings_.placement.monitorId = activeMonitorId;
        }
    }

    // 查询目标显示器底边的自动隐藏任务栏。只移动 Dock，不改变任务栏设置。
    APPBARDATA taskbar{sizeof(APPBARDATA)};
    taskbar.uEdge = ABE_BOTTOM;
    taskbar.rc = monitorInfo.rcMonitor;
    const HWND autoHideBar = reinterpret_cast<HWND>(SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &taskbar));
    RECT taskbarRect{};
    const LONG taskbarHeight = autoHideBar && GetWindowRect(autoHideBar, &taskbarRect)
                                   ? taskbarRect.bottom - taskbarRect.top : 0;
    const RECT workArea = dockWorkArea(monitorInfo.rcMonitor, monitorInfo.rcWork, taskbarHeight);
    const int workWidth = workArea.right - workArea.left;
    const int width = std::max(kMinimumDockWidth, static_cast<int>(layout_.width));
    const int height = std::max(1, static_cast<int>(layout_.height));
    const int x = workArea.left + std::max(0, (workWidth - width) / 2);
    const int bottomMargin = std::max(0,
                                      static_cast<int>(settings_.placement.bottomMarginDip * dpiScale_));
    const int y = workArea.bottom - height - bottomMargin;

    RECT previousBounds{};
    GetWindowRect(hwnd_, &previousBounds);
    if (previousBounds.left != x || previousBounds.top != y ||
        previousBounds.right - previousBounds.left != width || previousBounds.bottom - previousBounds.top != height)
    SetWindowPos(hwnd_,
                 HWND_TOPMOST,
                 x,
                 y,
                 width,
                 height,
                 SWP_NOACTIVATE);

    // 逐像素 Alpha 已定义轮廓；窗口区域裁切会截断放大图标和阴影。
    if (backdropWindow_ != nullptr) {
        const int panelHeight = std::max(1, static_cast<int>(layout_.panelHeight) - 7);
        const int panelWidth = std::max(1, static_cast<int>(layout_.panelWidth) - 8);
        SetWindowPos(backdropWindow_, hwnd_, x + static_cast<int>(layout_.panelLeft) + 4, y + height - panelHeight - 5,
                     panelWidth, panelHeight, SWP_NOACTIVATE | SWP_NOCOPYBITS);
        if (backdropSize_.cx != panelWidth || backdropSize_.cy != panelHeight) {
            const int diameter = static_cast<int>(std::clamp(panelHeight * 0.54F, 32.0F, 46.0F));
            HRGN region = CreateRoundRectRgn(0, 0, panelWidth + 1,
                                            panelHeight + 1, diameter, diameter);
            // 扩容后旧裁切区域必须失效，否则 DWM 可能保留新增区域的直角接缝。
            if (region && !SetWindowRgn(backdropWindow_, region, TRUE)) DeleteObject(region);
            backdropSize_ = {panelWidth, panelHeight};
            RedrawWindow(backdropWindow_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
    }
}

void DockWindow::positionEdgeWindow() {
    if (edgeWindow_ == nullptr || hwnd_ == nullptr) return;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = targetMonitor();
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo)) return;

    const RECT& workArea = monitorInfo.rcWork;
    const int width = std::max(kMinimumDockWidth, static_cast<int>(layout_.width));
    const int workWidth = static_cast<int>(workArea.right - workArea.left);
    const int x = static_cast<int>(workArea.left) + std::max(0, (workWidth - width) / 2);
    const int y = workArea.bottom - kEdgeActivationHeight;
    SetWindowPos(edgeWindow_,
                 HWND_TOPMOST,
                 x,
                 y,
                 width,
                 kEdgeActivationHeight,
                 SWP_NOACTIVATE);
}

HMONITOR DockWindow::targetMonitor() const {
    if (!settings_.placement.monitorId.empty()) {
        MonitorSearchContext context{};
        context.targetId = &settings_.placement.monitorId;
        EnumDisplayMonitors(nullptr,
                            nullptr,
                            &DockWindow::findMonitorProc,
                            reinterpret_cast<LPARAM>(&context));
        if (context.match != nullptr) return context.match;
    }
    return MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
}

std::wstring DockWindow::monitorId(HMONITOR monitor) {
    if (monitor == nullptr) return {};
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, &info) != FALSE ? std::wstring(info.szDevice) : std::wstring();
}

BOOL CALLBACK DockWindow::findMonitorProc(HMONITOR monitor,
                                           HDC /*deviceContext*/,
                                           LPRECT /*monitorRect*/,
                                           LPARAM data) {
    auto* context = reinterpret_cast<MonitorSearchContext*>(data);
    if (context == nullptr || context->targetId == nullptr) return FALSE;
    if (monitorId(monitor) == *context->targetId) {
        context->match = monitor;
        return FALSE;
    }
    return TRUE;
}

void DockWindow::showDock() {
    if (hwnd_ == nullptr) return;
    KillTimer(hwnd_, kRevealTimerId);
    revealedForeground_ = GetForegroundWindow();
    KillTimer(hwnd_, kHideTimerId);
    if (edgeWindow_ != nullptr) KillTimer(edgeWindow_, kShowTimerId);
    trackingEdgeMouse_ = false;
    ShowWindow(edgeWindow_, SW_HIDE);
    positionWindow();
    renderer_.render(layout_, items_);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (backdropAvailable_) ShowWindow(backdropWindow_, SW_SHOWNOACTIVATE);
    positionWindow();
    UpdateWindow(hwnd_);
    if (autoHideController_.wantsShow()) {
        autoHideController_.onShowCompleted();
    }
    outsideClickObserver_.start(hwnd_);
    // 可见时仍允许从另一块屏幕触底迁移；同屏不重复唤出。
    SetTimer(hwnd_, kRevealTimerId, 50U, nullptr);
}

void DockWindow::hideDock() {
    if (hwnd_ == nullptr) return;
    outsideClickObserver_.stop();
    KillTimer(hwnd_, kHideTimerId);
    KillTimer(hwnd_, kAnimationTimerId);
    animating_ = false;
    pointerLayoutPending_ = false;
    pointerX_ = -1.0F;
    trackingMouse_ = false;
    if (tooltipWindow_ != nullptr) {
        TOOLINFOW tool{sizeof(TOOLINFOW)};
        tool.hwnd = hwnd_;
        tool.uId = 1;
        SendMessageW(tooltipWindow_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    layout_ = layoutEngine_.calculate(items_, pointerX_, dpiScale_);
    targetLayout_ = layout_;
    ShowWindow(hwnd_, SW_HIDE);
    ShowWindow(backdropWindow_, SW_HIDE);
    if (autoHideController_.wantsHide() && !manuallyHidden_ && !hiddenForFullscreen_) {
        autoHideController_.onHideCompleted();
        startRevealWatch();
    }
}

/** 热区始终基于物理屏幕边界，不随任务栏展开或隐藏而变化。 */
bool DockWindow::refreshRevealBounds() {
    RECT dockBounds{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetWindowRect(hwnd_, &dockBounds) || !GetMonitorInfoW(targetMonitor(), &monitor)) return false;
    revealBounds_ = bottomRevealBounds(monitor.rcMonitor, dockBounds);
    return true;
}

/** 隐藏时检测底边停留，不创建拦截鼠标的窗口，系统任务栏覆盖边缘也不影响检测。 */
void DockWindow::startRevealWatch() {
    if (manuallyHidden_) return;
    ShowWindow(edgeWindow_, SW_HIDE);
    if (!refreshRevealBounds()) return;
    POINT pointer{};
    const bool inside = GetCursorPos(&pointer) && PtInRect(&revealBounds_, pointer);
    hoverRevealController_.begin(inside);
    SetTimer(hwnd_, kRevealTimerId, 50U, nullptr);
}

void DockWindow::pollRevealPointer() {
    ++revealPollCount_;
    POINT pointer{};
    if (!GetCursorPos(&pointer)) {
        hoverRevealController_.begin(false);
        return;
    }
    const bool pointerPressed = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
        GetAsyncKeyState(VK_MBUTTON) | GetAsyncKeyState(VK_XBUTTON1) | GetAsyncKeyState(VK_XBUTTON2)) & 0x8000) != 0;
    updateRevealPointer(pointer, GetTickCount64(), pointerPressed);
}

void DockWindow::updateRevealPointer(POINT pointer, ULONGLONG now, bool pointerPressed) {
    if (manuallyHidden_ || menuOpen_ || autoHideController_.visibilityLockCount() != 0) {
        hoverRevealController_.begin(false);
        return;
    }
    const HMONITOR candidate = MonitorFromPoint(pointer, MONITOR_DEFAULTTONULL);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!candidate || !GetMonitorInfoW(candidate, &monitor)) {
        hoverRevealController_.begin(false);
        return;
    }
    const bool sameMonitor = candidate == targetMonitor();
    if (sameMonitor) {
        if (!refreshRevealBounds()) return;
    } else {
        revealBounds_ = centeredBottomRevealBounds(monitor.rcMonitor, static_cast<LONG>(layout_.width));
    }
    const bool blocked = (sameMonitor && IsWindowVisible(hwnd_)) ||
        (settings_.behavior.hideInFullscreen && fullscreenDetector_.isFullscreenOnMonitor(candidate));
    if (blocked) {
        hoverRevealController_.begin(false);
        return;
    }
    if (hoverRevealController_.update(PtInRect(&revealBounds_, pointer) != FALSE, now,
            std::max(300U, settings_.behavior.showDelayMs), pointerPressed,
            reinterpret_cast<std::uintptr_t>(candidate))) {
        if (!sameMonitor) {
            // 不保存每次迁移：旧首选屏幕仅决定启动位置，运行时触底优先。
            settings_.placement.monitorId = monitorId(candidate);
            pointerX_ = -1.0F;
            positionWindow();
            dpiScale_ = readDpiScale();
            updateLayout();
            positionWindow();
        }
        hiddenForFullscreen_ = false;
        autoHideController_.setFullscreen(false);
        showDockFromEdge();
    }
}

/** 成功打开应用后同步记录意图；菜单或鼠标锁释放时再执行，不依赖消息时序。 */
void DockWindow::dismissAfterApplicationActivation() {
    if (manuallyHidden_ || hiddenForFullscreen_) return;
    autoHideController_.onApplicationActivated();
    if (autoHideController_.wantsHide()) hideDock();
}

void DockWindow::releaseVisibilityLock() {
    autoHideController_.releaseVisibilityLock();
    if (autoHideController_.wantsHide()) hideDock();
}

/** 外部应用获得前台也收起；桌面、任务栏和 Dock 自己的设置窗口不触发。 */
void DockWindow::handleForegroundChanged(HWND foreground) {
    if (!foreground || foreground != GetForegroundWindow() || !IsWindowVisible(foreground)) return;
    // 唤出时的前台应用没有变化，迟到或重复的通知不应立即收起刚显示的 Dock。
    if (foreground == revealedForeground_) return;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!processId || processId == GetCurrentProcessId()) return;
    const LONG_PTR style = GetWindowLongPtrW(foreground, GWL_EXSTYLE);
    if (style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return;
    wchar_t name[128]{};
    GetClassNameW(foreground, name, static_cast<int>(std::size(name)));
    if (lstrcmpW(name, L"Progman") == 0 || lstrcmpW(name, L"WorkerW") == 0 ||
        lstrcmpW(name, L"Shell_TrayWnd") == 0 || lstrcmpW(name, L"Shell_SecondaryTrayWnd") == 0) return;
    dismissAfterApplicationActivation();
}

void DockWindow::showDockFromEdge() {
    if (manuallyHidden_ || hiddenForFullscreen_) return;
    autoHideController_.forceShow();
    showDock();
}

void DockWindow::applyWindowOpacity() {
    // 透明度只应用到底板画刷，不再用整窗口 Alpha 把图标一起变灰。
    if (hwnd_ != nullptr) renderer_.render(layout_, items_);
}

void DockWindow::applyBackdropEffect() {
    if (backdropWindow_ == nullptr) return;
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    const auto setWindowCompositionAttribute =
        reinterpret_cast<SetWindowCompositionAttributeFunction>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (setWindowCompositionAttribute == nullptr) return;

    const bool highContrast = settings_.appearance.themeMode == domain::ThemeMode::HighContrast;
    // GradientColor 格式为 AABBGGRR，低 Alpha 只负责给系统模糊层染色。
    const DWORD tint = 0x18FFFFFFU;
    AccentPolicy policy{};
    policy.state = highContrast ? AccentState::Disabled : AccentState::BlurBehind;
    policy.flags = highContrast ? 0U : 2U;
    policy.gradientColor = highContrast ? 0U : tint;
    WindowCompositionAttributeData data{};
    data.attribute = kWindowCompositionAccentPolicy;
    data.data = &policy;
    data.size = sizeof(policy);
    backdropAvailable_ = setWindowCompositionAttribute(backdropWindow_, &data) != FALSE && !highContrast;
    if (!backdropAvailable_) ShowWindow(backdropWindow_, SW_HIDE);
}

void DockWindow::scheduleHide() {
    if (hwnd_ == nullptr || autoHideController_.state() != dock::AutoHideState::HidePending) {
        return;
    }
    SetTimer(hwnd_,
             kHideTimerId,
             std::max(20U, settings_.behavior.hideDelayMs),
             nullptr);
}

void DockWindow::updateLayout(bool configure) {
    if (!configure && !settings_.appearance.reduceMotion && !layout_.items.empty()) {
        // 高频鼠标消息仅保留最新坐标，每个动画帧最多计算一次布局。
        pointerLayoutPending_ = true;
        if (!animating_) {
            animating_ = true;
            lastAnimationTick_ = GetTickCount64();
            SetTimer(hwnd_, kAnimationTimerId, 16U, nullptr);
        }
        return;
    }
    pointerLayoutPending_ = false;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(targetMonitor(), &monitor)) {
        const float available = static_cast<float>(monitor.rcWork.right - monitor.rcWork.left) - 40.0F;
        const float count = static_cast<float>(std::count_if(items_.begin(), items_.end(), [](const auto& item) { return item.enabled; }));
        const float configured = settings_.appearance.iconSizeDip;
        const float magnification = settings_.appearance.reduceMotion ? 1.0F : std::clamp(settings_.appearance.maxIconSizeDip / configured, 1.0F, 2.0F);
        const float reservedSlots = std::min(count, 6.0F) * (magnification - 1.0F);
        const float size = count > 0 ? std::min(configured, std::max(16.0F,
            (available / dpiScale_ - 20.0F - count * 10.0F) / (count + reservedSlots))) : configured;
        layoutEngine_ = layout::LayoutEngine(size, 8.0F, magnification);
    }
    targetLayout_ = layoutEngine_.calculate(items_, pointerX_, dpiScale_);
    const bool sameItems = layout_.items.size() == targetLayout_.items.size() &&
        std::equal(layout_.items.begin(), layout_.items.end(), targetLayout_.items.begin(),
                   [](const auto& a, const auto& b) { return a.id == b.id; });
    if (sameItems && !layout_.items.empty() && !settings_.appearance.reduceMotion &&
        layout_.width == targetLayout_.width && layout_.height == targetLayout_.height) {
        if (!animating_) {
            animating_ = true;
            lastAnimationTick_ = GetTickCount64();
            SetTimer(hwnd_, kAnimationTimerId, 16U, nullptr);
        }
        return;
    }
    KillTimer(hwnd_, kAnimationTimerId);
    animating_ = false;
    layout_ = targetLayout_;
    positionWindow();
    // Layered 窗口的内容由 UpdateLayeredWindow 提交，不能只等待
    // 可能不会到来的 WM_PAINT。
    renderer_.render(layout_, items_);
}

void DockWindow::advanceAnimation() {
    if (pointerLayoutPending_) {
        pointerLayoutPending_ = false;
        targetLayout_ = layoutEngine_.calculate(items_, pointerX_, dpiScale_);
    }
    const ULONGLONG now = GetTickCount64();
    const float elapsed = static_cast<float>(now - lastAnimationTick_);
    lastAnimationTick_ = now;
    const float factor = 1.0F - std::exp(-std::min(elapsed, 64.0F) / 55.0F);
    float remaining = 0.0F;
    const auto approach = [&](float& value, float target) {
        value += (target - value) * factor;
        remaining = std::max(remaining, std::abs(target - value));
    };
    approach(layout_.width, targetLayout_.width);
    approach(layout_.height, targetLayout_.height);
    layout_.panelHeight = targetLayout_.panelHeight;
    layout_.panelLeft = targetLayout_.panelLeft;
    layout_.panelWidth = targetLayout_.panelWidth;
    for (std::size_t i = 0; i < layout_.items.size(); ++i) {
        auto& current = layout_.items[i];
        const auto& target = targetLayout_.items[i];
        approach(current.left, target.left);
        approach(current.right, target.right);
        approach(current.top, target.top);
        approach(current.bottom, target.bottom);
        approach(current.centerX, target.centerX);
        approach(current.scale, target.scale);
    }
    if (remaining < 0.15F) {
        layout_ = targetLayout_;
        KillTimer(hwnd_, kAnimationTimerId);
        animating_ = false;
    }
    renderer_.render(layout_, items_);
}

void DockWindow::requestIcons() {
    iconLoader_.request(items_);
}

void DockWindow::handleIconResults() {
    bool changed = false;
    for (const IconPixels& pixels : iconLoader_.takeResults()) {
        if (findItemById(pixels.itemId) == nullptr) continue;
        renderer_.setIconPixels(pixels);
        changed = true;
    }
    if (changed && hwnd_ != nullptr) {
        renderer_.render(layout_, items_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void DockWindow::refreshRunningState() {
    if (!pressedItemId_.empty() || menuOpen_ || trackingMouse_ || animating_) return;
    const auto windows = enumerateApplicationWindows();
    std::unordered_set<std::wstring> runningExecutables;
    for (const auto& window : windows) runningExecutables.insert(window.executable);
    bool changed = false;
    const auto removed = std::remove_if(items_.begin(), items_.end(), [&](const auto& item) {
        if (!item.transient || runningExecutables.contains(applicationIdentity(item.targetPath))) return false;
        renderer_.clearIcon(item.id);
        changed = true;
        return true;
    });
    items_.erase(removed, items_.end());
    std::unordered_set<std::wstring> represented;
    for (domain::DockItem& item : items_) {
        const auto identity = applicationIdentity(item.targetPath);
        if (!identity.empty()) represented.insert(identity);
        const bool running = !identity.empty() && runningExecutables.contains(identity);
        if (item.running != running) {
            item.running = running;
            changed = true;
        }
    }
    for (const auto& window : windows) {
        if (window.executable.empty() || represented.contains(window.executable) || items_.size() >= 40U) continue;
        represented.insert(window.executable);
        domain::DockItem item;
        item.id = "running-" + createItemId();
        item.type = domain::DockItemType::Application;
        item.displayName = std::filesystem::path(window.executable).stem().wstring();
        item.targetPath = window.executable;
        item.running = true;
        item.transient = true;
        items_.push_back(std::move(item));
        changed = true;
    }
    // items_ 的相对顺序由用户拖拽决定。刷新只删除已退出的临时应用、
    // 更新运行标记并在末尾追加新应用，不能按类型重新分组覆盖手动排序。
    if (changed && hwnd_ != nullptr) {
        requestIcons();
        updateLayout();
    }
}

void DockWindow::beginMouseTracking() {
    if (trackingMouse_ || hwnd_ == nullptr) {
        return;
    }
    TRACKMOUSEEVENT trackingEvent{};
    trackingEvent.cbSize = sizeof(trackingEvent);
    trackingEvent.dwFlags = TME_LEAVE;
    trackingEvent.hwndTrack = hwnd_;
    trackingMouse_ = TrackMouseEvent(&trackingEvent) != FALSE;
}

float DockWindow::readDpiScale() const {
    // GetDpiForWindow 在 Windows 10 可用；失败时退回系统默认 DPI。
    const UINT dpi = hwnd_ == nullptr ? 96U : GetDpiForWindow(hwnd_);
    return std::max(0.5F, static_cast<float>(dpi) / 96.0F);
}

void DockWindow::loadItems() {
    std::vector<domain::DockItem> loadedItems;
    std::wstring errorMessage;
    if (itemStore_.load(loadedItems, &errorMessage)) {
        items_ = std::move(loadedItems);
        return;
    }

    // 首次启动写入默认项目；损坏配置保留原文件，避免覆盖现场证据。
    if (!std::filesystem::exists(itemStore_.filePath())) {
        itemStore_.save(items_, nullptr);
    }
}

void DockWindow::loadSettings() {
    domain::AppSettings loadedSettings;
    std::wstring errorMessage;
    if (settingsStore_.load(loadedSettings, &errorMessage)) {
        settings_ = loadedSettings;
        if (settings_.appearance.visualRevision < 2U) {
            // 只替换旧版的精确默认值；用户主动调整的外观仍保留。
            if (std::fabs(settings_.appearance.iconSizeDip - 48.0F) < 0.01F &&
                std::fabs(settings_.appearance.maxIconSizeDip - 68.0F) < 0.01F) {
                settings_.appearance.iconSizeDip = 54.0F;
                settings_.appearance.maxIconSizeDip = 74.0F;
            }
            if (std::fabs(settings_.appearance.dockOpacity - 0.90F) < 0.01F) {
                settings_.appearance.dockOpacity = 0.72F;
            }
        }
        if (settings_.appearance.visualRevision < 4U) {
            settings_.appearance.unifiedIconTiles = false;
            settings_.appearance.visualRevision = 4U;
            settingsStore_.save(settings_, nullptr);
        }
    } else if (!std::filesystem::exists(settingsStore_.filePath())) {
        settingsStore_.save(settings_, nullptr);
    }

    // 注册表是开机启动的实际来源；设置文件只保存用户上一次的选择。
    settings_.behavior.launchAtStartup = startupManager_.isEnabled();
}

bool DockWindow::saveSettings() {
    std::wstring errorMessage;
    if (settingsStore_.save(settings_, &errorMessage)) {
        return true;
    }
    MessageBoxW(hwnd_,
                errorMessage.empty() ? L"无法保存 Dock 设置。" : errorMessage.c_str(),
                L"Planetary Guard",
                MB_OK | MB_ICONWARNING);
    return false;
}

bool DockWindow::saveItems() {
    std::wstring errorMessage;
    std::vector<domain::DockItem> pinned;
    for (const auto& item : items_) if (!item.transient) pinned.push_back(item);
    if (itemStore_.save(pinned, &errorMessage)) {
        return true;
    }
    MessageBoxW(hwnd_,
                errorMessage.empty() ? L"无法保存 Dock 配置。" : errorMessage.c_str(),
                L"Planetary Guard",
                MB_OK | MB_ICONWARNING);
    return false;
}

std::string DockWindow::createItemId() const {
    GUID guid{};
    if (SUCCEEDED(CoCreateGuid(&guid))) {
        wchar_t guidText[40]{};
        if (StringFromGUID2(guid, guidText, static_cast<int>(std::size(guidText))) > 0) {
            const int required = WideCharToMultiByte(CP_UTF8,
                                                     WC_ERR_INVALID_CHARS,
                                                     guidText,
                                                     -1,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     nullptr);
            if (required > 1) {
                std::string result(static_cast<std::size_t>(required), '\0');
                WideCharToMultiByte(CP_UTF8,
                                    WC_ERR_INVALID_CHARS,
                                    guidText,
                                    -1,
                                    result.data(),
                                    required,
                                    nullptr,
                                    nullptr);
                result.pop_back();
                return result;
            }
        }
    }
    return "item-" + std::to_string(GetTickCount64());
}

bool DockWindow::addPathItem(const std::wstring& path) {
    if (path.empty() || items_.size() >= kMaximumDockItems) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;

    domain::DockItem item;
    item.id = createItemId();
    item.targetPath = path;
    item.type = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U
                    ? domain::DockItemType::Folder
                    : domain::DockItemType::File;
    const std::filesystem::path filePath(path);
    item.displayName = filePath.filename().wstring();
    if (item.displayName.empty()) item.displayName = path;
    const std::wstring extension = filePath.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".lnk") == 0) {
        item.type = domain::DockItemType::Shortcut;
        item.displayName = filePath.stem().wstring();
    } else if (_wcsicmp(extension.c_str(), L".exe") == 0 ||
               _wcsicmp(extension.c_str(), L".com") == 0) {
        item.type = domain::DockItemType::Application;
        item.displayName = filePath.stem().wstring();
    }
    item.order = static_cast<int>(items_.size() * 10U);
    items_.push_back(std::move(item));
    return true;
}

void DockWindow::openApplicationPicker() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return;
    }
    dialog->SetTitle(L"选择要添加的应用");
    const COMDLG_FILTERSPEC filters[] = {
        {L"应用和快捷方式", L"*.exe;*.lnk;*.com"},
        {L"所有文件", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1U);
    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST |
                           FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    Microsoft::WRL::ComPtr<IShellItem> programsFolder;
    PIDLIST_ABSOLUTE programsId = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Programs, 0U, nullptr, &programsId)) &&
        programsId != nullptr) {
        if (SUCCEEDED(SHCreateItemFromIDList(programsId,
                                             IID_PPV_ARGS(programsFolder.GetAddressOf())))) {
            dialog->SetDefaultFolder(programsFolder.Get());
        }
        CoTaskMemFree(programsId);
    }

    if (dialog->Show(hwnd_) == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    Microsoft::WRL::ComPtr<IShellItemArray> selectedItems;
    if (FAILED(dialog->GetResults(selectedItems.GetAddressOf()))) return;

    DWORD count = 0U;
    if (FAILED(selectedItems->GetCount(&count))) return;
    bool changed = false;
    for (DWORD index = 0U; index < count; ++index) {
        Microsoft::WRL::ComPtr<IShellItem> selectedItem;
        if (FAILED(selectedItems->GetItemAt(index, selectedItem.GetAddressOf()))) continue;
        PWSTR selectedPath = nullptr;
        if (SUCCEEDED(selectedItem->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath)) &&
            selectedPath != nullptr) {
            changed = addPathItem(selectedPath) || changed;
            CoTaskMemFree(selectedPath);
        }
    }
    if (changed) {
        saveItems();
        requestIcons();
        updateLayout();
    }
}

void DockWindow::addItemsFromDrop(HDROP dropHandle) {
    const UINT count = DragQueryFileW(dropHandle, 0xFFFFFFFFU, nullptr, 0U);
    bool changed = false;
    for (UINT index = 0U; index < count; ++index) {
        const UINT length = DragQueryFileW(dropHandle, index, nullptr, 0U);
        if (length == 0U) continue;
        std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1U, L'\0');
        DragQueryFileW(dropHandle, index, buffer.data(), length + 1U);
        changed = addPathItem(buffer.data()) || changed;
    }
    DragFinish(dropHandle);
    if (changed) {
        saveItems();
        requestIcons();
        updateLayout();
    }
}

void DockWindow::openFilePicker() {
    std::vector<wchar_t> buffer(32768U, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"所有文件\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE && addPathItem(buffer.data())) {
        saveItems();
        requestIcons();
        updateLayout();
    }
}

void DockWindow::openIconPicker(const std::string& itemId) {
    domain::DockItem* item = nullptr;
    for (domain::DockItem& candidate : items_) {
        if (candidate.id == itemId) {
            item = &candidate;
            break;
        }
    }
    if (item == nullptr) return;

    std::vector<wchar_t> buffer(32768U, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"图标和程序\0*.ico;*.exe;*.dll\0图标文件\0*.ico\0程序文件\0*.exe;*.dll\0所有文件\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = L"选择自定义图标来源";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) return;

    item->customIconPath = buffer.data();
    renderer_.clearIcon(itemId);
    saveItems();
    requestIcons();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DockWindow::resetCustomIcon(const std::string& itemId) {
    for (domain::DockItem& item : items_) {
        if (item.id == itemId) {
            if (item.customIconPath.empty()) return;
            item.customIconPath.clear();
            renderer_.clearIcon(itemId);
            saveItems();
            requestIcons();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
}

int DockWindow::hitTest(float x, float y) const {
    for (std::size_t index = 0; index < layout_.items.size(); ++index) {
        const layout::LayoutItem& item = layout_.items[index];
        if (x >= item.left && x <= item.right && y >= item.top && y <= item.bottom) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void DockWindow::removeItem(const std::string& id) {
    const auto iterator = std::find_if(items_.begin(),
                                       items_.end(),
                                       [&id](const domain::DockItem& item) {
                                           return item.id == id;
                                       });
    if (iterator == items_.end()) return;
    items_.erase(iterator);
    renderer_.clearIcon(id);
    saveItems();
    requestIcons();
    updateLayout();
}

void DockWindow::reorderPressedItem(float pointerX) {
    if (pressedItemId_.empty()) return;
    const auto current = std::find_if(items_.begin(),
                                     items_.end(),
                                     [this](const domain::DockItem& item) {
                                         return item.id == pressedItemId_;
                                     });
    if (current == items_.end()) return;
    const std::size_t from = static_cast<std::size_t>(current - items_.begin());
    std::string targetItemId;
    bool insertAfterTarget = true;
    for (std::size_t index = 0; index < layout_.items.size(); ++index) {
        const layout::LayoutItem& visual = layout_.items[index];
        targetItemId = visual.id;
        if (pointerX < (visual.left + visual.right) * 0.5F) {
            insertAfterTarget = false;
            break;
        }
        insertAfterTarget = true;
    }
    if (targetItemId.empty() || targetItemId == pressedItemId_) return;
    const auto targetIterator = std::find_if(items_.begin(),
                                             items_.end(),
                                             [&targetItemId](const domain::DockItem& item) {
                                                 return item.id == targetItemId;
                                             });
    if (targetIterator == items_.end()) return;
    std::size_t target = static_cast<std::size_t>(targetIterator - items_.begin()) +
                         (insertAfterTarget ? 1U : 0U);
    if (target > from) --target;
    if (target == from || target > items_.size() - 1U) return;

    domain::DockItem moved = std::move(*current);
    items_.erase(current);
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(target), std::move(moved));
    for (std::size_t index = 0; index < items_.size(); ++index) {
        items_[index].order = static_cast<int>(index * 10U);
    }
    saveItems();
    requestIcons();
    updateLayout();
}

void DockWindow::showContextMenu(int screenX, int screenY) {
    menuOpen_ = true;
    autoHideController_.acquireVisibilityLock();
    POINT screenPoint{screenX, screenY};
    if (screenX < 0 || screenY < 0) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        screenPoint.x = (client.right - client.left) / 2;
        screenPoint.y = (client.bottom - client.top) / 2;
        ClientToScreen(hwnd_, &screenPoint);
    }
    POINT clientPoint = screenPoint;
    ScreenToClient(hwnd_, &clientPoint);
    const int hitIndex = hitTest(static_cast<float>(clientPoint.x),
                                 static_cast<float>(clientPoint.y));
    const std::string itemId = hitIndex >= 0
                                   ? layout_.items[static_cast<std::size_t>(hitIndex)].id
                                   : std::string();

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        menuOpen_ = false;
        releaseVisibilityLock();
        return;
    }
    if (!itemId.empty()) {
        AppendMenuW(menu, MF_STRING, kContextOpenCommand, L"打开");
        AppendMenuW(menu, MF_STRING, kContextChooseIconCommand, L"更换图标…");
        if (const domain::DockItem* item = findItemById(itemId);
            item != nullptr && !item->customIconPath.empty()) {
            AppendMenuW(menu, MF_STRING, kContextResetIconCommand, L"恢复默认图标");
        }
        const auto* selected = findItemById(itemId);
        if (selected && selected->transient) {
            AppendMenuW(menu, MF_STRING, kContextPinCommand, L"保留在程序坞");
        } else {
            AppendMenuW(menu, MF_STRING, kContextRemoveCommand, L"取消固定");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kContextAddApplicationCommand, L"添加应用…");
    AppendMenuW(menu, MF_STRING, kContextAddCommand, L"添加文件或文件夹…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kContextRecycleCommand, L"打开回收站");
    AppendMenuW(menu, MF_STRING, kContextEmptyRecycleCommand, L"清空回收站…");
    const UINT command = TrackPopupMenu(menu,
                                        TPM_RETURNCMD | TPM_NONOTIFY,
                                        screenPoint.x,
                                        screenPoint.y,
                                        0,
                                        hwnd_,
                                        nullptr);
    DestroyMenu(menu);
    menuOpen_ = false;
    if (command == kContextPinCommand) {
        for (auto& item : items_) if (item.id == itemId) item.transient = false;
        saveItems();
    } else if (command == kContextRecycleCommand) {
        std::wstring error;
        if (launcher_.launch(L"shell:RecycleBinFolder", L"", L"", &error))
            dismissAfterApplicationActivation();
    } else if (command == kContextEmptyRecycleCommand) {
        // 保留 Windows 自带的删除确认，不使用 SHERB_NOCONFIRMATION。
        SHEmptyRecycleBinW(hwnd_, nullptr, 0);
    } else if (command == kContextOpenCommand && !itemId.empty()) {
        const domain::DockItem* item = findItemById(itemId);
        if (item != nullptr) {
            activateOrOpen(*item);
        }
    } else if (command == kContextChooseIconCommand && !itemId.empty()) {
        openIconPicker(itemId);
    } else if (command == kContextResetIconCommand && !itemId.empty()) {
        resetCustomIcon(itemId);
    } else if (command == kContextRemoveCommand) {
        removeItem(itemId);
    } else if (command == kContextAddApplicationCommand) {
        openApplicationPicker();
    } else if (command == kContextAddCommand) {
        openFilePicker();
    }
    releaseVisibilityLock();
}

void DockWindow::handleTrayCallback(LPARAM lParam) {
    if (!trayController_.handleCallback(lParam)) return;
    const TrayCommand command = trayController_.showMenu(
        settings_.behavior.autoHide,
        settings_.behavior.launchAtStartup);
    handleTrayCommand(command);
}

void DockWindow::handleTrayCommand(TrayCommand command) {
    switch (command) {
    case TrayCommand::ShowDock:
        manuallyHidden_ = false;
        autoHideController_.forceShow();
        showDock();
        break;
    case TrayCommand::HideDock:
        outsideClickObserver_.stop();
        // 手动隐藏不启动边缘唤出，用户可通过托盘“显示 Dock”恢复。
        manuallyHidden_ = true;
        KillTimer(hwnd_, kRevealTimerId);
        KillTimer(hwnd_, kHideTimerId);
        if (edgeWindow_ != nullptr) {
            KillTimer(edgeWindow_, kShowTimerId);
            ShowWindow(edgeWindow_, SW_HIDE);
        }
        ShowWindow(hwnd_, SW_HIDE);
        ShowWindow(backdropWindow_, SW_HIDE);
        break;
    case TrayCommand::ToggleAutoHide:
        toggleAutoHide();
        break;
    case TrayCommand::ToggleStartup:
        toggleStartup();
        break;
    case TrayCommand::OpenSettings:
        openSettingsWindow();
        break;
    case TrayCommand::OpenConfigFolder:
        openConfigFolder();
        break;
    case TrayCommand::Exit:
        DestroyWindow(hwnd_);
        break;
    case TrayCommand::None:
        break;
    }
}

void DockWindow::toggleAutoHide() {
    settings_.behavior.autoHide = !settings_.behavior.autoHide;
    autoHideController_.setEnabled(settings_.behavior.autoHide);
    saveSettings();
    if (!settings_.behavior.autoHide) {
        showDock();
    }
}

void DockWindow::toggleStartup() {
    const bool enabled = !settings_.behavior.launchAtStartup;
    std::wstring errorMessage;
    if (!startupManager_.setEnabled(enabled, &errorMessage)) {
        MessageBoxW(hwnd_,
                    errorMessage.empty() ? L"无法修改开机启动设置。" : errorMessage.c_str(),
                    L"Planetary Guard",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    settings_.behavior.launchAtStartup = enabled;
    saveSettings();
}

void DockWindow::openConfigFolder() {
    const std::wstring folder = settingsStore_.filePath().parent_path().wstring();
    ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DockWindow::openSettingsWindow() {
    settingsWindow_.show(hwnd_,
                         settings_,
                         [this](const domain::AppSettings& settings) {
                             applySettings(settings);
                         });
}

void DockWindow::applySettings(const domain::AppSettings& settings) {
    const bool wasHidingInFullscreen = settings_.behavior.hideInFullscreen;
    const bool startupChanged = settings.behavior.launchAtStartup !=
                                settings_.behavior.launchAtStartup;
    if (startupChanged) {
        std::wstring errorMessage;
        if (!startupManager_.setEnabled(settings.behavior.launchAtStartup, &errorMessage)) {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"无法修改开机启动设置。" : errorMessage.c_str(),
                        L"Planetary Guard",
                        MB_OK | MB_ICONWARNING);
            return;
        }
    }

    settings_ = settings;
    applyBackdropEffect();
    const float maxScale = std::clamp(
        settings_.appearance.maxIconSizeDip / std::max(settings_.appearance.iconSizeDip, 1.0F),
        1.0F,
        2.0F);
    const float effectiveMaxScale = settings_.appearance.reduceMotion ? 1.0F : maxScale;
    layoutEngine_ = layout::LayoutEngine(settings_.appearance.iconSizeDip, 8.0F, effectiveMaxScale);
    renderer_.applyAppearance(settings_.appearance);
    applyWindowOpacity();
    autoHideController_.setEnabled(settings_.behavior.autoHide);
    if (wasHidingInFullscreen && !settings_.behavior.hideInFullscreen && hiddenForFullscreen_) {
        hiddenForFullscreen_ = false;
        autoHideController_.setFullscreen(false);
        if (wasVisibleBeforeFullscreen_ && !manuallyHidden_) {
            autoHideController_.forceShow();
            showDock();
        }
    }
    saveSettings();
    updateLayout();
    if (!settings_.behavior.autoHide) showDock();
}

void DockWindow::handleFullscreenChanged() {
    const bool fullscreen = fullscreenDetector_.isFullscreen();
    const bool shouldHideForFullscreen = settings_.behavior.hideInFullscreen && fullscreen;
    autoHideController_.setFullscreen(shouldHideForFullscreen);
    if (!settings_.behavior.hideInFullscreen) return;

    if (fullscreen && !hiddenForFullscreen_) {
        outsideClickObserver_.stop();
        wasVisibleBeforeFullscreen_ = IsWindowVisible(hwnd_) != FALSE;
        hiddenForFullscreen_ = true;
        ShowWindow(edgeWindow_, SW_HIDE);
        ShowWindow(hwnd_, SW_HIDE);
        ShowWindow(backdropWindow_, SW_HIDE);
        KillTimer(hwnd_, kHideTimerId);
        startRevealWatch();
    } else if (!fullscreen && hiddenForFullscreen_) {
        hiddenForFullscreen_ = false;
        if (wasVisibleBeforeFullscreen_ && !manuallyHidden_) {
            autoHideController_.forceShow();
            showDock();
        } else if (!manuallyHidden_) {
            // 全屏结束时原本已收起，也必须恢复唤出检测。
            startRevealWatch();
        }
    }
}

/** 显式环境开关启用的本地故障快照；默认不写文件，不记录窗口标题或输入内容。 */
void DockWindow::writeDiagnosticState() const {
    wchar_t path[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"PLANETARY_GUARD_DIAGNOSTICS", path, 32768);
    if (!length || length >= 32768) return;
    RECT bounds{};
    GetWindowRect(hwnd_, &bounds);
    POINT pointer{};
    const BOOL cursorAvailable = GetCursorPos(&pointer);
    const bool buttonPressed = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
        GetAsyncKeyState(VK_MBUTTON) | GetAsyncKeyState(VK_XBUTTON1) | GetAsyncKeyState(VK_XBUTTON2)) & 0x8000) != 0;
    std::ofstream output(std::filesystem::path(path), std::ios::trunc);
    output << "visible=" << !!IsWindowVisible(hwnd_) << " manual=" << manuallyHidden_
           << " fullscreenBlocked=" << hiddenForFullscreen_
           << " detectedFullscreen=" << fullscreenDetector_.isFullscreen()
           << " state=" << static_cast<int>(autoHideController_.state())
           << " locks=" << autoHideController_.visibilityLockCount()
           << " polls=" << revealPollCount_ << " cursorAvailable=" << !!cursorAvailable
           << " insideEdge=" << (cursorAvailable && PtInRect(&revealBounds_, pointer))
           << " buttonPressed=" << buttonPressed << " delay=" << settings_.behavior.showDelayMs
           << " dock=" << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom
           << " edge=" << revealBounds_.left << ',' << revealBounds_.top << ',' << revealBounds_.right << ',' << revealBounds_.bottom;
}

const domain::DockItem* DockWindow::findItemById(const std::string& id) const {
    const auto iterator = std::find_if(items_.begin(),
                                       items_.end(),
                                       [&id](const domain::DockItem& item) {
                                           return item.id == id;
                                       });
    return iterator == items_.end() ? nullptr : &(*iterator);
}

void DockWindow::launchItemAt(float x, float y) {
    for (const layout::LayoutItem& layoutItem : layout_.items) {
        if (x < layoutItem.left || x > layoutItem.right ||
            y < layoutItem.top || y > layoutItem.bottom) {
            continue;
        }
        const domain::DockItem* item = findItemById(layoutItem.id);
        if (item == nullptr) {
            return;
        }
        activateOrOpen(*item);
        return;
    }
}

void DockWindow::showFolderContents(const domain::DockItem& item) {
    // 网络目录交由资源管理器处理，避免在 UI 线程等待网络枚举。
    if (item.targetPath.starts_with(L"\\\\")) {
        std::wstring error;
        if (launcher_.launch(item.targetPath, L"", L"", &error))
            dismissAfterApplicationActivation();
        return;
    }
    std::vector<std::filesystem::path> entries;
    std::error_code error;
    std::filesystem::directory_iterator iterator(item.targetPath,
        std::filesystem::directory_options::skip_permission_denied, error);
    for (; !error && iterator != std::filesystem::directory_iterator() && entries.size() < 40;
         iterator.increment(error)) entries.push_back(iterator->path());
    std::sort(entries.begin(), entries.end());
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"在资源管理器中打开");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto label = entries[i].filename().wstring();
        // 菜单中的 & 是助记符，需要转义文件名。
        for (std::size_t pos = 0; (pos = label.find(L'&', pos)) != std::wstring::npos; pos += 2) label.insert(pos, 1, L'&');
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(i + 10), label.c_str());
    }
    if (entries.empty()) AppendMenuW(menu, MF_GRAYED, 0, error ? L"无法读取目录" : L"文件夹为空");
    if (entries.size() == 40) AppendMenuW(menu, MF_GRAYED, 0, L"更多内容请在资源管理器查看");
    POINT point{};
    GetCursorPos(&point);
    menuOpen_ = true;
    autoHideController_.acquireVisibilityLock();
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                        point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    menuOpen_ = false;
    releaseVisibilityLock();
    std::wstring launchError;
    bool launched = false;
    if (command == 1) launched = launcher_.launch(item.targetPath, L"", L"", &launchError);
    else if (command >= 10 && command - 10 < entries.size())
        launched = launcher_.launch(entries[command - 10].wstring(), L"", L"", &launchError);
    if (launched) dismissAfterApplicationActivation();
    if (!launchError.empty()) MessageBoxW(hwnd_, launchError.c_str(), L"打开失败", MB_OK | MB_ICONWARNING);
}

void DockWindow::activateOrOpen(const domain::DockItem& source) {
    const domain::DockItem item = source;
    if (item.type == domain::DockItemType::Folder) {
        showFolderContents(item);
        return;
    }
    const auto identity = applicationIdentity(item.targetPath);
    std::vector<ApplicationWindow> matching;
    if (!identity.empty() && item.arguments.empty()) {
        for (const auto& window : enumerateApplicationWindows())
            if (window.executable == identity) matching.push_back(window);
    }
    if (matching.size() == 1 && activateApplicationWindow(matching.front())) {
        dismissAfterApplicationActivation();
        return;
    }
    if (matching.size() > 1) {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        for (std::size_t i = 0; i < matching.size(); ++i)
            AppendMenuW(menu, MF_STRING, i + 1, matching[i].title.c_str());
        POINT point{};
        GetCursorPos(&point);
        menuOpen_ = true;
        autoHideController_.acquireVisibilityLock();
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            point.x, point.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        menuOpen_ = false;
        releaseVisibilityLock();
        if (selected > 0 && selected <= matching.size() && activateApplicationWindow(matching[selected - 1]))
            dismissAfterApplicationActivation();
        return;
    }
        std::wstring errorMessage;
        if (!launcher_.launch(item.targetPath,
                              item.arguments,
                              item.workingDirectory,
                              &errorMessage)) {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"无法启动此项目。" : errorMessage.c_str(),
                        L"Planetary Guard",
                        MB_OK | MB_ICONWARNING);
        } else {
            dismissAfterApplicationActivation();
        }
}

LRESULT CALLBACK DockWindow::windowProc(HWND hwnd,
                                        UINT message,
                                        WPARAM wParam,
                                        LPARAM lParam) {
    DockWindow* window = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<DockWindow*>(createStruct->lpCreateParams);
        // CreateWindowEx 尚未返回，成员句柄此时仍为空。必须先记录真实
        // HWND，否则 WM_NCCREATE 会把 nullptr 传给 DefWindowProc 并导致
        // 整个窗口创建失败。
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window == nullptr ? DefWindowProcW(hwnd, message, wParam, lParam)
                             : window->handleMessage(message, wParam, lParam);
}

LRESULT CALLBACK DockWindow::edgeWindowProc(HWND hwnd,
                                            UINT message,
                                            WPARAM wParam,
                                            LPARAM lParam) {
    DockWindow* window = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<DockWindow*>(createStruct->lpCreateParams);
        window->edgeWindow_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window == nullptr ? DefWindowProcW(hwnd, message, wParam, lParam)
                             : window->handleEdgeMessage(message, wParam, lParam);
}

LRESULT DockWindow::handleEdgeMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEMOVE: {
        if (!trackingEdgeMouse_) {
            TRACKMOUSEEVENT trackingEvent{};
            trackingEvent.cbSize = sizeof(trackingEvent);
            trackingEvent.dwFlags = TME_LEAVE;
            trackingEvent.hwndTrack = edgeWindow_;
            trackingEdgeMouse_ = TrackMouseEvent(&trackingEvent) != FALSE;
            SetTimer(edgeWindow_,
                     kShowTimerId,
                     std::max(300U, settings_.behavior.showDelayMs),
                     nullptr);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingEdgeMouse_ = false;
        KillTimer(edgeWindow_, kShowTimerId);
        return 0;
    case WM_TIMER:
        if (wParam == kShowTimerId) {
            KillTimer(edgeWindow_, kShowTimerId);
            showDockFromEdge();
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return DefWindowProcW(edgeWindow_, message, wParam, lParam);
    }
    return DefWindowProcW(edgeWindow_, message, wParam, lParam);
}

LRESULT DockWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == OutsideClickObserver::kPressedMessage) {
        // 世代号屏蔽上次显示期间排队的点击，防止下一次唤出后立即又被收起。
        if (outsideClickObserver_.isCurrentNotification(wParam) && IsWindowVisible(hwnd_) &&
            !menuOpen_ && autoHideController_.visibilityLockCount() == 0U) {
            dismissAfterApplicationActivation();
        }
        return 0;
    }
    if (message == TrayController::kCallbackMessage) {
        handleTrayCallback(lParam);
        return 0;
    }
    if (message == FullscreenDetector::kChangedMessage) {
        handleForegroundChanged(reinterpret_cast<HWND>(wParam));
        handleFullscreenChanged();
        return 0;
    }
    if (message == IconLoader::kResultMessage) {
        handleIconResults();
        return 0;
    }
    if (taskbarCreatedMessage_ != 0U && message == taskbarCreatedMessage_) {
        trayController_.initialize(hwnd_);
        positionWindow();
        positionEdgeWindow();
        return 0;
    }

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd_, &paint);
        renderer_.render(layout_, items_);
        EndPaint(hwnd_, &paint);
        if (renderer_.consumeDeviceReset()) {
            iconLoader_.invalidateRequests();
            requestIcons();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SIZE:
        renderer_.resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    {
        renderer_.applyAppearance(settings_.appearance);
        const std::wstring previousMonitorId = settings_.placement.monitorId;
        updateLayout();
        positionEdgeWindow();
        handleFullscreenChanged();
        if (settings_.placement.monitorId != previousMonitorId) {
            settingsStore_.save(settings_, nullptr);
        }
        return 0;
    }
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            const std::wstring previousMonitorId = settings_.placement.monitorId;
            updateLayout();
            positionEdgeWindow();
            if (settings_.placement.monitorId != previousMonitorId) {
                settingsStore_.save(settings_, nullptr);
            }
        }
        return TRUE;
    case WM_LBUTTONDOWN: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        const int hitIndex = hitTest(x, y);
        if (hitIndex >= 0) {
            pressedItemId_ = layout_.items[static_cast<std::size_t>(hitIndex)].id;
            dragStartPoint_.x = GET_X_LPARAM(lParam);
            dragStartPoint_.y = GET_Y_LPARAM(lParam);
            dragging_ = false;
            autoHideController_.acquireVisibilityLock();
            SetCapture(hwnd_);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        // 隐藏后仍可能收到已排队的移动消息；不能因此进入 Showing 或重启动画。
        if (!IsWindowVisible(hwnd_)) return 0;
        if (tooltipWindow_ != nullptr && !dragging_) {
            const int hovered = hitTest(static_cast<float>(GET_X_LPARAM(lParam)),
                                        static_cast<float>(GET_Y_LPARAM(lParam)));
            TOOLINFOW tool{sizeof(TOOLINFOW)};
            tool.hwnd = hwnd_;
            tool.uId = 1;
            if (hovered >= 0) {
                const auto& slot = layout_.items[static_cast<std::size_t>(hovered)];
                const auto* item = findItemById(slot.id);
                const std::wstring nextText = item == nullptr ? L"" : item->displayName;
                if (tooltipText_ != nextText) {
                    tooltipText_ = nextText;
                    tool.lpszText = tooltipText_.data();
                    SendMessageW(tooltipWindow_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
                }
                POINT position{static_cast<LONG>(slot.centerX), static_cast<LONG>(slot.top - 32.0F * dpiScale_)};
                ClientToScreen(hwnd_, &position);
                SendMessageW(tooltipWindow_, TTM_TRACKPOSITION, 0, MAKELPARAM(position.x, position.y));
            }
            SendMessageW(tooltipWindow_, TTM_TRACKACTIVATE, hovered >= 0, reinterpret_cast<LPARAM>(&tool));
        }
        pointerX_ = static_cast<float>(GET_X_LPARAM(lParam));
        // 固定画布坐标不随放大变化。
        autoHideController_.onMouseEnter();
        beginMouseTracking();
        if (!pressedItemId_.empty()) {
            const int deltaX = GET_X_LPARAM(lParam) - dragStartPoint_.x;
            const int deltaY = GET_Y_LPARAM(lParam) - dragStartPoint_.y;
            if (!dragging_ && (std::abs(deltaX) > 8 || std::abs(deltaY) > 8)) {
                dragging_ = true;
            }
            if (dragging_) reorderPressedItem(static_cast<float>(GET_X_LPARAM(lParam)));
        }
        updateLayout(false);
        return 0;
    case WM_MOUSELEAVE:
        if (tooltipWindow_ != nullptr) {
            TOOLINFOW tool{sizeof(TOOLINFOW)};
            tool.hwnd = hwnd_;
            tool.uId = 1;
            SendMessageW(tooltipWindow_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
        }
        trackingMouse_ = false;
        pointerX_ = -1.0F;
        autoHideController_.onMouseLeave();
        scheduleHide();
        updateLayout(false);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (!pressedItemId_.empty() && !dragging_) {
            launchItemAt(static_cast<float>(GET_X_LPARAM(lParam)),
                         static_cast<float>(GET_Y_LPARAM(lParam)));
        }
        pressedItemId_.clear();
        dragging_ = false;
        releaseVisibilityLock();
        return 0;
    case WM_TIMER:
        if (wParam == kRevealTimerId) {
            pollRevealPointer();
            return 0;
        }
        if (wParam == kAnimationTimerId) {
            advanceAnimation();
            return 0;
        }
        if (wParam == kRunningStateTimerId) {
            // 最大化/F11/任务栏设置可在不切换前台窗口时发生，低频复核避免卡在全屏状态。
            handleFullscreenChanged();
            refreshRunningState();
            writeDiagnosticState();
            return 0;
        }
        if (wParam == kHideTimerId) {
            KillTimer(hwnd_, kHideTimerId);
            autoHideController_.onHideTimer();
            if (autoHideController_.wantsHide()) {
                hideDock();
            }
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        showContextMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_DROPFILES:
        addItemsFromDrop(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_CANCELMODE:
        if (GetCapture() == hwnd_) ReleaseCapture();
        pressedItemId_.clear();
        dragging_ = false;
        releaseVisibilityLock();
        return 0;
    case WM_DPICHANGED:
        dpiScale_ = static_cast<float>(HIWORD(wParam)) / 96.0F;
        updateLayout();
        positionEdgeWindow();
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        outsideClickObserver_.stop();
        KillTimer(hwnd_, kRevealTimerId);
        if (backdropWindow_ != nullptr) { DestroyWindow(backdropWindow_); backdropWindow_ = nullptr; }
        KillTimer(hwnd_, kAnimationTimerId);
        KillTimer(hwnd_, kHideTimerId);
        KillTimer(hwnd_, kRunningStateTimerId);
        iconLoader_.stop();
        fullscreenDetector_.shutdown();
        DragAcceptFiles(hwnd_, FALSE);
        trayController_.remove();
        if (edgeWindow_ != nullptr && IsWindow(edgeWindow_)) {
            KillTimer(edgeWindow_, kShowTimerId);
            DestroyWindow(edgeWindow_);
            edgeWindow_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace planetary::platform
