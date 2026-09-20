#pragma once

#include "domain/DockItem.h"

#include <filesystem>
#include <string>
#include <vector>

namespace planetary::config {

/**
 * 本地 Dock 项目存储。
 *
 * 配置文件位于 %LOCALAPPDATA%\\PlanetaryGuard，保存采用临时文件 +
 * 备份 + 原子替换，读取失败时会尝试 .bak，不会把损坏配置写回正式文件。
 */
class DockItemStore {
public:
    DockItemStore();
    explicit DockItemStore(std::filesystem::path filePath);

    /** 读取正式配置，失败时自动尝试同名 .bak。 */
    bool load(std::vector<domain::DockItem>& items, std::wstring* errorMessage) const;

    /** 将项目列表以 schemaVersion=1 写入本地 JSON。 */
    bool save(const std::vector<domain::DockItem>& items, std::wstring* errorMessage) const;

    const std::filesystem::path& filePath() const;

private:
    bool loadFile(const std::filesystem::path& path,
                  std::vector<domain::DockItem>& items,
                  std::string* errorMessage) const;

    static std::filesystem::path defaultFilePath();

    std::filesystem::path filePath_;
};

} // namespace planetary::config
