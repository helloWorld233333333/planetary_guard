#pragma once

#include "config/DockItemStore.h"
#include "config/SettingsStore.h"
#include "dock/AutoHideController.h"
#include "dock/HoverRevealController.h"
#include "domain/AppSettings.h"
#include "domain/DockItem.h"
#include "layout/LayoutEngine.h"
#include "platform/DockRenderer.h"
#include "platform/FullscreenDetector.h"
#include "platform/IconLoader.h"
#include "platform/ShellLauncher.h"
#include "platform/StartupManager.h"
#include "platform/SettingsWindow.h"
#include "platform/TrayController.h"

#include <shellapi.h>
#include <windows.h>

#include <string>
#include <vector>

namespace planetary::platform {

/**
 * 轻量 Win32 Dock 窗口。
 *
 * 采用 WS_POPUP + TOOLWINDOW，不创建任务栏按钮，也不抢占焦点；窗口
 * 生命周期和输入事件集中在此处，布局与绘制保持独立。
 */
class DockWindow {
public:
    explicit DockWindow(HINSTANCE instance, bool inspectWindow = false);
    ~DockWindow();

    bool create();
    void show();
    HWND handle() const;
    const std::wstring& creationError() const;

private:
    friend struct DockVisibilityTestAccess;
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK edgeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleEdgeMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool registerWindowClass();
    bool registerEdgeWindowClass();
    void updateLayout(bool configure = true);
    void advanceAnimation();
    void positionWindow();
    void positionEdgeWindow();
    void showDock();
    void hideDock();
    void dismissAfterApplicationActivation();
    void releaseVisibilityLock();
    void handleForegroundChanged(HWND foreground);
    void showDockFromEdge();
    void startRevealWatch();
    void pollRevealPointer();
    void updateRevealPointer(POINT pointer, ULONGLONG now);
    void scheduleHide();
    void applyWindowOpacity();
    void applyBackdropEffect();
    void handleTrayCallback(LPARAM lParam);
    void handleTrayCommand(TrayCommand command);
    void toggleAutoHide();
    void toggleStartup();
    void openConfigFolder();
    void handleFullscreenChanged();
    void openSettingsWindow();
    void applySettings(const domain::AppSettings& settings);
    void requestIcons();
    void handleIconResults();
    void refreshRunningState();
    void activateOrOpen(const domain::DockItem& item);
    void showFolderContents(const domain::DockItem& item);
    void beginMouseTracking();
    void launchItemAt(float x, float y);
    void loadItems();
    void loadSettings();
    bool saveItems();
    bool saveSettings();
    bool addPathItem(const std::wstring& path);
    void addItemsFromDrop(HDROP dropHandle);
    void openApplicationPicker();
    void openFilePicker();
    void openIconPicker(const std::string& itemId);
    void showContextMenu(int screenX, int screenY);
    void resetCustomIcon(const std::string& itemId);
    void removeItem(const std::string& id);
    void reorderPressedItem(float pointerX);
    int hitTest(float x, float y) const;
    std::string createItemId() const;
    const domain::DockItem* findItemById(const std::string& id) const;
    float readDpiScale() const;
    HMONITOR targetMonitor() const;
    static std::wstring monitorId(HMONITOR monitor);
    struct MonitorSearchContext {
        const std::wstring* targetId = nullptr;
        HMONITOR match = nullptr;
    };
    static BOOL CALLBACK findMonitorProc(HMONITOR monitor,
                                         HDC deviceContext,
                                         LPRECT monitorRect,
                                         LPARAM data);

    HINSTANCE instance_ = nullptr;
    bool inspectWindow_ = false;
    HWND hwnd_ = nullptr;
    HWND backdropWindow_ = nullptr;
    bool backdropAvailable_ = false;
    SIZE backdropSize_{};
    HWND edgeWindow_ = nullptr;
    HWND tooltipWindow_ = nullptr;
    std::wstring tooltipText_;
    bool classRegistered_ = false;
    bool edgeClassRegistered_ = false;
    bool trackingMouse_ = false;
    bool trackingEdgeMouse_ = false;
    RECT revealBounds_{};
    dock::HoverRevealController hoverRevealController_;
    HWND revealedForeground_ = nullptr;
    bool manuallyHidden_ = false;
    bool dragging_ = false;
    bool menuOpen_ = false;
    float pointerX_ = -1.0F;
    POINT dragStartPoint_{};
    std::string pressedItemId_;
    float dpiScale_ = 1.0F;
    config::DockItemStore itemStore_;
    config::SettingsStore settingsStore_;
    domain::AppSettings settings_;
    dock::AutoHideController autoHideController_;
    StartupManager startupManager_;
    SettingsWindow settingsWindow_;
    TrayController trayController_;
    FullscreenDetector fullscreenDetector_;
    IconLoader iconLoader_;
    bool hiddenForFullscreen_ = false;
    bool wasVisibleBeforeFullscreen_ = true;
    layout::LayoutEngine layoutEngine_;
    layout::LayoutSnapshot layout_;
    layout::LayoutSnapshot targetLayout_;
    ULONGLONG lastAnimationTick_ = 0;
    bool animating_ = false;
    bool pointerLayoutPending_ = false;
    DockRenderer renderer_;
    ShellLauncher launcher_;
    std::vector<domain::DockItem> items_;
    UINT taskbarCreatedMessage_ = 0U;
    std::wstring creationError_;
};

} // namespace planetary::platform
