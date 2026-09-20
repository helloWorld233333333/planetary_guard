#include "platform/SettingsWindow.h"

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace planetary::platform {

namespace {

constexpr wchar_t kClassName[] = L"PlanetaryGuardSettingsWindow";
constexpr wchar_t kTitle[] = L"Planetary Guard 设置";
constexpr int kAutoHideCheck = 51001;
constexpr int kFullscreenCheck = 51002;
constexpr int kStartupCheck = 51003;
constexpr int kReduceMotionCheck = 51004;
constexpr int kMonitorCombo = 51005;
constexpr int kThemeCombo = 51006;
constexpr int kIconSizeEdit = 51007;
constexpr int kMaxIconSizeEdit = 51008;
constexpr int kOpacityEdit = 51009;
constexpr int kApplyButton = 51010;
constexpr int kCancelButton = 51011;
constexpr int kUnifiedIconTilesCheck = 51012;
constexpr int kDesktopCheck = 51013;

HWND createStatic(HWND parent, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0,
                                   L"STATIC",
                                   text,
                                   WS_CHILD | WS_VISIBLE,
                                   x,
                                   y,
                                   width,
                                   height,
                                   parent,
                                   nullptr,
                                   GetModuleHandleW(nullptr),
                                   nullptr);
    if (control != nullptr) {
        SendMessageW(control,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
    }
    return control;
}

void setFont(HWND control) {
    if (control != nullptr) {
        SendMessageW(control,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
    }
}

HMENU controlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

} // namespace

SettingsWindow::~SettingsWindow() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
}

bool SettingsWindow::registerClass() {
    if (classRegistered_) return true;
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &SettingsWindow::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    classRegistered_ = true;
    return true;
}

bool SettingsWindow::show(HWND owner,
                          const domain::AppSettings& settings,
                          ApplyCallback callback) {
    if (!registerClass()) return false;
    owner_ = owner;
    settings_ = settings;
    applyCallback_ = std::move(callback);
    if (hwnd_ == nullptr) {
        const UINT dpi = owner_ == nullptr ? 96U : GetDpiForWindow(owner_);
        RECT windowRect{0, 0, MulDiv(500, static_cast<int>(dpi), 96),
                        MulDiv(484, static_cast<int>(dpi), 96)};
        AdjustWindowRectExForDpi(&windowRect,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 FALSE,
                                 WS_EX_DLGMODALFRAME,
                                 dpi);
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME,
                                kClassName,
                                kTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                windowRect.right - windowRect.left,
                                windowRect.bottom - windowRect.top,
                                owner_,
                                nullptr,
                                instance_,
                                this);
        if (hwnd_ == nullptr) return false;
        createControls();
        scaleControlsForDpi(GetDpiForWindow(hwnd_));
    }
    updateControls();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void SettingsWindow::scaleControlsForDpi(UINT newDpi) {
    if (hwnd_ == nullptr || newDpi == 0U || newDpi == controlsDpi_) return;
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{};
        if (GetWindowRect(child, &rect) == FALSE) continue;
        POINT points[2]{{rect.left, rect.top}, {rect.right, rect.bottom}};
        MapWindowPoints(nullptr, hwnd_, points, 2U);
        const int x = MulDiv(points[0].x, static_cast<int>(newDpi), static_cast<int>(controlsDpi_));
        const int y = MulDiv(points[0].y, static_cast<int>(newDpi), static_cast<int>(controlsDpi_));
        const int width = MulDiv(points[1].x - points[0].x,
                                 static_cast<int>(newDpi),
                                 static_cast<int>(controlsDpi_));
        const int height = MulDiv(points[1].y - points[0].y,
                                  static_cast<int>(newDpi),
                                  static_cast<int>(controlsDpi_));
        SetWindowPos(child, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    }
    controlsDpi_ = newDpi;
}

void SettingsWindow::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

void SettingsWindow::createControls() {
    createStatic(hwnd_, L"Dock 行为", 20, 16, 320, 22);
    autoHideCheck_ = CreateWindowExW(0,
                                     L"BUTTON",
                                     L"自动隐藏 Dock",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     28,
                                     45,
                                     300,
                                     24,
                                     hwnd_,
                                     controlId(kAutoHideCheck),
                                     instance_,
                                     nullptr);
    fullscreenCheck_ = CreateWindowExW(0,
                                       L"BUTTON",
                                       L"全屏游戏/视频时隐藏",
                                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       28,
                                       73,
                                       300,
                                       24,
                                       hwnd_,
                                       controlId(kFullscreenCheck),
                                       instance_,
                                       nullptr);
    startupCheck_ = CreateWindowExW(0,
                                     L"BUTTON",
                                     L"登录 Windows 时启动",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     28,
                                     101,
                                     300,
                                     24,
                                     hwnd_,
                                     controlId(kStartupCheck),
                                     instance_,
                                     nullptr);

    reduceMotionCheck_ = CreateWindowExW(0,
                                         L"BUTTON",
                                         L"减少动画和图标放大",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         28,
                                         129,
                                         300,
                                         24,
                                         hwnd_,
                                         controlId(kReduceMotionCheck),
                                         instance_,
                                         nullptr);

    unifiedIconTilesCheck_ = CreateWindowExW(0,
                                              L"BUTTON",
                                              L"为图标添加彩色底框（可选）",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              28,
                                              157,
                                              330,
                                              24,
                                              hwnd_,
                                              controlId(kUnifiedIconTilesCheck),
                                              instance_,
                                              nullptr);

    createStatic(hwnd_, L"目标显示器", 28, 194, 150, 22);
    monitorCombo_ = CreateWindowExW(0,
                                    L"COMBOBOX",
                                    nullptr,
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    190,
                                    191,
                                    270,
                                    180,
                                    hwnd_,
                                    controlId(kMonitorCombo),
                                    instance_,
                                    nullptr);

    createStatic(hwnd_, L"主题", 28, 230, 150, 22);
    themeCombo_ = CreateWindowExW(0,
                                  L"COMBOBOX",
                                  nullptr,
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  190,
                                  227,
                                  180,
                                  120,
                                  hwnd_,
                                  controlId(kThemeCombo),
                                  instance_,
                                  nullptr);
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随系统"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高对比度"));

    createStatic(hwnd_, L"图标大小（24-96）", 28, 266, 150, 22);
    iconSizeEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                    L"EDIT",
                                    nullptr,
                                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                                    190,
                                    263,
                                    70,
                                    24,
                                    hwnd_,
                                    controlId(kIconSizeEdit),
                                    instance_,
                                    nullptr);
    createStatic(hwnd_, L"最大放大尺寸（24-96）", 28, 302, 150, 22);
    maxIconSizeEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                       L"EDIT",
                                       nullptr,
                                       WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                                       190,
                                       299,
                                       70,
                                       24,
                                       hwnd_,
                                       controlId(kMaxIconSizeEdit),
                                       instance_,
                                       nullptr);

    createStatic(hwnd_, L"Dock 不透明度（40-100%）", 28, 338, 170, 22);
    opacityEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE,
                                   L"EDIT",
                                   nullptr,
                                   WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                                   190,
                                   335,
                                   70,
                                   24,
                                   hwnd_,
                                   controlId(kOpacityEdit),
                                   instance_,
                                   nullptr);

    HWND applyButton = CreateWindowExW(0,
                                       L"BUTTON",
                                       L"保存",
                                       WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       300,
                                       382,
                                       75,
                                       28,
                                       hwnd_,
                                       controlId(kApplyButton),
                                       instance_,
                                       nullptr);
    HWND cancelButton = CreateWindowExW(0,
                                        L"BUTTON",
                                        L"取消",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        385,
                                        382,
                                        75,
                                        28,
                                        hwnd_,
                                        controlId(kCancelButton),
                                        instance_,
                                        nullptr);

    setFont(autoHideCheck_);
    // 将外观与位置区域整体下移，为新增的行为选项留一行。
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&bounds), 2);
        if (bounds.top >= 185)
            SetWindowPos(child, nullptr, bounds.left, bounds.top + 32, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    desktopCheck_ = CreateWindowExW(0, L"BUTTON", L"显示桌面时强制显示程序坞",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        28, 185, 400, 24, hwnd_, controlId(kDesktopCheck), instance_, nullptr);
    setFont(desktopCheck_);
    setFont(fullscreenCheck_);
    setFont(startupCheck_);
    setFont(reduceMotionCheck_);
    setFont(unifiedIconTilesCheck_);
    setFont(monitorCombo_);
    setFont(themeCombo_);
    setFont(iconSizeEdit_);
    setFont(maxIconSizeEdit_);
    setFont(opacityEdit_);
    setFont(applyButton);
    setFont(cancelButton);
}

BOOL CALLBACK SettingsWindow::monitorEnumProc(HMONITOR monitor,
                                              HDC /*deviceContext*/,
                                              LPRECT /*monitorRect*/,
                                              LPARAM data) {
    auto* window = reinterpret_cast<SettingsWindow*>(data);
    if (window == nullptr || window->monitorCombo_ == nullptr) return FALSE;

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) return TRUE;

    const std::wstring id = info.szDevice;
    if (id.empty()) return TRUE;
    window->monitorIds_.push_back(id);
    const std::wstring label = L"显示器 " +
                               std::to_wstring(window->monitorIds_.size()) +
                               L"（" + id + L"）";
    SendMessageW(window->monitorCombo_,
                 CB_ADDSTRING,
                 0,
                 reinterpret_cast<LPARAM>(label.c_str()));
    return TRUE;
}

void SettingsWindow::refreshMonitorOptions() {
    if (monitorCombo_ == nullptr) return;
    monitorIds_.clear();
    SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorCombo_,
                 CB_ADDSTRING,
                 0,
                 reinterpret_cast<LPARAM>(L"自动选择（启动后记忆）"));
    EnumDisplayMonitors(nullptr,
                        nullptr,
                        &SettingsWindow::monitorEnumProc,
                        reinterpret_cast<LPARAM>(this));

    int selection = 0;
    if (!settings_.placement.monitorId.empty()) {
        const auto iterator = std::find(monitorIds_.begin(),
                                        monitorIds_.end(),
                                        settings_.placement.monitorId);
        if (iterator != monitorIds_.end()) {
            selection = static_cast<int>(std::distance(monitorIds_.begin(), iterator)) + 1;
        } else {
            monitorIds_.push_back(settings_.placement.monitorId);
            const std::wstring label = L"不可用（" + settings_.placement.monitorId + L"）";
            SendMessageW(monitorCombo_,
                         CB_ADDSTRING,
                         0,
                         reinterpret_cast<LPARAM>(label.c_str()));
            selection = static_cast<int>(monitorIds_.size());
        }
    }
    SendMessageW(monitorCombo_, CB_SETCURSEL, selection, 0);
}

void SettingsWindow::updateControls() {
    if (autoHideCheck_ == nullptr) return;
    SendMessageW(desktopCheck_, BM_SETCHECK,
                 settings_.behavior.showOnDesktop ? BST_CHECKED : BST_UNCHECKED, 0);
    refreshMonitorOptions();
    SendMessageW(autoHideCheck_, BM_SETCHECK,
                 settings_.behavior.autoHide ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fullscreenCheck_, BM_SETCHECK,
                 settings_.behavior.hideInFullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(startupCheck_, BM_SETCHECK,
                 settings_.behavior.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(reduceMotionCheck_, BM_SETCHECK,
                 settings_.appearance.reduceMotion ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(unifiedIconTilesCheck_, BM_SETCHECK,
                 settings_.appearance.unifiedIconTiles ? BST_CHECKED : BST_UNCHECKED, 0);
    int themeIndex = 0;
    switch (settings_.appearance.themeMode) {
    case domain::ThemeMode::Light: themeIndex = 1; break;
    case domain::ThemeMode::Dark: themeIndex = 2; break;
    case domain::ThemeMode::HighContrast: themeIndex = 3; break;
    case domain::ThemeMode::FollowSystem: themeIndex = 0; break;
    }
    SendMessageW(themeCombo_, CB_SETCURSEL, themeIndex, 0);
    SetWindowTextW(iconSizeEdit_, std::to_wstring(static_cast<int>(settings_.appearance.iconSizeDip)).c_str());
    SetWindowTextW(maxIconSizeEdit_, std::to_wstring(static_cast<int>(settings_.appearance.maxIconSizeDip)).c_str());
    SetWindowTextW(opacityEdit_,
                   std::to_wstring(static_cast<int>(settings_.appearance.dockOpacity * 100.0F)).c_str());
}

void SettingsWindow::apply() {
    domain::AppSettings updated = settings_;
    updated.behavior.autoHide = SendMessageW(autoHideCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    updated.behavior.showOnDesktop = SendMessageW(desktopCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    updated.behavior.hideInFullscreen = SendMessageW(fullscreenCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    updated.behavior.launchAtStartup = SendMessageW(startupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    updated.appearance.reduceMotion = SendMessageW(reduceMotionCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    updated.appearance.unifiedIconTiles =
        SendMessageW(unifiedIconTilesCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    const LRESULT monitorSelection = SendMessageW(monitorCombo_, CB_GETCURSEL, 0, 0);
    if (monitorSelection <= 0 || static_cast<std::size_t>(monitorSelection - 1) >= monitorIds_.size()) {
        updated.placement.monitorId.clear();
    } else {
        updated.placement.monitorId = monitorIds_[static_cast<std::size_t>(monitorSelection - 1)];
    }

    const LRESULT themeSelection = SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0);
    switch (themeSelection) {
    case 1: updated.appearance.themeMode = domain::ThemeMode::Light; break;
    case 2: updated.appearance.themeMode = domain::ThemeMode::Dark; break;
    case 3: updated.appearance.themeMode = domain::ThemeMode::HighContrast; break;
    default: updated.appearance.themeMode = domain::ThemeMode::FollowSystem; break;
    }

    wchar_t buffer[64]{};
    GetWindowTextW(iconSizeEdit_, buffer, static_cast<int>(std::size(buffer)));
    const int iconSize = _wtoi(buffer);
    GetWindowTextW(maxIconSizeEdit_, buffer, static_cast<int>(std::size(buffer)));
    const int maxIconSize = _wtoi(buffer);
    updated.appearance.iconSizeDip = static_cast<float>(std::clamp(iconSize, 24, 96));
    updated.appearance.maxIconSizeDip = static_cast<float>(std::clamp(maxIconSize, 24, 96));
    updated.appearance.maxIconSizeDip = std::max(updated.appearance.maxIconSizeDip,
                                                 updated.appearance.iconSizeDip);
    GetWindowTextW(opacityEdit_, buffer, static_cast<int>(std::size(buffer)));
    const int opacityPercent = _wtoi(buffer);
    updated.appearance.dockOpacity = static_cast<float>(std::clamp(opacityPercent, 40, 100)) / 100.0F;
    settings_ = updated;
    if (applyCallback_) applyCallback_(settings_);
    hide();
}

void SettingsWindow::cancel() {
    hide();
}

void SettingsWindow::handleCommand(WORD commandId) {
    if (commandId == kApplyButton) {
        apply();
    } else if (commandId == kCancelButton) {
        cancel();
    }
}

LRESULT CALLBACK SettingsWindow::windowProc(HWND hwnd,
                                            UINT message,
                                            WPARAM wParam,
                                            LPARAM lParam) {
    SettingsWindow* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<SettingsWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    if (window == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND:
        window->handleCommand(LOWORD(wParam));
        return 0;
    case WM_CLOSE:
        window->cancel();
        return 0;
    case WM_DPICHANGED: {
        const UINT dpi = HIWORD(wParam);
        window->scaleControlsForDpi(dpi);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd,
                     nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_DESTROY:
        window->hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace planetary::platform
