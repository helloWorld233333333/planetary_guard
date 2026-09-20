#pragma once

#include <cstdint>
#include <string>

namespace planetary::domain {

/** Dock 外观主题。 */
enum class ThemeMode {
    FollowSystem,
    Light,
    Dark,
    HighContrast
};

/** 外观相关设置。 */
struct AppearanceSettings {
    /** 视觉默认值迁移版本，不影响配置格式主版本。 */
    std::uint32_t visualRevision = 4U;
    ThemeMode themeMode = ThemeMode::FollowSystem;
    float iconSizeDip = 54.0F;
    float maxIconSizeDip = 74.0F;
    float dockOpacity = 0.72F;
    /** 将软件原图标统一放入 Mac 风格圆角底座。 */
    bool unifiedIconTiles = false;
    bool reduceMotion = false;
};

/** 行为相关设置。 */
struct BehaviorSettings {
    bool autoHide = false;
    std::uint32_t hideDelayMs = 600U;
    std::uint32_t showDelayMs = 300U;
    bool hideInFullscreen = true;
    /** 桌面获得前台时保持 Dock 可见；手动隐藏仍优先。 */
    bool showOnDesktop = false;
    bool launchAtStartup = false;
};

/** 位置相关设置。 */
struct PlacementSettings {
    std::wstring monitorId;
    float bottomMarginDip = 12.0F;
};

/** 版本化的本地应用设置。 */
struct AppSettings {
    std::uint32_t schemaVersion = 1U;
    AppearanceSettings appearance;
    BehaviorSettings behavior;
    PlacementSettings placement;
};

} // namespace planetary::domain
