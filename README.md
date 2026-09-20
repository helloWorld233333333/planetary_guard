# Planetary Guard

一个为 Windows 10 / 11 制作的轻量级 macOS 风格 Dock。它使用原生 C++20 与 Win32 实现，不依赖浏览器运行时、云端服务或账号；你的应用列表和设置只保存在本机。

> 项目仍在快速迭代中，欢迎通过 Issue 反馈 Bug、体验问题和功能建议。

## 功能

- 底部悬浮、玻璃质感的 macOS 风格程序坞。
- 图标悬停放大、提示名称与平滑动画。
- 直接拖入 `.exe`、快捷方式、文件或文件夹；也可以通过右键菜单添加。
- 优先读取软件自身的 Windows Shell 图标，并支持为单个项目指定自定义图标。
- 拖拽调整顺序；顺序会保存在本地，刷新运行状态或重启后不会被重排。
- 已固定的应用显示运行状态；未固定但正在运行的应用会临时出现，退出后自动移除。
- 点击图标启动或切换应用；点击 Dock 外部的应用窗口会自动收起。
- 可选自动隐藏、全屏隐藏、开机启动、减少动画、图标大小和透明度。
- 多显示器支持：鼠标在任一显示器底边中间区域停留至少 300 ms，Dock 会迁移到那块屏幕。
- 支持自动隐藏的 Windows 任务栏，不修改任务栏或系统工作区设置。

## 安装

前往本仓库的 [Releases](../../releases) 页面下载最新版本：

- `PlanetaryGuard-*-win-x64-setup.exe`：推荐。当前用户安装包，不需要管理员权限。
- `PlanetaryGuard-*-win-x64-portable.zip`：便携版，解压后运行 `planetary_guard.exe`。

安装后从开始菜单启动 Planetary Guard。程序不会替换 Windows 任务栏；它是一个独立的悬浮 Dock。

> 目前安装包尚未签名。首次运行时 Windows SmartScreen 可能提示“未知发布者”，请只从本仓库的 Releases 下载。

## 使用方法

### 添加与整理项目

1. 将应用、快捷方式、文件或文件夹直接拖到 Dock；或右键 Dock，选择“添加应用”或“添加文件”。
2. 按住图标并左右拖动，即可调整位置。
3. 右键图标可以打开、移除、固定临时运行应用、选择自定义图标，或恢复软件原本图标。

固定项目的顺序会持久保存。临时出现的运行应用只在运行期间显示；想让它长期保留，请右键选择固定。

### 隐藏与唤出

- 从托盘菜单可手动显示或隐藏 Dock、打开设置或退出程序。
- 关闭 Dock 后，把鼠标移到当前显示器屏幕底部、Dock 大致宽度范围内，连续停留至少 300 ms 即可唤出。
- 在多显示器环境中，在另一块屏幕按同样方式触底，Dock 会移动到该屏幕。
- 拖动鼠标或按住鼠标按钮经过底边不会唤出，避免影响拖拽和输入操作。

### 设置

右键 Dock 或托盘图标并打开“设置”，可以调整：

- 自动隐藏、全屏时隐藏、开机启动；
- 图标大小、悬停放大尺寸、Dock 透明度、减少动画；
- 主题与底部边距。

## 系统要求

- Windows 10 或 Windows 11，64 位。
- 建议开启桌面合成效果以获得更好的玻璃背景。
- 不需要联网，也不会上传、同步或存储你的数据到云端。

配置保存在 `%LOCALAPPDATA%\\PlanetaryGuard\\`：

- `dock-items.json`：固定项目及其顺序；
- `settings.json`：外观和行为设置；
- 每次写入都会保留 `.bak` 备份，以便配置损坏时自动恢复。

卸载程序不会自动删除这些配置；如需完全重置，可在退出 Dock 后手动删除该目录。

## 已知限制

- 这是 Dock，不是 Windows 任务栏替代品；开始菜单、通知区域和系统任务栏仍由 Windows 管理。
- 混合 DPI、多显示器热插拔和 Windows 10 的不同任务栏布局仍需要更多实机验证。
- 文件夹目前使用原生列表菜单，不是 macOS 的网格堆栈效果。
- UWP / 打包应用的宿主进程识别在部分情况下可能不完整。

## 从源码构建

项目使用 C++20、CMake 和原生 Win32 API。需要安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载、Windows SDK 和 CMake/Ninja。

在 **x64 Developer PowerShell** 中执行：

```powershell
git clone <你的仓库地址>
Set-Location planetary_guard

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

生成的程序为 `build\\planetary_guard.exe`。

构建安装包：

```powershell
powershell -ExecutionPolicy Bypass -File .\\packaging\\build-package.ps1 -BuildDirectory .\\build
```

产物会写入 `dist\\`。安装器支持仅解压校验：

```powershell
.\\dist\\PlanetaryGuard-*-win-x64-setup.exe --extract-only C:\\Temp\\PlanetaryGuard-check
```

## 参与贡献

欢迎提交 Issue 和 Pull Request。提交前请至少完成 Release 构建与测试：

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

## 许可证

[MIT License](LICENSE)
