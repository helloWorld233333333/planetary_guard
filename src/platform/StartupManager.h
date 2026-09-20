#pragma once

#include <string>

namespace planetary::platform {

/** 管理当前用户范围的开机启动，不需要管理员权限。 */
class StartupManager {
public:
    bool isEnabled() const;
    bool setEnabled(bool enabled, std::wstring* errorMessage) const;

private:
    static constexpr wchar_t kRunKeyPath[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr wchar_t kValueName[] = L"PlanetaryGuard";
};

} // namespace planetary::platform
