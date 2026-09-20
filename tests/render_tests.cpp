#include "platform/DockRenderer.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace planetary::platform {
/** 逐行读取逻辑画布，排除为减少分配而预留的 DIB 容量。 */
struct DockRendererTestAccess {
    static std::vector<unsigned char> pixels(const DockRenderer& renderer) {
        std::vector<unsigned char> result(renderer.width_ * renderer.height_ * 4U);
        const auto* source = static_cast<const unsigned char*>(renderer.surfacePixels_);
        for (UINT row = 0; row < renderer.height_; ++row)
            std::memcpy(result.data() + row * renderer.width_ * 4U,
                        source + row * renderer.surfaceWidth_ * 4U, renderer.width_ * 4U);
        return result;
    }
};
}

/** 在自有隐藏窗口验证增减图标后的离屏渲染，无用户输入或桌面截图。 */
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"STATIC", L"",
        WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        planetary::platform::DockRenderer reused;
        if (!window || !reused.initialize(window)) throw std::runtime_error("renderer init failed");
        planetary::layout::LayoutEngine engine(54, 8, 1.4F);
        // 交替跨越 DIB 的 256px 分配边界，覆盖扩容、复用以及缩小。
        for (int count : {3, 14, 15, 16, 2, 20, 14}) {
            std::vector<planetary::domain::DockItem> items;
            for (int i = 0; i < count; ++i)
                items.push_back({std::to_string(i), planetary::domain::DockItemType::Application, L"A", L"a.exe"});
            const auto frame = engine.calculate(items, -1, 1);
            const UINT width = static_cast<UINT>(frame.width);
            const UINT height = static_cast<UINT>(frame.height);
            reused.resize(width, height);
            reused.render(frame, items);
            const auto actual = planetary::platform::DockRendererTestAccess::pixels(reused);
            planetary::platform::DockRenderer fresh;
            if (!fresh.initialize(window)) throw std::runtime_error("fresh renderer init failed");
            fresh.resize(width, height);
            fresh.render(frame, items);
            const auto expected = planetary::platform::DockRendererTestAccess::pixels(fresh);
            if (actual != expected) throw std::runtime_error("resize retained stale pixels");
            // 底板上缘下方、图标上方必须有彩色像素，而非中性灰。
            const UINT x = static_cast<UINT>(frame.panelLeft + frame.panelWidth * 0.3F);
            const UINT y = static_cast<UINT>(frame.height - frame.panelHeight + 4);
            const auto offset = (y * width + x) * 4U;
            if (actual[offset + 3] == 0 || actual[offset] <= actual[offset + 2] + 10)
                throw std::runtime_error("glass should have a visible cool tint");
        }
        std::cout << "Render resize and colored glass regression tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (window) DestroyWindow(window);
    CoUninitialize();
    return result;
}
