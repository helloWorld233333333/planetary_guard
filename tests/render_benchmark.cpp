#include "platform/DockRenderer.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

/** 独立隐藏窗口上的绘制测试，不注入用户输入、不读取桌面内容。 */
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"STATIC", L"", WS_POPUP, 0, 0, 1300, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    {
        planetary::platform::DockRenderer renderer;
        if (!window || !renderer.initialize(window)) return 1;
        planetary::layout::LayoutEngine engine(54, 8, 1.4F);
        std::vector<planetary::domain::DockItem> items;
        for (int i = 0; i < 16; ++i) {
            items.push_back({std::to_string(i), planetary::domain::DockItemType::Application, L"App", L"app.exe"});
            planetary::platform::IconPixels icon;
            icon.itemId = items.back().id;
            icon.size = 128;
            icon.bgra.resize(128U * 128U * 4U, 220);
            for (std::size_t p = 3; p < icon.bgra.size(); p += 4) icon.bgra[p] = 255;
            renderer.setIconPixels(icon);
        }
        const auto base = engine.calculate(items, -1, 1);
        SetWindowPos(window, nullptr, 0, 0, static_cast<int>(base.width), static_cast<int>(base.height), SWP_NOACTIVATE | SWP_NOZORDER);
        renderer.resize(static_cast<UINT>(base.width), static_cast<UINT>(base.height));
        std::vector<double> times;
        for (int i = 0; i < 360; ++i) {
            const auto begin = std::chrono::steady_clock::now();
            const auto frame = engine.calculate(items, base.width * (i % 180) / 180.0F, 1);
            renderer.render(frame, items);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            if (i >= 30) times.push_back(ms);
        }
        std::sort(times.begin(), times.end());
        std::cout << "16 synthetic icons, hidden layered window, warmed samples=" << times.size()
                  << ", median_ms=" << times[times.size()/2]
                  << ", p95_ms=" << times[times.size()*95/100]
                  << ", max_ms=" << times.back() << '\n';
    }
    DestroyWindow(window);
    CoUninitialize();
    return result;
}
