
# Planetary Guard

一个面向 Windows 10/11 的轻量 Mac 风格底部悬浮 Dock 原型。

当前切片已经实现：

0.1.5：底板改为蓝紫—冰青极光玻璃，阴影不再以黑色实心层压暗整块底板；新增运行应用导致底板扩容时强制刷新圆角裁切。成功启动/切换应用后收起 Dock，鼠标移到屏幕工作区底边可重新唤出（不改变原有“鼠标离开自动隐藏”设置）；取消窗口菜单或启动失败不会收起。保留固定画布和事件合并优化。

0.1.4：动画移到固定透明画布，底板和系统模糊窗口在悬停时不再伸缩；合并鼠标移动事件，每帧最多一次布局计算；悬停期间延后运行窗口枚举。玻璃底板采用细双层边缘高光及更柔和阴影。新增跨 DPI 连续扫过边界测试和 `planetary_render_benchmark`（隐藏窗口、16 个模拟图标，仅测布局和绘制，不代表桌面帧率）。

0.1.3 修复：材质层改用无文本、无背景画刷的专用窗口，去掉误绘制的 Material 字样和矩形灰底；浅色底板更淡，模糊改为系统 BlurBehind。悬停动画复用按容量增长的 DIB 和画刷，减少窗口重复定位；图标缩放改为预乘 Alpha 双线性采样。帧率与实际画面仍需桌面截图及交互验收。

0.1.2 新增：按完整程序路径归并可见窗口；临时运行应用自动出现和退出；右键固定；多个窗口点击选择；文件夹最多 40 项的本地内容菜单；右键打开/清空回收站（保留系统确认）。临时应用不写入配置。系统模糊与图标使用独立窗口，失败时保留半透明底板。原生 C++ 安装器替代普通自解压包。

验证范围：自动测试、进程启动、安装器文件提取。真实桌面截图及 Windows 10 实机回归尚未完成。文件夹当前为列表菜单，不是 macOS 扇形/网格堆栈；打包应用的宿主进程识别仍有限制。

构建建议：使用独立 Release 目录并执行 `cmake --build <目录> --clean-first`，避免本机中文 MSVC 依赖前缀编码导致增量构建漏掉头文件变化。完成后执行 `packaging/build-package.ps1 -BuildDirectory <目录>`。安装器支持 `--extract-only <绝对目录>`，仅提取文件用于验证，不注册开始菜单和卸载信息。

- 原生 Win32 弹出窗口，不创建任务栏按钮、不主动抢焦点。
- Direct2D 在 32 位内存画布上绘制，通过 `UpdateLayeredWindow` 逐像素合成：底板半透明，图标保持完全清晰。
- 紧凑圆角底板使用系统强调色染色的纵向渐变、轻量阴影、顶部高光和 Windows Acrylic 模糊回退。
- 图标占据主视觉，取消独立卡片和块状阴影；默认 54 DIP，悬停最大 74 DIP。
- 优先从 Windows Shell Jumbo 图标列表读取最高 256px 的软件自带图标，支持 `.exe`、`.lnk`、文件和文件夹。
- 根据图标 Alpha 边界自动裁切、等比缩放和居中，降低 Windows 图标大小不一带来的凌乱感。
- Shell 图标提取在后台线程执行，UI 线程只创建 Direct2D 位图。
- 鼠标悬停时的平滑图标放大与自动布局。
- 点击后通过 `ShellExecuteExW` 启动记事本、文件管理器和终端。
- 路径、参数、工作目录分开传递，不拼接不可信命令行。
- 右键选择“添加应用…”，可从开始菜单程序目录一次选择多个已有软件或快捷方式。
- 拖入文件或文件夹添加项目；右键添加文件、打开和移除项目。
- 拖动项目调整顺序，配置保存到本地 JSON 并保留 `.bak` 备份。
- 本地版本化设置保存，范围校验和 `.bak` 恢复。
- 自动隐藏状态机、底部边缘唤出和托盘菜单。
- 使用 WinEventHook 监听前台窗口，全屏游戏/视频时自动避让 Dock。
- 标准 Win32 设置窗口：自动隐藏、全屏隐藏、开机启动、图标大小和放大尺寸。
- 设置窗口支持目标显示器、跟随系统/浅色/深色/高对比度主题、Dock 不透明度和减少动画。
- 右键为条目选择 `.ico/.exe/.dll` 自定义图标，也可以随时恢复默认 Shell 图标；无效路径自动回退目标图标。
- 当前用户开机启动开关，不要求管理员权限。
- 保存目标显示器设备 ID；显示器热插拔或系统恢复后自动重新定位，目标不可用时回退到当前显示器。
- 布局、配置、设置和自动隐藏状态机的独立回归测试。
- PerMonitorV2 清单和单实例互斥体。

## 构建环境

项目固定使用 C++20，不会在旧编译器上静默降级。当前已验证的工具链是 Visual Studio
2026 Community Insiders（MSVC 19.51.36257、CMake 4.3.1），并且已安装 Desktop C++
工作负载。Visual Studio 2017 Build Tools 仍可能存在于机器上，但仅作为旧工具链，不能
满足本项目的 C++20 构建要求。Windows 10 SDK 10.0.17763 可用于构建 Windows 10/11 版本。

在 Visual Studio 2026 Developer PowerShell（x64）中执行：

```powershell
Set-Location D:\WorkSpace\voice\planetary_guard

cmake -S . -B build-cpp20 -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpp20
ctest --test-dir build-cpp20 --output-on-failure
```

生成的程序位于 `build-cpp20\planetary_guard.exe`。使用 Ninja 是因为当前 CMake
版本对预览版 Visual Studio 实例的 Visual Studio generator 探测不稳定；这不影响
MSVC/C++20 编译能力。

## 发布与安装

在已经配置好 Release 构建后，可以生成免安装便携包和当前用户安装包：

```powershell
Set-Location D:\WorkSpace\voice\planetary_guard
powershell -ExecutionPolicy Bypass -File .\packaging\build-package.ps1
```

产物位于 `dist`：`*-portable.zip` 可以直接解压运行，`*-setup.exe` 是 7-Zip
自解压安装包，会安装到 `%LOCALAPPDATA%\Programs\PlanetaryGuard`，创建开始菜单和卸载快捷方式，
并登记到 Windows“已安装的应用”，不需要管理员权限。卸载会保留
`%LOCALAPPDATA%\PlanetaryGuard` 下的用户配置。

## 代码结构

- `src/config`：无第三方依赖的 JSON 解析与原子配置存储。
- `src/domain`：Dock 条目领域模型。
- `src/layout`：DPI、间距、悬停放大的纯逻辑布局核心。
- `src/dock`：自动隐藏等不依赖 Win32 的 Dock 交互状态机。
- `src/platform`：Win32 窗口、Direct2D 渲染、托盘、启动项和 Shell 启动器。
- `tests`：不依赖第三方框架的核心回归测试。

配置文件默认位于 `%LOCALAPPDATA%\PlanetaryGuard\dock-items.json`，设置文件为同目录下的
`settings.json`，写入时均保留同名 `.bak`。

## 已知限制

- 安装包暂未进行代码签名，Windows SmartScreen 可能显示未知发布者提示。
- 设置窗口使用原生 Win32 控件；极端缩放比例和特殊辅助主题仍建议在目标机器上实测。
