#pragma once

#include <string>

namespace planetary::platform {

/**
 * 通过 ShellExecuteExW 启动 Dock 条目。
 *
 * 路径、参数和工作目录分开传递，避免拼接命令行导致引号和注入问题。
 */
class ShellLauncher {
public:
    /**
     * 启动一个可执行文件、文件夹、文件或 URL。
     *
     * @param targetPath 目标路径或 URL。
     * @param arguments 独立的启动参数。
     * @param workingDirectory 可选工作目录。
     * @param errorMessage 失败时写入可读错误信息，可为空。
     */
    bool launch(const std::wstring& targetPath,
                const std::wstring& arguments,
                const std::wstring& workingDirectory,
                std::wstring* errorMessage) const;
};

} // namespace planetary::platform
