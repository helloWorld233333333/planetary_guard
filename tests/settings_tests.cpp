#include "config/SettingsStore.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testSettingsRoundTripAndBackupRecovery() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"PlanetaryGuardSettingsTest-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);

    planetary::config::SettingsStore store(directory / L"settings.json");
    planetary::domain::AppSettings original;
    original.appearance.themeMode = planetary::domain::ThemeMode::Dark;
    original.appearance.iconSizeDip = 52.0F;
    original.appearance.dockOpacity = 0.72F;
    original.behavior.autoHide = true;
    original.behavior.showOnDesktop = true;
    original.behavior.hideDelayMs = 900U;
    original.behavior.showDelayMs = 80U;
    original.behavior.launchAtStartup = true;
    original.placement.monitorId = L"DISPLAY-TEST";

    std::wstring error;
    expectTrue(store.save(original, &error), "settings should save");

    planetary::domain::AppSettings loaded;
    expectTrue(store.load(loaded, &error), "settings should load");
    expectTrue(loaded.behavior.showOnDesktop, "desktop visibility option should round-trip");
    expectTrue(loaded.behavior.showDelayMs == 300U, "legacy short hover delay must clamp to 300ms");
    expectTrue(loaded.appearance.themeMode == planetary::domain::ThemeMode::Dark,
               "theme should round-trip");
    expectTrue(loaded.appearance.visualRevision == 4U,
               "visual revision should round-trip");
    expectTrue(!loaded.appearance.unifiedIconTiles,
               "unified icon presentation should round-trip");
    expectTrue(loaded.behavior.autoHide && loaded.behavior.launchAtStartup,
               "behavior flags should round-trip");
    expectTrue(loaded.placement.monitorId == L"DISPLAY-TEST",
               "monitor id should round-trip");

    original.behavior.autoHide = false;
    original.behavior.showOnDesktop = false;
    expectTrue(store.save(original, &error), "second settings save should succeed");
    expectTrue(store.load(loaded, &error) && !loaded.behavior.showOnDesktop,
               "desktop option can be disabled and saved");
    {
        std::ofstream broken(store.filePath(), std::ios::binary | std::ios::trunc);
        broken << "{broken";
    }
    loaded = {};
    expectTrue(store.load(loaded, &error), "corrupt settings should recover from backup");
    expectTrue(loaded.behavior.autoHide, "backup should contain the previous settings");

    std::filesystem::remove_all(directory, cleanupError);
}

void testSettingsClampUnsafeValues() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"PlanetaryGuardSettingsClampTest-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    std::filesystem::create_directories(directory, cleanupError);
    {
        std::ofstream output(directory / L"settings.json", std::ios::binary | std::ios::trunc);
        output << R"({"schemaVersion":1,"appearance":{"iconSizeDip":1,"maxIconSizeDip":200,"dockOpacity":4},"behavior":{"hideDelayMs":999999}})";
    }

    planetary::config::SettingsStore store(directory / L"settings.json");
    planetary::domain::AppSettings settings;
    std::wstring error;
    expectTrue(store.load(settings, &error), "valid but out-of-range values should load");
    expectTrue(settings.appearance.iconSizeDip == 24.0F,
               "icon size should be clamped to minimum");
    expectTrue(settings.appearance.maxIconSizeDip == 96.0F,
               "maximum icon size should be clamped");
    expectTrue(settings.appearance.dockOpacity == 1.0F,
               "opacity should be clamped");
    expectTrue(settings.appearance.visualRevision == 0U,
               "legacy settings without a visual revision should remain detectable");
    expectTrue(settings.behavior.hideDelayMs == 10000U,
               "hide delay should be clamped");

    std::filesystem::remove_all(directory, cleanupError);
}

} // namespace

int main() {
    try {
        testSettingsRoundTripAndBackupRecovery();
        testSettingsClampUnsafeValues();
        std::cout << "All settings tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Settings test failure: " << error.what() << '\n';
        return 1;
    }
}
