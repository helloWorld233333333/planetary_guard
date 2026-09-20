#include "config/DockItemStore.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testStoreRoundTripAndBackupRecovery() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (L"PlanetaryGuardStoreTest-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);

    planetary::config::DockItemStore store(directory / L"dock-items.json");
    std::vector<planetary::domain::DockItem> original = {
        {"item-1",
         planetary::domain::DockItemType::File,
         L"说明.txt",
         L"C:\\说明.txt",
         L"--demo",
         L"C:\\",
         L"C:\\icons\\note.ico",
         10,
         true}
    };
    std::wstring error;
    auto temporary = original.front();
    temporary.id = "runtime-only";
    temporary.transient = true;
    original.push_back(temporary);
    expectTrue(store.save(original, &error), "store should save a valid item list");

    std::vector<planetary::domain::DockItem> loaded;
    expectTrue(store.load(loaded, &error), "store should load its saved item list");
    expectTrue(loaded.size() == 1U, "round trip should preserve item count");
    expectTrue(loaded.front().displayName == L"说明.txt", "round trip should preserve UTF-16 name");
    expectTrue(loaded.front().customIconPath == L"C:\\icons\\note.ico",
               "round trip should preserve custom icon path");

    original.front().displayName = L"新名称.txt";
    expectTrue(store.save(original, &error), "second save should succeed");
    {
        std::ofstream broken(store.filePath(), std::ios::binary | std::ios::trunc);
        broken << "{broken";
    }
    loaded.clear();
    expectTrue(store.load(loaded, &error), "corrupt primary file should recover from backup");
    expectTrue(loaded.front().displayName == L"说明.txt", "backup should contain the previous version");

    // 保存按容器顺序重建 order；名称、类型和旧 order 都不能覆盖拖拽顺序。
    auto folder = original.front();
    folder.id = "z-folder";
    folder.type = planetary::domain::DockItemType::Folder;
    folder.order = 500;
    auto app = original.front();
    app.id = "a-apifox";
    app.type = planetary::domain::DockItemType::Application;
    app.displayName = L"Apifox";
    app.order = -10;
    expectTrue(store.save({folder, temporary, app}, &error), "manual order should save");
    planetary::config::DockItemStore reopened(store.filePath());
    expectTrue(reopened.load(loaded, &error), "manual order should reload after restart");
    expectTrue(loaded.size() == 2 && loaded[0].id == "z-folder" && loaded[1].id == "a-apifox",
               "restart must preserve manual order and exclude transient entries");

    std::filesystem::remove_all(directory, cleanupError);
}

} // namespace

int main() {
    try {
        testStoreRoundTripAndBackupRecovery();
        std::cout << "All store tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Store test failure: " << error.what() << '\n';
        return 1;
    }
}
