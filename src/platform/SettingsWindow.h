#pragma once

#include "domain/AppSettings.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace planetary::platform {

/** 轻量标准 Win32 设置窗口，避免引入第二套 UI 框架。 */
class SettingsWindow {
public:
    using ApplyCallback = std::function<void(const domain::AppSettings&)>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool show(HWND owner, const domain::AppSettings& settings, ApplyCallback callback);
    void hide();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK monitorEnumProc(HMONITOR monitor,
                                         HDC deviceContext,
                                         LPRECT monitorRect,
                                         LPARAM data);
    bool registerClass();
    void createControls();
    void refreshMonitorOptions();
    void updateControls();
    void apply();
    void cancel();
    void handleCommand(WORD commandId);
    void scaleControlsForDpi(UINT newDpi);

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND autoHideCheck_ = nullptr;
    HWND desktopCheck_ = nullptr;
    HWND fullscreenCheck_ = nullptr;
    HWND startupCheck_ = nullptr;
    HWND reduceMotionCheck_ = nullptr;
    HWND unifiedIconTilesCheck_ = nullptr;
    HWND monitorCombo_ = nullptr;
    HWND themeCombo_ = nullptr;
    HWND iconSizeEdit_ = nullptr;
    HWND maxIconSizeEdit_ = nullptr;
    HWND opacityEdit_ = nullptr;
    std::vector<std::wstring> monitorIds_;
    domain::AppSettings settings_;
    ApplyCallback applyCallback_;
    bool classRegistered_ = false;
    UINT controlsDpi_ = 96U;
};

} // namespace planetary::platform
