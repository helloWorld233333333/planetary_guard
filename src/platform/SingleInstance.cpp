#include "platform/SingleInstance.h"

namespace planetary::platform {

SingleInstanceGuard::SingleInstanceGuard(const wchar_t* mutexName) {
    mutex_ = CreateMutexW(nullptr, TRUE, mutexName);
    anotherInstanceRunning_ = mutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (mutex_ != nullptr) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
}

bool SingleInstanceGuard::anotherInstanceRunning() const {
    return anotherInstanceRunning_;
}

} // namespace planetary::platform
