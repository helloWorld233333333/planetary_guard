#include "config/DockItemStore.h"

#include "config/Json.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace planetary::config {

namespace {

constexpr std::size_t kMaximumItems = 200U;
constexpr std::size_t kMaximumIdBytes = 128U;
constexpr std::size_t kMaximumDisplayNameBytes = 256U;
constexpr std::size_t kMaximumPathBytes = 32768U;

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8,
                                             MB_ERR_INVALID_CHARS,
                                             value.data(),
                                             static_cast<int>(value.size()),
                                             nullptr,
                                             0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        result.data(),
                        required);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8,
                                             WC_ERR_INVALID_CHARS,
                                             value.data(),
                                             static_cast<int>(value.size()),
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        result.data(),
                        required,
                        nullptr,
                        nullptr);
    return result;
}

const JsonValue* findField(const JsonObject& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

bool readString(const JsonObject& object,
                std::string_view key,
                std::size_t maximumBytes,
                std::wstring& output,
                std::string* errorMessage,
                bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            *errorMessage = "缺少字段：" + std::string(key);
            return false;
        }
        output.clear();
        return true;
    }
    const auto* stringValue = std::get_if<std::string>(&value->value);
    if (stringValue == nullptr || stringValue->size() > maximumBytes) {
        *errorMessage = "字符串字段无效：" + std::string(key);
        return false;
    }
    output = utf8ToWide(*stringValue);
    if (!stringValue->empty() && output.empty()) {
        *errorMessage = "字符串不是有效 UTF-8：" + std::string(key);
        return false;
    }
    return true;
}

bool readId(const JsonObject& object, std::string& output, std::string* errorMessage) {
    const JsonValue* value = findField(object, "id");
    const auto* stringValue = value == nullptr ? nullptr : std::get_if<std::string>(&value->value);
    if (stringValue == nullptr || stringValue->empty() || stringValue->size() > kMaximumIdBytes) {
        *errorMessage = "id 字段无效";
        return false;
    }
    const std::wstring wideValue = utf8ToWide(*stringValue);
    if (wideValue.empty()) {
        *errorMessage = "id 字段不是有效 UTF-8";
        return false;
    }
    output = *stringValue;
    return true;
}

bool readNumber(const JsonObject& object,
                std::string_view key,
                double& output,
                std::string* errorMessage,
                bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            *errorMessage = "缺少数字字段：" + std::string(key);
            return false;
        }
        return true;
    }
    const auto* number = std::get_if<double>(&value->value);
    if (number == nullptr || !std::isfinite(*number)) {
        *errorMessage = "数字字段无效：" + std::string(key);
        return false;
    }
    output = *number;
    return true;
}

bool readBoolean(const JsonObject& object,
                 std::string_view key,
                 bool& output,
                 std::string* errorMessage,
                 bool required) {
    const JsonValue* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            *errorMessage = "缺少布尔字段：" + std::string(key);
            return false;
        }
        return true;
    }
    const auto* boolean = std::get_if<bool>(&value->value);
    if (boolean == nullptr) {
        *errorMessage = "布尔字段无效：" + std::string(key);
        return false;
    }
    output = *boolean;
    return true;
}

domain::DockItemType parseType(const std::string& value) {
    if (value == "application") return domain::DockItemType::Application;
    if (value == "shortcut") return domain::DockItemType::Shortcut;
    if (value == "file") return domain::DockItemType::File;
    if (value == "folder") return domain::DockItemType::Folder;
    if (value == "url") return domain::DockItemType::Url;
    return domain::DockItemType::Unknown;
}

const char* typeToString(domain::DockItemType type) {
    switch (type) {
    case domain::DockItemType::Application: return "application";
    case domain::DockItemType::Shortcut: return "shortcut";
    case domain::DockItemType::File: return "file";
    case domain::DockItemType::Folder: return "folder";
    case domain::DockItemType::Url: return "url";
    case domain::DockItemType::Unknown: return "unknown";
    }
    return "unknown";
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
    if (length > 0U) return std::wstring(buffer, length);
    return L"Windows 错误码：" + std::to_wstring(errorCode);
}

} // namespace

DockItemStore::DockItemStore() : filePath_(defaultFilePath()) {}

DockItemStore::DockItemStore(std::filesystem::path filePath)
    : filePath_(std::move(filePath)) {}

const std::filesystem::path& DockItemStore::filePath() const {
    return filePath_;
}

bool DockItemStore::load(std::vector<domain::DockItem>& items,
                         std::wstring* errorMessage) const {
    setError(errorMessage, L"");
    std::string primaryError;
    if (std::filesystem::exists(filePath_) && loadFile(filePath_, items, &primaryError)) {
        return true;
    }

    const std::filesystem::path backupPath = filePath_.wstring() + L".bak";
    std::string backupError;
    if (std::filesystem::exists(backupPath) && loadFile(backupPath, items, &backupError)) {
        return true;
    }

    // 首次启动没有配置文件不是错误；正式文件存在但损坏时返回错误提示，
    // 调用方仍可继续使用内置默认项目。
    if (std::filesystem::exists(filePath_)) {
        setError(errorMessage,
                 L"配置文件无法读取，将使用默认项目：" +
                     utf8ToWide(primaryError.empty() ? backupError : primaryError));
    }
    return false;
}

bool DockItemStore::loadFile(const std::filesystem::path& path,
                             std::vector<domain::DockItem>& items,
                             std::string* errorMessage) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *errorMessage = "无法打开配置文件";
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    const auto parsed = parseJson(content, errorMessage);
    if (!parsed.has_value() || !parsed->isObject()) {
        if (errorMessage->empty()) *errorMessage = "配置根节点必须是对象";
        return false;
    }

    const auto* root = std::get_if<JsonObject>(&parsed->value);
    const JsonValue* schemaValue = findField(*root, "schemaVersion");
    const auto* schema = schemaValue == nullptr ? nullptr : std::get_if<double>(&schemaValue->value);
    if (schema == nullptr || *schema < 1.0 || *schema > 1.0) {
        *errorMessage = "schemaVersion 不受支持";
        return false;
    }

    const JsonValue* itemsValue = findField(*root, "items");
    const auto* array = itemsValue == nullptr ? nullptr : std::get_if<JsonArray>(&itemsValue->value);
    if (array == nullptr || array->size() > kMaximumItems) {
        *errorMessage = "items 必须是最多 200 项的数组";
        return false;
    }

    std::vector<domain::DockItem> parsedItems;
    parsedItems.reserve(array->size());
    std::unordered_set<std::string> ids;
    for (const JsonValue& itemValue : *array) {
        const auto* object = std::get_if<JsonObject>(&itemValue.value);
        if (object == nullptr) {
            *errorMessage = "items 中存在非对象元素";
            return false;
        }

        domain::DockItem item;
        if (!readId(*object, item.id, errorMessage)) return false;
        if (!ids.insert(item.id).second) {
            *errorMessage = "items 中存在重复 id";
            return false;
        }

        std::string typeValue;
        const JsonValue* type = findField(*object, "type");
        if (type != nullptr) {
            const auto* stringType = std::get_if<std::string>(&type->value);
            if (stringType == nullptr) {
                *errorMessage = "type 字段无效";
                return false;
            }
            typeValue = *stringType;
        }
        item.type = parseType(typeValue);

        if (!readString(*object, "displayName", kMaximumDisplayNameBytes, item.displayName, errorMessage, true) ||
            !readString(*object, "targetPath", kMaximumPathBytes, item.targetPath, errorMessage, true) ||
            !readString(*object, "arguments", kMaximumPathBytes, item.arguments, errorMessage, false) ||
            !readString(*object, "workingDirectory", kMaximumPathBytes, item.workingDirectory, errorMessage, false) ||
            !readString(*object, "customIconPath", kMaximumPathBytes, item.customIconPath, errorMessage, false)) {
            return false;
        }
        if (item.targetPath.empty()) {
            *errorMessage = "targetPath 不能为空";
            return false;
        }

        double order = static_cast<double>(parsedItems.size() * 10U);
        if (!readNumber(*object, "order", order, errorMessage, false) ||
            !readBoolean(*object, "enabled", item.enabled, errorMessage, false)) {
            return false;
        }
        item.order = static_cast<int>(std::clamp(order, -100000.0, 100000.0));
        parsedItems.push_back(std::move(item));
    }

    std::sort(parsedItems.begin(), parsedItems.end(), [](const auto& left, const auto& right) {
        if (left.order != right.order) return left.order < right.order;
        return left.id < right.id;
    });
    items = std::move(parsedItems);
    return true;
}

bool DockItemStore::save(const std::vector<domain::DockItem>& items,
                         std::wstring* errorMessage) const {
    setError(errorMessage, L"");
    if (items.size() > kMaximumItems) {
        setError(errorMessage, L"Dock 项目数量超过 200 项限制。");
        return false;
    }

    JsonArray jsonItems;
    jsonItems.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const domain::DockItem& item = items[index];
        if (item.transient) continue;
        JsonObject object;
        object.emplace("id", JsonValue(item.id));
        object.emplace("type", JsonValue(typeToString(item.type)));
        object.emplace("displayName", JsonValue(wideToUtf8(item.displayName)));
        object.emplace("targetPath", JsonValue(wideToUtf8(item.targetPath)));
        object.emplace("arguments", JsonValue(wideToUtf8(item.arguments)));
        object.emplace("workingDirectory", JsonValue(wideToUtf8(item.workingDirectory)));
        object.emplace("customIconPath", JsonValue(wideToUtf8(item.customIconPath)));
        object.emplace("order", JsonValue(static_cast<double>(index * 10U)));
        object.emplace("enabled", JsonValue(item.enabled));
        jsonItems.emplace_back(std::move(object));
    }

    JsonObject root;
    root.emplace("schemaVersion", JsonValue(1.0));
    root.emplace("items", JsonValue(std::move(jsonItems)));
    const std::string serialized = serializeJson(JsonValue(std::move(root)));

    std::error_code directoryError;
    if (!filePath_.parent_path().empty()) {
        std::filesystem::create_directories(filePath_.parent_path(), directoryError);
    }
    if (directoryError) {
        setError(errorMessage, L"无法创建配置目录：" + utf8ToWide(directoryError.message()));
        return false;
    }

    const std::filesystem::path temporaryPath = filePath_.wstring() + L".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            setError(errorMessage, L"无法写入临时配置文件。");
            return false;
        }
        output << serialized;
        output.flush();
        if (!output) {
            setError(errorMessage, L"写入临时配置文件失败。");
            return false;
        }
    }

    const std::filesystem::path backupPath = filePath_.wstring() + L".bak";
    if (std::filesystem::exists(filePath_)) {
        std::error_code backupError;
        std::filesystem::copy_file(filePath_,
                                   backupPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   backupError);
        if (backupError) {
            DeleteFileW(temporaryPath.c_str());
            setError(errorMessage, L"无法生成配置备份：" + utf8ToWide(backupError.message()));
            return false;
        }
    }

    if (!MoveFileExW(temporaryPath.c_str(),
                     filePath_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        setError(errorMessage, L"无法替换配置文件：" + systemErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

std::filesystem::path DockItemStore::defaultFilePath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_DEFAULT,
                                       nullptr,
                                       &localAppData))) {
        const std::filesystem::path result = std::filesystem::path(localAppData) /
                                             L"PlanetaryGuard" /
                                             L"dock-items.json";
        CoTaskMemFree(localAppData);
        return result;
    }
    return std::filesystem::current_path() / L"dock-items.json";
}

} // namespace planetary::config
