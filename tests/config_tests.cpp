#include "config/Json.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testJsonParsesChineseConfig() {
    const std::string input =
        R"({"schemaVersion":1,"items":[{"displayName":"终端","enabled":true,"order":10}]})";
    std::string error;
    const auto parsed = planetary::config::parseJson(input, &error);
    expectTrue(parsed.has_value(), "valid config should parse");
    expectTrue(parsed->isObject(), "root should be an object");
    const auto* items = parsed->find("items");
    expectTrue(items != nullptr && items->isArray(), "items should be an array");
    const auto& array = std::get<planetary::config::JsonArray>(items->value);
    expectTrue(array.size() == 1U, "config should contain one item");
    const auto* name = array.front().find("displayName");
    expectTrue(name != nullptr && name->isString(), "displayName should be a string");
    expectTrue(std::get<std::string>(name->value) == "终端", "UTF-8 text should be preserved");
}

void testJsonSupportsUnicodeEscapeAndRoundTrip() {
    std::string error;
    const auto parsed = planetary::config::parseJson(R"({"label":"\u7ec8\u7aef"})", &error);
    expectTrue(parsed.has_value(), "unicode escape should parse");
    const auto* label = parsed->find("label");
    expectTrue(label != nullptr && std::get<std::string>(label->value) == "终端",
               "unicode escape should become UTF-8");
    const std::string serialized = planetary::config::serializeJson(*parsed);
    const auto reparsed = planetary::config::parseJson(serialized, &error);
    expectTrue(reparsed.has_value(), "serialized JSON should parse again");
}

void testJsonRejectsTrailingData() {
    std::string error;
    const auto parsed = planetary::config::parseJson("{}{}", &error);
    expectTrue(!parsed.has_value(), "trailing JSON data should be rejected");
}

} // namespace

int main() {
    try {
        testJsonParsesChineseConfig();
        testJsonSupportsUnicodeEscapeAndRoundTrip();
        testJsonRejectsTrailingData();
        std::cout << "All configuration tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Configuration test failure: " << error.what() << '\n';
        return 1;
    }
}
