#include "platform/DockWindow.h"
#include "platform/SingleInstance.h"

#include <windows.h>

namespace {

void configureDpiAwareness() {
    // 正式程序由 resources/planetary_guard.manifest 声明 PerMonitorV2；
    // 这里仅作为未携带资源时的旧系统回退，调用失败也不会阻止启动。
    SetProcessDPIAware();
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE /*previousInstance*/,
                    PWSTR commandLine,
                    int /*showCommand*/) {
    configureDpiAwareness();

    planetary::platform::SingleInstanceGuard instanceGuard(
        L"Local\\PlanetaryGuard.Dock.Singleton");
    if (instanceGuard.anotherInstanceRunning()) {
        return 0;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(comResult);

    int exitCode = 0;
    {
        // DockWindow 持有依赖 COM 的 Shell/DirectWrite 资源，必须先于
        // CoUninitialize 销毁。
        planetary::platform::DockWindow dockWindow(instance, std::wstring(commandLine) == L"--inspect-window");
        if (!dockWindow.create()) {
            const std::wstring message = dockWindow.creationError().empty()
                                             ? L"Planetary Guard Dock 初始化失败。"
                                             : dockWindow.creationError();
            MessageBoxW(nullptr,
                        message.c_str(),
                        L"Planetary Guard",
                        MB_OK | MB_ICONERROR);
            exitCode = 1;
        } else {
            dockWindow.show();

            MSG message{};
            BOOL messageResult = 0;
            while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            // GetMessage 仅在 API 失败时返回 -1；WM_QUIT 是正常退出，不应把
            // 最后一条窗口消息的 wParam 当成进程错误码。
            exitCode = messageResult == -1 ? 2 : 0;
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return exitCode;
}
