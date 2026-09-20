#include "platform/WindowCatalog.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using planetary::platform::applicationIdentity;
        if (!applicationIdentity(L"https://example.com").empty()) throw std::runtime_error("URLs must not become application identities");
        if (!applicationIdentity(L"C:\\work\\notes.txt").empty()) throw std::runtime_error("Documents must not match running apps");
        if (applicationIdentity(L"C:\\Tools\\APP.EXE") != applicationIdentity(L"c:\\tools\\app.exe"))
            throw std::runtime_error("Executable matching should ignore Windows path case");
        if (applicationIdentity(L"C:\\One\\app.exe") == applicationIdentity(L"C:\\Two\\app.exe"))
            throw std::runtime_error("Same file name in different folders must not match");
        if (planetary::platform::activateApplicationWindow({})) throw std::runtime_error("Stale handles must not activate");
        std::cout << "Window identity tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what();
        return 1;
    }
}
