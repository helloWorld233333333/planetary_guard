#include "platform/WindowCatalog.h"
#include "platform/DesktopGeometry.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using namespace planetary::platform;
        const RECT monitor{-1920, 0, 0, 1080};
        const RECT dockBounds{-1300, 900, -600, 1000};
        const auto secondEdge = centeredBottomRevealBounds(RECT{1920, -200, 3200, 824}, 700);
        if (secondEdge.left != 2210 || secondEdge.right != 2910 || secondEdge.top != 822)
            throw std::runtime_error("target screen must have its own centered physical edge");
        const auto wideEdge = centeredBottomRevealBounds(monitor, 3000);
        if (wideEdge.left != -1920 || wideEdge.right != 0)
            throw std::runtime_error("activation width must fit narrow screens");
        const auto edge = bottomRevealBounds(monitor, dockBounds);
        if (edge.top != 1078 || edge.bottom != 1080 || edge.left != -1300 || edge.right != -600)
            throw std::runtime_error("reveal must use physical bottom on negative-coordinate monitor");
        if (PtInRect(&edge, POINT{-900, 960})) throw std::runtime_error("input area must not trigger reveal");
        if (!PtInRect(&edge, POINT{-900, 1079})) throw std::runtime_error("true bottom must trigger reveal");
        const RECT maximized{-1920, 0, 0, 1078};
        if (isFullscreenCoverage(maximized, monitor, true, WS_OVERLAPPEDWINDOW))
            throw std::runtime_error("autohide-taskbar maximized app must not be fullscreen");
        if (!isFullscreenCoverage(monitor, monitor, false, WS_POPUP))
            throw std::runtime_error("borderless fullscreen must still be detected");
        // 来自实机钉钉窗口：自绘边框、保留系统菜单、处于最大化。
        if (isFullscreenCoverage(monitor, monitor, true, 0x97080000L))
            throw std::runtime_error("maximized DingTalk custom frame must not suppress dock");
        if (!isFullscreenCoverage(monitor, monitor, true, WS_POPUP | WS_VISIBLE | WS_MAXIMIZE))
            throw std::runtime_error("maximized borderless game without window controls must remain fullscreen");
        if (!isFullscreenCoverage(monitor, monitor, false, WS_POPUP | WS_VISIBLE | WS_SYSMENU))
            throw std::runtime_error("nonmaximized fullscreen video retaining system menu must remain fullscreen");
        if (!isFullscreenCoverage(monitor, monitor, false, WS_POPUP | WS_VISIBLE))
            throw std::runtime_error("video or game entering borderless fullscreen must suppress dock");
        const auto work = dockWorkArea(monitor, maximized, 48);
        if (work.bottom != 1032) throw std::runtime_error("dock must avoid autohide taskbar area");
        if (dockWorkArea(monitor, maximized, 0).bottom != 1078)
            throw std::runtime_error("normal work area must stay unchanged");
        using planetary::platform::applicationIdentity;
        if (!applicationIdentity(L"https://example.com").empty()) throw std::runtime_error("URLs must not become application identities");
        if (!applicationIdentity(L"C:\\work\\notes.txt").empty()) throw std::runtime_error("Documents must not match running apps");
        if (applicationIdentity(L"C:\\Tools\\APP.EXE") != applicationIdentity(L"c:\\tools\\app.exe"))
            throw std::runtime_error("Executable matching should ignore Windows path case");
        if (applicationIdentity(L"C:\\One\\app.exe") == applicationIdentity(L"C:\\Two\\app.exe"))
            throw std::runtime_error("Same file name in different folders must not match");
        if (planetary::platform::activateApplicationWindow({})) throw std::runtime_error("Stale handles must not activate");
        std::cout << "Window identity tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what();
        return 1;
    }
}
