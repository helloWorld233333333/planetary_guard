#pragma once

#include "domain/AppSettings.h"

#include <filesystem>
#include <string>

namespace planetary::config {

/** 本地设置存储，采用 schemaVersion=1 和临时文件/备份原子替换。 */
class SettingsStore {
public:
    SettingsStore();
    explicit SettingsStore(std::filesystem::path filePath);

    /** 读取正式设置，失败时自动尝试同名 .bak。 */
    bool load(domain::AppSettings& settings, std::wstring* errorMessage) const;

    /** 保存设置并保留上一份 .bak。 */
    bool save(const domain::AppSettings& settings, std::wstring* errorMessage) const;

    const std::filesystem::path& filePath() const;

private:
    bool loadFile(const std::filesystem::path& path,
                  domain::AppSettings& settings,
                  std::string* errorMessage) const;
    static std::filesystem::path defaultFilePath();

    std::filesystem::path filePath_;
};

} // namespace planetary::config
