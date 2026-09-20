#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
/** 从资源提取安装文件，关闭流后再检查，避免把不完整写入视为成功。 */
bool extract(HINSTANCE instance, int id, const std::filesystem::path& path) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(instance, resource);
    const auto* data = static_cast<const char*>(LockResource(loaded));
    if (!data) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(data, SizeofResource(instance, resource));
    output.close();
    return !output.fail();
}

std::filesystem::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value))) return {};
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
}

bool registerInstall(const std::filesystem::path& destination) {
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    const auto programs = knownFolder(FOLDERID_Programs);
    if (programs.empty() || FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(&link)))) return false;
    const auto executable = destination / L"planetary_guard.exe";
    link->SetPath(executable.c_str());
    link->SetWorkingDirectory(destination.c_str());
    if (FAILED(link.As(&file)) || FAILED(file->Save((programs / L"Planetary Guard.lnk").c_str(), TRUE))) return false;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlanetaryGuard",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool success = true;
    const auto write = [&](const wchar_t* name, const std::wstring& value) {
        if (RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) success = false;
    };
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    const std::wstring powershell = (std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe").wstring();
    write(L"DisplayName", L"Planetary Guard");
    write(L"DisplayVersion", L"0.1.5");
    write(L"Publisher", L"Planetary Guard Contributors");
    write(L"InstallLocation", destination.wstring());
    write(L"DisplayIcon", executable.wstring());
    write(L"UninstallString", L"\"" + powershell + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" +
                               (destination / L"uninstall.ps1").wstring() + L"\"");
    RegCloseKey(key);
    return success;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    const bool extractOnly = count == 3 && std::wstring(arguments[1]) == L"--extract-only";
    const auto local = knownFolder(FOLDERID_LocalAppData);
    std::filesystem::path destination = extractOnly ? std::filesystem::path(arguments[2]) : local / L"Programs/PlanetaryGuard";
    LocalFree(arguments);
    if (local.empty() || !destination.is_absolute()) return 2;
    if (!extractOnly && MessageBoxW(nullptr,
        (L"安装 Planetary Guard 0.1.5 到：\n" + destination.wstring() + L"\n\n请先退出正在运行的 Dock。用户配置会保留。").c_str(),
        L"Planetary Guard 安装", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return 0;
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    bool success = !error && extract(instance, 101, destination / L"planetary_guard.exe") &&
        extract(instance, 102, destination / L"README.md") && extract(instance, 103, destination / L"LICENSE") &&
        extract(instance, 104, destination / L"uninstall.ps1");
    if (extractOnly) return success ? 0 : 3;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool integrated = success && SUCCEEDED(com) && registerInstall(destination);
    if (SUCCEEDED(com)) CoUninitialize();
    MessageBoxW(nullptr, !success ? L"安装文件写入失败。请退出 Dock 并检查目录权限后重试。" :
        integrated ? L"安装完成。可从开始菜单启动 Planetary Guard。" : L"程序文件已安装，但开始菜单或卸载注册失败。可直接从安装目录启动。",
        L"Planetary Guard", MB_OK | (success && integrated ? MB_ICONINFORMATION : MB_ICONWARNING));
    return success && integrated ? 0 : 4;
}
