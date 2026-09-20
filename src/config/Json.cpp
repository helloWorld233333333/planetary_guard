#include "config/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace planetary::config {

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    std::optional<JsonValue> parse(std::string* errorMessage) {
        skipWhitespace();
        std::optional<JsonValue> value = parseValue(errorMessage);
        if (!value.has_value()) {
            return std::nullopt;
        }
        skipWhitespace();
        if (position_ != input_.size()) {
            setError(errorMessage, "JSON 后存在未解析内容");
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<JsonValue> parseValue(std::string* errorMessage) {
        skipWhitespace();
        if (position_ >= input_.size()) {
            setError(errorMessage, "JSON 值缺失");
            return std::nullopt;
        }

        switch (input_[position_]) {
        case '{':
            return parseObject(errorMessage);
        case '[':
            return parseArray(errorMessage);
        case '"': {
            std::optional<std::string> stringValue = parseString(errorMessage);
            return stringValue.has_value()
                       ? std::optional<JsonValue>(JsonValue(std::move(*stringValue)))
                       : std::nullopt;
        }
        case 't':
            return parseLiteral("true", JsonValue(true), errorMessage);
        case 'f':
            return parseLiteral("false", JsonValue(false), errorMessage);
        case 'n':
            return parseLiteral("null", JsonValue(nullptr), errorMessage);
        default:
            return parseNumber(errorMessage);
        }
    }

    std::optional<JsonValue> parseObject(std::string* errorMessage) {
        ++position_; // {
        JsonObject object;
        skipWhitespace();
        if (consume('}')) {
            return JsonValue(std::move(object));
        }

        while (position_ < input_.size()) {
            if (input_[position_] != '"') {
                setError(errorMessage, "对象键必须是字符串");
                return std::nullopt;
            }
            std::optional<std::string> key = parseString(errorMessage);
            if (!key.has_value()) {
                return std::nullopt;
            }
            skipWhitespace();
            if (!consume(':')) {
                setError(errorMessage, "对象键后缺少冒号");
                return std::nullopt;
            }
            std::optional<JsonValue> value = parseValue(errorMessage);
            if (!value.has_value()) {
                return std::nullopt;
            }
            object[std::move(*key)] = std::move(*value);
            skipWhitespace();
            if (consume('}')) {
                return JsonValue(std::move(object));
            }
            if (!consume(',')) {
                setError(errorMessage, "对象成员之间缺少逗号");
                return std::nullopt;
            }
            skipWhitespace();
        }

        setError(errorMessage, "对象没有闭合");
        return std::nullopt;
    }

    std::optional<JsonValue> parseArray(std::string* errorMessage) {
        ++position_; // [
        JsonArray array;
        skipWhitespace();
        if (consume(']')) {
            return JsonValue(std::move(array));
        }

        while (position_ < input_.size()) {
            std::optional<JsonValue> value = parseValue(errorMessage);
            if (!value.has_value()) {
                return std::nullopt;
            }
            array.push_back(std::move(*value));
            skipWhitespace();
            if (consume(']')) {
                return JsonValue(std::move(array));
            }
            if (!consume(',')) {
                setError(errorMessage, "数组元素之间缺少逗号");
                return std::nullopt;
            }
            skipWhitespace();
        }

        setError(errorMessage, "数组没有闭合");
        return std::nullopt;
    }

    std::optional<std::string> parseString(std::string* errorMessage) {
        if (!consume('"')) {
            setError(errorMessage, "字符串缺少起始引号");
            return std::nullopt;
        }

        std::string result;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return result;
            }
            if (character < 0x20U) {
                setError(errorMessage, "字符串包含未转义控制字符");
                return std::nullopt;
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }

            if (position_ >= input_.size()) {
                setError(errorMessage, "字符串转义不完整");
                return std::nullopt;
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                const std::optional<unsigned int> codePoint = parseUnicodeEscape(errorMessage);
                if (!codePoint.has_value()) {
                    return std::nullopt;
                }
                appendUtf8(*codePoint, result);
                break;
            }
            default:
                setError(errorMessage, "字符串包含未知转义");
                return std::nullopt;
            }
        }

        setError(errorMessage, "字符串没有闭合");
        return std::nullopt;
    }

    std::optional<unsigned int> parseUnicodeEscape(std::string* errorMessage) {
        if (position_ + 4U > input_.size()) {
            setError(errorMessage, "Unicode 转义长度不足");
            return std::nullopt;
        }
        unsigned int value = 0U;
        for (std::size_t index = 0; index < 4U; ++index) {
            const int digit = hexDigit(input_[position_++]);
            if (digit < 0) {
                setError(errorMessage, "Unicode 转义包含非法字符");
                return std::nullopt;
            }
            value = (value << 4U) | static_cast<unsigned int>(digit);
        }

        // 合并常见的 UTF-16 代理对，避免中文扩展字符被拆成两个码点。
        if (value >= 0xD800U && value <= 0xDBFFU &&
            position_ + 6U <= input_.size() && input_[position_] == '\\' &&
            input_[position_ + 1U] == 'u') {
            position_ += 2U;
            const std::optional<unsigned int> low = parseUnicodeEscape(errorMessage);
            if (!low.has_value() || *low < 0xDC00U || *low > 0xDFFFU) {
                setError(errorMessage, "Unicode 代理对无效");
                return std::nullopt;
            }
            value = 0x10000U + ((value - 0xD800U) << 10U) + (*low - 0xDC00U);
        }
        return value;
    }

    std::optional<JsonValue> parseNumber(std::string* errorMessage) {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            setError(errorMessage, "非法 JSON 值");
            return std::nullopt;
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                setError(errorMessage, "小数点后缺少数字");
                return std::nullopt;
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                setError(errorMessage, "指数后缺少数字");
                return std::nullopt;
            }
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }

        const std::string number(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0' || !std::isfinite(parsed)) {
            setError(errorMessage, "JSON 数字无效");
            return std::nullopt;
        }
        return JsonValue(parsed);
    }

    std::optional<JsonValue> parseLiteral(std::string_view literal,
                                          JsonValue value,
                                          std::string* errorMessage) {
        if (input_.substr(position_, literal.size()) != literal) {
            setError(errorMessage, "JSON 字面量无效");
            return std::nullopt;
        }
        position_ += literal.size();
        return value;
    }

    static int hexDigit(char character) {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    }

    static void appendUtf8(unsigned int codePoint, std::string& output) {
        if (codePoint <= 0x7FU) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else if (codePoint <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else if (codePoint <= 0x10FFFFU) {
            output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    }

    void skipWhitespace() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    static void setError(std::string* errorMessage, const char* message) {
        if (errorMessage != nullptr && errorMessage->empty()) {
            *errorMessage = message;
        }
    }

    std::string_view input_;
    std::size_t position_ = 0U;
};

void appendEscapedString(std::string_view value, std::string& output) {
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U) {
                std::ostringstream control;
                control << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(character);
                output += control.str();
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void serializeValue(const JsonValue& value, std::string& output);

struct JsonSerializerVisitor {
    std::string& output;

    void operator()(std::nullptr_t) const { output += "null"; }

    void operator()(bool current) const { output += current ? "true" : "false"; }

    void operator()(double current) const {
        std::ostringstream number;
        number << std::setprecision(std::numeric_limits<double>::max_digits10) << current;
        output += number.str();
    }

    void operator()(const std::string& current) const {
        appendEscapedString(current, output);
    }

    void operator()(const JsonArray& current) const {
        output.push_back('[');
        for (std::size_t index = 0; index < current.size(); ++index) {
            if (index > 0U) output.push_back(',');
            serializeValue(current[index], output);
        }
        output.push_back(']');
    }

    void operator()(const JsonObject& current) const {
        output.push_back('{');
        std::size_t index = 0U;
        for (const auto& entry : current) {
            if (index++ > 0U) output.push_back(',');
            appendEscapedString(entry.first, output);
            output.push_back(':');
            serializeValue(entry.second, output);
        }
        output.push_back('}');
    }
};

void serializeValue(const JsonValue& value, std::string& output) {
    std::visit(JsonSerializerVisitor{output}, value.value);
}

} // namespace

JsonValue::JsonValue() : value(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) : value(nullptr) {}
JsonValue::JsonValue(bool booleanValue) : value(booleanValue) {}
JsonValue::JsonValue(double numberValue) : value(numberValue) {}
JsonValue::JsonValue(std::string stringValue) : value(std::move(stringValue)) {}
JsonValue::JsonValue(const char* stringValue) : value(std::string(stringValue == nullptr ? "" : stringValue)) {}
JsonValue::JsonValue(JsonArray arrayValue) : value(std::move(arrayValue)) {}
JsonValue::JsonValue(JsonObject objectValue) : value(std::move(objectValue)) {}

bool JsonValue::isObject() const { return std::holds_alternative<JsonObject>(value); }
bool JsonValue::isArray() const { return std::holds_alternative<JsonArray>(value); }
bool JsonValue::isString() const { return std::holds_alternative<std::string>(value); }
bool JsonValue::isNumber() const { return std::holds_alternative<double>(value); }
bool JsonValue::isBoolean() const { return std::holds_alternative<bool>(value); }

const JsonValue* JsonValue::find(std::string_view key) const {
    const auto* object = std::get_if<JsonObject>(&value);
    if (object == nullptr) return nullptr;
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

JsonValue* JsonValue::find(std::string_view key) {
    auto* object = std::get_if<JsonObject>(&value);
    if (object == nullptr) return nullptr;
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

std::optional<JsonValue> parseJson(std::string_view input, std::string* errorMessage) {
    if (errorMessage != nullptr) errorMessage->clear();
    return JsonParser(input).parse(errorMessage);
}

std::string serializeJson(const JsonValue& value) {
    std::string output;
    output.reserve(256U);
    serializeValue(value, output);
    return output;
}

} // namespace planetary::config
