#pragma once

#include "domain/DockItem.h"
#include "domain/AppSettings.h"
#include "layout/LayoutEngine.h"
#include "platform/IconLoader.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace planetary::platform {

/**
 * Dock 的 Direct2D 渲染器。
 *
 * 使用系统 Direct2D 绘制轻量、高 DPI 的悬浮 Dock，不引入第三方 UI 库。
 */
class DockRenderer {
public:
    ~DockRenderer();

    bool initialize(HWND hwnd);
    void resize(UINT width, UINT height);
    void applyAppearance(const domain::AppearanceSettings& appearance);
    void setIconPixels(const IconPixels& pixels);
    void clearIcon(const std::string& itemId);
    void render(const layout::LayoutSnapshot& snapshot,
                const std::vector<domain::DockItem>& items);
    bool consumeDeviceReset();

private:
    // 回归测试只读取本渲染器的离屏像素，不读取用户桌面。
    friend struct DockRendererTestAccess;
    bool createRenderTarget();
    bool createSurface();
    void releaseSurface();
    bool present();
    void createBrushes();
    ID2D1Bitmap* iconFor(const domain::DockItem& item);
    bool isDarkAppearance() const;

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> glassSheenBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> topSheenBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tileBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tileBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> indicatorBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    UINT width_ = 0U;
    UINT height_ = 0U;
    UINT surfaceWidth_ = 0U;
    UINT surfaceHeight_ = 0U;
    bool darkAppearance_ = false;
    HDC memoryDc_ = nullptr;
    HBITMAP surfaceBitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    void* surfacePixels_ = nullptr;
    domain::AppearanceSettings appearance_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>> iconCache_;
    std::unordered_map<std::string, std::uint32_t> accentColors_;
    bool deviceReset_ = false;
};

} // namespace planetary::platform
