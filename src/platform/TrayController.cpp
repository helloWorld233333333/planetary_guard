#include "platform/TrayController.h"

namespace planetary::platform {

namespace {

constexpr UINT kShowCommand = 42001U;
constexpr UINT kHideCommand = 42002U;
constexpr UINT kAutoHideCommand = 42003U;
constexpr UINT kStartupCommand = 42004U;
constexpr UINT kSettingsCommand = 42005U;
constexpr UINT kConfigCommand = 42006U;
constexpr UINT kExitCommand = 42007U;

} // namespace

TrayController::~TrayController() {
    remove();
}

bool TrayController::initialize(HWND owner) {
    remove();
    owner_ = owner;
    if (owner_ == nullptr) return false;

    iconData_ = {};
    iconData_.cbSize = sizeof(iconData_);
    iconData_.hWnd = owner_;
    iconData_.uID = 1U;
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
    iconData_.uCallbackMessage = kCallbackMessage;
    iconData_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    iconData_.guidItem = GUID{0x3b6f1d5a,
                              0x5b57,
                              0x4b76,
                              {0x9a, 0x2e, 0x22, 0x8a, 0x41, 0x6c, 0x72, 0x01}};
    constexpr wchar_t kTooltip[] = L"Planetary Guard Dock";
    wcsncpy_s(iconData_.szTip, kTooltip, _TRUNCATE);
    return addIcon();
}

bool TrayController::addIcon() {
    if (owner_ == nullptr || iconData_.hIcon == nullptr) return false;
    if (Shell_NotifyIconW(NIM_ADD, &iconData_) == FALSE) return false;
    iconData_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &iconData_);
    installed_ = true;
    return true;
}

void TrayController::remove() {
    if (installed_) {
        Shell_NotifyIconW(NIM_DELETE, &iconData_);
    }
    installed_ = false;
    owner_ = nullptr;
    iconData_ = {};
}

bool TrayController::handleCallback(LPARAM lParam) const {
    return lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP ||
           lParam == WM_LBUTTONDBLCLK;
}

TrayCommand TrayController::showMenu(bool autoHideEnabled, bool startupEnabled) const {
    if (!installed_ || owner_ == nullptr) return TrayCommand::None;

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return TrayCommand::None;
    AppendMenuW(menu, MF_STRING, kShowCommand, L"显示 Dock");
    AppendMenuW(menu, MF_STRING, kHideCommand, L"隐藏 Dock");
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kAutoHideCommand, L"自动隐藏");
    AppendMenuW(menu, MF_STRING, kStartupCommand, L"开机启动");
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"设置…");
    AppendMenuW(menu, MF_STRING, kConfigCommand, L"打开配置目录");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"退出");
    CheckMenuItem(menu,
                  kAutoHideCommand,
                  MF_BYCOMMAND | (autoHideEnabled ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu,
                  kStartupCommand,
                  MF_BYCOMMAND | (startupEnabled ? MF_CHECKED : MF_UNCHECKED));

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    const UINT command = TrackPopupMenu(menu,
                                        TPM_RETURNCMD | TPM_NONOTIFY,
                                        cursor.x,
                                        cursor.y,
                                        0,
                                        owner_,
                                        nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command) {
    case kShowCommand: return TrayCommand::ShowDock;
    case kHideCommand: return TrayCommand::HideDock;
    case kAutoHideCommand: return TrayCommand::ToggleAutoHide;
    case kStartupCommand: return TrayCommand::ToggleStartup;
    case kSettingsCommand: return TrayCommand::OpenSettings;
    case kConfigCommand: return TrayCommand::OpenConfigFolder;
    case kExitCommand: return TrayCommand::Exit;
    default: return TrayCommand::None;
    }
}

} // namespace planetary::platform
