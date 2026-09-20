#include "platform/StartupManager.h"

#include <windows.h>

#include <array>

namespace planetary::platform {

namespace {

std::wstring currentStartupCommand() {
    std::array<wchar_t, 32768> executablePath{};
    const DWORD pathLength = GetModuleFileNameW(nullptr,
                                                executablePath.data(),
                                                static_cast<DWORD>(executablePath.size()));
    if (pathLength == 0U || pathLength >= executablePath.size()) return {};
    return L"\"" + std::wstring(executablePath.data(), pathLength) + L"\"";
}

std::wstring lastErrorMessage(DWORD errorCode) {
    std::array<wchar_t, 256> buffer{};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        errorCode,
                                        0,
                                        buffer.data(),
                                        static_cast<DWORD>(buffer.size()),
                                        nullptr);
    return length > 0U ? std::wstring(buffer.data(), length)
                       : L"Windows 错误码：" + std::to_wstring(errorCode);
}

} // namespace

bool StartupManager::isEnabled() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      kRunKeyPath,
                      0U,
                      KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0U;
    std::array<wchar_t, 32768> value{};
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    const LONG result = RegQueryValueExW(key,
                                         kValueName,
                                         nullptr,
                                         &type,
                                         reinterpret_cast<BYTE*>(value.data()),
                                         &bytes);
    RegCloseKey(key);
    const std::wstring expected = currentStartupCommand();
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) &&
           !expected.empty() &&
           CompareStringOrdinal(value.data(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool StartupManager::setEnabled(bool enabled, std::wstring* errorMessage) const {
    if (errorMessage != nullptr) errorMessage->clear();

    HKEY key = nullptr;
    if (!enabled) {
        const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER,
                                              kRunKeyPath,
                                              0U,
                                              KEY_SET_VALUE,
                                              &key);
        if (openResult == ERROR_FILE_NOT_FOUND) return true;
        if (openResult != ERROR_SUCCESS) {
            if (errorMessage != nullptr) *errorMessage = lastErrorMessage(static_cast<DWORD>(openResult));
            return false;
        }
        const LONG deleteResult = RegDeleteValueW(key, kValueName);
        RegCloseKey(key);
        if (deleteResult == ERROR_FILE_NOT_FOUND) return true;
        if (deleteResult != ERROR_SUCCESS && errorMessage != nullptr) {
            *errorMessage = lastErrorMessage(static_cast<DWORD>(deleteResult));
        }
        return deleteResult == ERROR_SUCCESS;
    }

    const LONG createResult = RegCreateKeyExW(HKEY_CURRENT_USER,
                                              kRunKeyPath,
                                              0U,
                                              nullptr,
                                              REG_OPTION_NON_VOLATILE,
                                              KEY_SET_VALUE,
                                              nullptr,
                                              &key,
                                              nullptr);
    if (createResult != ERROR_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = lastErrorMessage(static_cast<DWORD>(createResult));
        return false;
    }

    const std::wstring command = currentStartupCommand();
    if (command.empty()) {
        RegCloseKey(key);
        if (errorMessage != nullptr) *errorMessage = L"无法获取程序路径。";
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command.size() + 1U) * sizeof(wchar_t));
    const LONG setResult = RegSetValueExW(key,
                                          kValueName,
                                          0U,
                                          REG_SZ,
                                          reinterpret_cast<const BYTE*>(command.c_str()),
                                          bytes);
    RegCloseKey(key);
    if (setResult != ERROR_SUCCESS && errorMessage != nullptr) {
        *errorMessage = lastErrorMessage(static_cast<DWORD>(setResult));
    }
    return setResult == ERROR_SUCCESS;
}

} // namespace planetary::platform
