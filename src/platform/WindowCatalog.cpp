#include "platform/WindowCatalog.h"
#include <dwmapi.h>
#include <shobjidl.h>
#include <wrl.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace planetary::platform {
std::wstring applicationIdentity(const std::wstring& target) {
    std::wstring path = target;
    if (_wcsicmp(std::filesystem::path(path).extension().c_str(), L".lnk") == 0) {
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        Microsoft::WRL::ComPtr<IPersistFile> file;
        wchar_t resolved[32768]{};
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&link))) || FAILED(link.As(&file)) ||
            FAILED(file->Load(path.c_str(), STGM_READ)) ||
            FAILED(link->GetPath(resolved, static_cast<int>(std::size(resolved)), nullptr, SLGP_RAWPATH))) return {};
        path = resolved;
    }
    if (_wcsicmp(std::filesystem::path(path).extension().c_str(), L".exe") != 0) return {};
    wchar_t resolved[32768]{};
    if (!std::filesystem::path(path).is_absolute()) {
        const DWORD count = SearchPathW(nullptr, path.c_str(), nullptr,
            static_cast<DWORD>(std::size(resolved)), resolved, nullptr);
        if (count == 0 || count >= std::size(resolved)) return {};
        path = resolved;
    }
    path = std::filesystem::path(path).lexically_normal().wstring();
    std::transform(path.begin(), path.end(), path.begin(), ::towlower);
    return path;
}

std::vector<ApplicationWindow> enumerateApplicationWindows() {
    std::vector<ApplicationWindow> result;
    EnumWindows([](HWND handle, LPARAM data) -> BOOL {
        if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER) != nullptr ||
            (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        wchar_t title[512]{};
        if (GetWindowTextW(handle, title, static_cast<int>(std::size(title))) == 0) return TRUE;
        DWORD processId = 0;
        GetWindowThreadProcessId(handle, &processId);
        if (processId == GetCurrentProcessId()) return TRUE;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process) return TRUE;
        wchar_t path[32768]{};
        DWORD count = static_cast<DWORD>(std::size(path));
        const BOOL success = QueryFullProcessImageNameW(process, 0, path, &count);
        CloseHandle(process);
        if (success) {
            auto& windows = *reinterpret_cast<std::vector<ApplicationWindow>*>(data);
            windows.push_back({handle, processId, applicationIdentity(path), title});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

bool activateApplicationWindow(const ApplicationWindow& window) {
    DWORD processId = 0;
    if (!IsWindow(window.handle)) return false;
    GetWindowThreadProcessId(window.handle, &processId);
    if (processId != window.processId) return false;
    if (IsIconic(window.handle)) ShowWindowAsync(window.handle, SW_RESTORE);
    if (!SetForegroundWindow(window.handle)) {
        FLASHWINFO flash{sizeof(FLASHWINFO), window.handle, FLASHW_TRAY, 3, 0};
        FlashWindowEx(&flash);
    }
    return true;
}
}
