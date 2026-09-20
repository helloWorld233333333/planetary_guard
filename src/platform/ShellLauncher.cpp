#include "platform/ShellLauncher.h"

#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <vector>

namespace planetary::platform {

namespace {
struct WindowSearch {
    std::wstring path;
    HWND window = nullptr;
};

BOOL CALLBACK findApplicationWindow(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return TRUE;
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (queried && _wcsicmp(path, search.path.c_str()) == 0) {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}
}

bool ShellLauncher::launch(const std::wstring& targetPath,
                           const std::wstring& arguments,
                           const std::wstring& workingDirectory,
                           std::wstring* errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (targetPath.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"目标路径为空。";
        }
        return false;
    }

    // 只有直接启动应用且没有额外参数时复用窗口，文件和参数启动保留 Shell 语义。
    if (arguments.empty() && std::filesystem::path(targetPath).extension() == L".exe") {
        wchar_t resolved[32768]{};
        std::wstring path = targetPath;
        if (!std::filesystem::path(path).is_absolute()) {
            const DWORD size = SearchPathW(nullptr, path.c_str(), nullptr,
                                           static_cast<DWORD>(std::size(resolved)), resolved, nullptr);
            if (size > 0 && size < std::size(resolved)) path = resolved;
        }
        WindowSearch search{path};
        EnumWindows(findApplicationWindow, reinterpret_cast<LPARAM>(&search));
        if (search.window != nullptr) {
            if (IsIconic(search.window)) ShowWindowAsync(search.window, SW_RESTORE);
            if (!SetForegroundWindow(search.window)) {
                FLASHWINFO flash{sizeof(FLASHWINFO), search.window, FLASHW_TRAY, 3, 0};
                FlashWindowEx(&flash);
            }
            return true;
        }
    }

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"open";
    executeInfo.lpFile = targetPath.c_str();
    executeInfo.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    executeInfo.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    executeInfo.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&executeInfo)) {
        if (executeInfo.hProcess != nullptr) {
            CloseHandle(executeInfo.hProcess);
        }
        return true;
    }

    if (errorMessage != nullptr) {
        wchar_t buffer[256]{};
        const DWORD errorCode = GetLastError();
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                            nullptr,
                                            errorCode,
                                            0,
                                            buffer,
                                            static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                                            nullptr);
        if (length > 0U) {
            *errorMessage = buffer;
        } else {
            *errorMessage = L"无法启动目标，系统错误码：" + std::to_wstring(errorCode);
        }
    }
    return false;
}

} // namespace planetary::platform
