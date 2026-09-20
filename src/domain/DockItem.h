#pragma once

#include <string>

namespace planetary::domain {

/**
 * Dock 中条目的来源类型。
 *
 * 早期版本只会启动应用程序，但保留这些类型可以让配置文件和后续
 * 的文件夹、网址快捷方式保持向后兼容，而不需要重新设计数据模型。
 */
enum class DockItemType {
    Application,
    Shortcut,
    File,
    Folder,
    Url,
    Unknown
};

/**
 * Dock 条目的领域模型。
 */
struct DockItem {
    /** 稳定的配置项 ID。 */
    std::string id;
    /** 条目类型。 */
    DockItemType type = DockItemType::Unknown;
    /** 面向用户展示的名称。 */
    std::wstring displayName;
    /** 应用、文件、文件夹或 URL 的目标路径。 */
    std::wstring targetPath;
    /** 启动参数，禁止由调用方直接拼接到路径中。 */
    std::wstring arguments;
    /** 进程工作目录，可为空。 */
    std::wstring workingDirectory;
    /** 可选自定义图标路径。 */
    std::wstring customIconPath;
    /** 配置中的排序值。 */
    int order = 0;
    /** 是否在 Dock 中显示。 */
    bool enabled = true;
    /** 当前目标程序是否正在运行；仅为运行时状态，不写入配置。 */
    bool running = false;
    /** 临时运行条目，不写入用户配置。 */
    bool transient = false;
};

} // namespace planetary::domain
