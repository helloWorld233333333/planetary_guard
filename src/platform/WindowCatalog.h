#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace planetary::platform {
/** 可切换的应用窗口；路径用于精确匹配，句柄在使用前必须重新验证。 */
struct ApplicationWindow {
    HWND handle = nullptr;
    DWORD processId = 0;
    std::wstring executable;
    std::wstring title;
};
/** 本地可见窗口快照，排除工具窗口、隐藏窗口和 DWM 隐匿窗口。 */
std::vector<ApplicationWindow> enumerateApplicationWindows();
/** 解析 EXE 和快捷方式到规范化绝对路径，不执行目标。 */
std::wstring applicationIdentity(const std::wstring& target);
/** 激活仍属于快照进程的窗口，保留应用自身的未保存内容提示。 */
bool activateApplicationWindow(const ApplicationWindow& window);
}
