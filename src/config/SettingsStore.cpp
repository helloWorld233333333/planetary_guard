#include "config/SettingsStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config/Json.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace planetary::config {

namespace {

constexpr double kMinimumOpacity = 0.40;
constexpr double kMaximumOpacity = 1.00;
constexpr double kMinimumIconSize = 24.0;
constexpr double kMaximumIconSize = 96.0;
constexpr double kMinimumMargin = 0.0;
constexpr double kMaximumMargin = 128.0;
constexpr double kMinimumDelay = 20.0;
constexpr double kMaximumDelay = 10000.0;

const JsonValue* findField(const JsonObject& object, const char* key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

bool readNumber(const JsonObject& object,
                const char* key,
                double& output,
                std::string* errorMessage,
                bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required && errorMessage != nullptr) *errorMessage = std::string("缺少数字字段：") + key;
        return !required;
    }
    const auto* number = std::get_if<double>(&value->value);
    if (number == nullptr || !std::isfinite(*number)) {
        if (errorMessage != nullptr) *errorMessage = std::string("数字字段无效：") + key;
        return false;
    }
    output = *number;
    return true;
}

bool readBoolean(const JsonObject& object,
                 const char* key,
                 bool& output,
                 std::string* errorMessage,
                 bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required && errorMessage != nullptr) *errorMessage = std::string("缺少布尔字段：") + key;
        return !required;
    }
    const auto* boolean = std::get_if<bool>(&value->value);
    if (boolean == nullptr) {
        if (errorMessage != nullptr) *errorMessage = std::string("布尔字段无效：") + key;
        return false;
    }
    output = *boolean;
    return true;
}

bool readString(const JsonObject& object,
                const char* key,
                std::wstring& output,
                std::string* errorMessage,
                bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required && errorMessage != nullptr) *errorMessage = std::string("缺少字符串字段：") + key;
        return !required;
    }
    const auto* stringValue = std::get_if<std::string>(&value->value);
    if (stringValue == nullptr || stringValue->size() > 1024U) {
        if (errorMessage != nullptr) *errorMessage = std::string("字符串字段无效：") + key;
        return false;
    }
    if (stringValue->empty()) {
        output.clear();
        return true;
    }
    const int requiredLength = MultiByteToWideChar(CP_UTF8,
                                                   MB_ERR_INVALID_CHARS,
                                                   stringValue->data(),
                                                   static_cast<int>(stringValue->size()),
                                                   nullptr,
                                                   0);
    if (requiredLength <= 0) {
        if (errorMessage != nullptr) *errorMessage = std::string("UTF-8 字段无效：") + key;
        return false;
    }
    output.resize(static_cast<std::size_t>(requiredLength));
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        stringValue->data(),
                        static_cast<int>(stringValue->size()),
                        output.data(),
                        requiredLength);
    return true;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8,
                                           WC_ERR_INVALID_CHARS,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr);
    if (length <= 0) return {};
    std::string output(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        output.data(),
                        length,
                        nullptr,
                        nullptr);
    return output;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0);
    if (length <= 0) return {};
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        output.data(),
                        length);
    return output;
}

domain::ThemeMode parseTheme(const std::string& value) {
    if (value == "light") return domain::ThemeMode::Light;
    if (value == "dark") return domain::ThemeMode::Dark;
    if (value == "high_contrast") return domain::ThemeMode::HighContrast;
    return domain::ThemeMode::FollowSystem;
}

const char* themeToString(domain::ThemeMode theme) {
    switch (theme) {
    case domain::ThemeMode::FollowSystem: return "follow_system";
    case domain::ThemeMode::Light: return "light";
    case domain::ThemeMode::Dark: return "dark";
    case domain::ThemeMode::HighContrast: return "high_contrast";
    }
    return "follow_system";
}

void setError(std::wstring* errorMessage, const std::wstring& value) {
    if (errorMessage != nullptr) *errorMessage = value;
}

std::wstring systemErrorMessage(DWORD errorCode) {
    wchar_t buffer[256]{};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        errorCode,
                                        0,
                                        buffer,
                                        static_cast<DWORD>(std::size(buffer)),
                                        nullptr);
    return length > 0U ? std::wstring(buffer, length)
                       : L"Windows 错误码：" + std::to_wstring(errorCode);
}

} // namespace

SettingsStore::SettingsStore() : filePath_(defaultFilePath()) {}

SettingsStore::SettingsStore(std::filesystem::path filePath)
    : filePath_(std::move(filePath)) {}

const std::filesystem::path& SettingsStore::filePath() const {
    return filePath_;
}

bool SettingsStore::load(domain::AppSettings& settings,
                         std::wstring* errorMessage) const {
    setError(errorMessage, L"");
    std::string primaryError;
    if (std::filesystem::exists(filePath_) && loadFile(filePath_, settings, &primaryError)) {
        return true;
    }

    const std::filesystem::path backupPath = filePath_.wstring() + L".bak";
    std::string backupError;
    if (std::filesystem::exists(backupPath) && loadFile(backupPath, settings, &backupError)) {
        return true;
    }

    if (std::filesystem::exists(filePath_)) {
        setError(errorMessage,
                 L"设置文件无法读取，将使用默认设置：" +
                     utf8ToWide(primaryError.empty() ? backupError : primaryError));
    }
    return false;
}

bool SettingsStore::loadFile(const std::filesystem::path& path,
                             domain::AppSettings& settings,
                             std::string* errorMessage) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (errorMessage != nullptr) *errorMessage = "无法打开设置文件";
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    const auto parsed = parseJson(content, errorMessage);
    if (!parsed.has_value() || !parsed->isObject()) {
        if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "设置根节点必须是对象";
        return false;
    }
    const auto* root = std::get_if<JsonObject>(&parsed->value);
    double schemaVersion = 0.0;
    if (!readNumber(*root, "schemaVersion", schemaVersion, errorMessage, true) ||
        schemaVersion != 1.0) {
        if (errorMessage != nullptr && errorMessage->empty()) *errorMessage = "schemaVersion 不受支持";
        return false;
    }

    domain::AppSettings parsedSettings;
    parsedSettings.schemaVersion = 1U;
    // 旧配置没有此字段，保留 0 供应用层执行一次性视觉迁移。
    parsedSettings.appearance.visualRevision = 0U;
    const JsonValue* appearanceValue = findField(*root, "appearance");
    if (appearanceValue != nullptr) {
        const auto* appearance = std::get_if<JsonObject>(&appearanceValue->value);
        if (appearance == nullptr) {
            if (errorMessage != nullptr) *errorMessage = "appearance 必须是对象";
            return false;
        }
        const JsonValue* theme = findField(*appearance, "themeMode");
        if (theme != nullptr) {
            const auto* themeString = std::get_if<std::string>(&theme->value);
            if (themeString == nullptr) {
                if (errorMessage != nullptr) *errorMessage = "themeMode 必须是字符串";
                return false;
            }
            parsedSettings.appearance.themeMode = parseTheme(*themeString);
        }
        double visualRevision = parsedSettings.appearance.visualRevision;
        if (!readNumber(*appearance, "visualRevision", visualRevision, errorMessage, false)) {
            return false;
        }
        parsedSettings.appearance.visualRevision = static_cast<std::uint32_t>(
            std::clamp(visualRevision, 0.0, 4.0));
        double number = parsedSettings.appearance.iconSizeDip;
        if (!readNumber(*appearance, "iconSizeDip", number, errorMessage, false)) return false;
        parsedSettings.appearance.iconSizeDip = static_cast<float>(std::clamp(number, kMinimumIconSize, kMaximumIconSize));
        number = parsedSettings.appearance.maxIconSizeDip;
        if (!readNumber(*appearance, "maxIconSizeDip", number, errorMessage, false)) return false;
        parsedSettings.appearance.maxIconSizeDip = static_cast<float>(std::clamp(number, kMinimumIconSize, kMaximumIconSize));
        number = parsedSettings.appearance.dockOpacity;
        if (!readNumber(*appearance, "dockOpacity", number, errorMessage, false)) return false;
        parsedSettings.appearance.dockOpacity = static_cast<float>(std::clamp(number, kMinimumOpacity, kMaximumOpacity));
        if (!readBoolean(*appearance, "reduceMotion", parsedSettings.appearance.reduceMotion, errorMessage, false)) return false;
        if (!readBoolean(*appearance,
                         "unifiedIconTiles",
                         parsedSettings.appearance.unifiedIconTiles,
                         errorMessage,
                         false)) return false;
    }

    const JsonValue* behaviorValue = findField(*root, "behavior");
    if (behaviorValue != nullptr) {
        const auto* behavior = std::get_if<JsonObject>(&behaviorValue->value);
        if (behavior == nullptr) {
            if (errorMessage != nullptr) *errorMessage = "behavior 必须是对象";
            return false;
        }
        if (!readBoolean(*behavior, "autoHide", parsedSettings.behavior.autoHide, errorMessage, false) ||
            !readBoolean(*behavior, "hideInFullscreen", parsedSettings.behavior.hideInFullscreen, errorMessage, false) ||
            !readBoolean(*behavior, "launchAtStartup", parsedSettings.behavior.launchAtStartup, errorMessage, false)) {
            return false;
        }
        double delay = parsedSettings.behavior.hideDelayMs;
        if (!readNumber(*behavior, "hideDelayMs", delay, errorMessage, false)) return false;
        parsedSettings.behavior.hideDelayMs = static_cast<std::uint32_t>(std::clamp(delay, kMinimumDelay, kMaximumDelay));
        delay = parsedSettings.behavior.showDelayMs;
        if (!readNumber(*behavior, "showDelayMs", delay, errorMessage, false)) return false;
        parsedSettings.behavior.showDelayMs = static_cast<std::uint32_t>(std::clamp(delay, kMinimumDelay, kMaximumDelay));
    }

    const JsonValue* placementValue = findField(*root, "placement");
    if (placementValue != nullptr) {
        const auto* placement = std::get_if<JsonObject>(&placementValue->value);
        if (placement == nullptr) {
            if (errorMessage != nullptr) *errorMessage = "placement 必须是对象";
            return false;
        }
        if (!readString(*placement, "monitorId", parsedSettings.placement.monitorId, errorMessage, false)) return false;
        double margin = parsedSettings.placement.bottomMarginDip;
        if (!readNumber(*placement, "bottomMarginDip", margin, errorMessage, false)) return false;
        parsedSettings.placement.bottomMarginDip = static_cast<float>(std::clamp(margin, kMinimumMargin, kMaximumMargin));
    }

    parsedSettings.appearance.maxIconSizeDip = std::max(parsedSettings.appearance.maxIconSizeDip,
                                                        parsedSettings.appearance.iconSizeDip);
    settings = parsedSettings;
    return true;
}

bool SettingsStore::save(const domain::AppSettings& settings,
                         std::wstring* errorMessage) const {
    setError(errorMessage, L"");
    JsonObject appearance;
    appearance.emplace("visualRevision", JsonValue(static_cast<double>(
        std::min(settings.appearance.visualRevision, 4U))));
    appearance.emplace("themeMode", JsonValue(themeToString(settings.appearance.themeMode)));
    appearance.emplace("iconSizeDip", JsonValue(static_cast<double>(std::clamp(settings.appearance.iconSizeDip, 24.0F, 96.0F))));
    appearance.emplace("maxIconSizeDip", JsonValue(static_cast<double>(std::clamp(settings.appearance.maxIconSizeDip, 24.0F, 96.0F))));
    appearance.emplace("dockOpacity", JsonValue(static_cast<double>(std::clamp(settings.appearance.dockOpacity, 0.40F, 1.0F))));
    appearance.emplace("unifiedIconTiles", JsonValue(settings.appearance.unifiedIconTiles));
    appearance.emplace("reduceMotion", JsonValue(settings.appearance.reduceMotion));

    JsonObject behavior;
    behavior.emplace("autoHide", JsonValue(settings.behavior.autoHide));
    behavior.emplace("hideDelayMs", JsonValue(static_cast<double>(std::clamp(settings.behavior.hideDelayMs, 20U, 10000U))));
    behavior.emplace("showDelayMs", JsonValue(static_cast<double>(std::clamp(settings.behavior.showDelayMs, 20U, 10000U))));
    behavior.emplace("hideInFullscreen", JsonValue(settings.behavior.hideInFullscreen));
    behavior.emplace("launchAtStartup", JsonValue(settings.behavior.launchAtStartup));

    JsonObject placement;
    placement.emplace("monitorId", JsonValue(wideToUtf8(settings.placement.monitorId)));
    placement.emplace("bottomMarginDip", JsonValue(static_cast<double>(std::clamp(settings.placement.bottomMarginDip, 0.0F, 128.0F))));

    JsonObject root;
    root.emplace("schemaVersion", JsonValue(1.0));
    root.emplace("appearance", JsonValue(std::move(appearance)));
    root.emplace("behavior", JsonValue(std::move(behavior)));
    root.emplace("placement", JsonValue(std::move(placement)));
    const std::string serialized = serializeJson(JsonValue(std::move(root)));

    std::error_code directoryError;
    if (!filePath_.parent_path().empty()) {
        std::filesystem::create_directories(filePath_.parent_path(), directoryError);
    }
    if (directoryError) {
        setError(errorMessage, L"无法创建设置目录：" + utf8ToWide(directoryError.message()));
        return false;
    }

    const std::filesystem::path temporaryPath = filePath_.wstring() + L".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            setError(errorMessage, L"无法写入临时设置文件。");
            return false;
        }
        output << serialized;
        output.flush();
        if (!output) {
            setError(errorMessage, L"写入临时设置文件失败。");
            return false;
        }
    }

    const std::filesystem::path backupPath = filePath_.wstring() + L".bak";
    if (std::filesystem::exists(filePath_)) {
        std::error_code backupError;
        std::filesystem::copy_file(filePath_, backupPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   backupError);
        if (backupError) {
            DeleteFileW(temporaryPath.c_str());
            setError(errorMessage, L"无法生成设置备份：" + utf8ToWide(backupError.message()));
            return false;
        }
    }

    if (!MoveFileExW(temporaryPath.c_str(),
                     filePath_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        setError(errorMessage, L"无法替换设置文件：" + systemErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

std::filesystem::path SettingsStore::defaultFilePath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_DEFAULT,
                                       nullptr,
                                       &localAppData))) {
        const std::filesystem::path result = std::filesystem::path(localAppData) /
                                             L"PlanetaryGuard" /
                                             L"settings.json";
        CoTaskMemFree(localAppData);
        return result;
    }
    return std::filesystem::current_path() / L"settings.json";
}

} // namespace planetary::config
