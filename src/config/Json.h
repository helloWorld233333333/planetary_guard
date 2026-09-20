#pragma once

#include <map>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace planetary::config {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

/** 一个只覆盖配置文件所需类型的 JSON 值。 */
struct JsonValue {
    using Storage = std::variant<std::nullptr_t,
                                 bool,
                                 double,
                                 std::string,
                                 JsonArray,
                                 JsonObject>;

    Storage value;

    JsonValue();
    explicit JsonValue(std::nullptr_t);
    explicit JsonValue(bool booleanValue);
    explicit JsonValue(double numberValue);
    explicit JsonValue(std::string stringValue);
    explicit JsonValue(const char* stringValue);
    explicit JsonValue(JsonArray arrayValue);
    explicit JsonValue(JsonObject objectValue);

    bool isObject() const;
    bool isArray() const;
    bool isString() const;
    bool isNumber() const;
    bool isBoolean() const;

    const JsonValue* find(std::string_view key) const;
    JsonValue* find(std::string_view key);
};

/** 解析 UTF-8 JSON；失败时返回空值并写出简短错误。 */
std::optional<JsonValue> parseJson(std::string_view input, std::string* errorMessage);

/** 序列化为 UTF-8 JSON。 */
std::string serializeJson(const JsonValue& value);

} // namespace planetary::config
