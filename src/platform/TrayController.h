#pragma once

#include <windows.h>
#include <shellapi.h>

namespace planetary::platform {

/** 托盘菜单动作。 */
enum class TrayCommand {
    None,
    ShowDock,
    HideDock,
    ToggleAutoHide,
    ToggleStartup,
    OpenSettings,
    OpenConfigFolder,
    Exit
};

/** 只负责 Shell_NotifyIcon 生命周期和标准托盘菜单。 */
class TrayController {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 41U;

    TrayController() = default;
    ~TrayController();

    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;

    bool initialize(HWND owner);
    void remove();
    bool handleCallback(LPARAM lParam) const;
    TrayCommand showMenu(bool autoHideEnabled, bool startupEnabled) const;

private:
    bool addIcon();

    HWND owner_ = nullptr;
    NOTIFYICONDATAW iconData_{};
    bool installed_ = false;
};

} // namespace planetary::platform
