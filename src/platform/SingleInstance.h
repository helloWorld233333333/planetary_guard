#pragma once

#include <windows.h>

namespace planetary::platform {

/**
 * 进程级单实例守卫。
 *
 * 使用 Local 命名空间的互斥体，不需要管理员权限；第二次启动时由
 * 调用方安全退出，后续可以在这里增加激活已有窗口的逻辑。
 */
class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const wchar_t* mutexName);
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    bool anotherInstanceRunning() const;

private:
    HANDLE mutex_ = nullptr;
    bool anotherInstanceRunning_ = false;
};

} // namespace planetary::platform
