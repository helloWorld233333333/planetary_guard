#include "platform/DockRenderer.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <utility>

namespace planetary::platform {

namespace {

// 程序坞的视觉重心应该是应用图标，底板只做低对比度承托。
// 窗口级透明度会让下方壁纸色彩自然混入这层中性表面。
const D2D1_COLOR_F kDarkBackgroundColor = D2D1::ColorF(0x363546, 1.0F);
const D2D1_COLOR_F kDarkTextColor = D2D1::ColorF(0xF7F8FA, 1.0F);
const D2D1_COLOR_F kLightBackgroundColor = D2D1::ColorF(0xECECF2, 1.0F);
const D2D1_COLOR_F kLightTextColor = D2D1::ColorF(0x202124, 1.0F);
const D2D1_COLOR_F kIconColors[] = {
    D2D1::ColorF(0x4F6BED, 1.0F),
    D2D1::ColorF(0x3BB273, 1.0F),
    D2D1::ColorF(0xF2A93B, 1.0F),
    D2D1::ColorF(0xD95D8A, 1.0F),
};

D2D1_COLOR_F colorFromSystem(int index) {
    const COLORREF color = GetSysColor(index);
    return D2D1::ColorF(GetRValue(color) / 255.0F,
                        GetGValue(color) / 255.0F,
                        GetBValue(color) / 255.0F,
                        1.0F);
}

D2D1_COLOR_F tintedSurface(bool dark) {
    const D2D1_COLOR_F base = dark ? kDarkBackgroundColor : kLightBackgroundColor;
    DWORD colorization = 0U;
    BOOL opaqueBlend = FALSE;
    if (FAILED(DwmGetColorizationColor(&colorization, &opaqueBlend))) {
        return base;
    }
    const float accentRed = static_cast<float>((colorization >> 16U) & 0xFFU) / 255.0F;
    const float accentGreen = static_cast<float>((colorization >> 8U) & 0xFFU) / 255.0F;
    const float accentBlue = static_cast<float>(colorization & 0xFFU) / 255.0F;
    const float accentWeight = dark ? 0.12F : 0.08F;
    return D2D1::ColorF(base.r * (1.0F - accentWeight) + accentRed * accentWeight,
                        base.g * (1.0F - accentWeight) + accentGreen * accentWeight,
                        base.b * (1.0F - accentWeight) + accentBlue * accentWeight,
                        1.0F);
}

D2D1_COLOR_F tileColor(std::uint32_t rgb, std::size_t index, bool dark) {
    if (rgb == 0U) {
        return kIconColors[index % std::size(kIconColors)];
    }
    float red = static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F;
    float green = static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F;
    float blue = static_cast<float>(rgb & 0xFFU) / 255.0F;
    const float maximum = std::max({red, green, blue});
    const float minimum = std::min({red, green, blue});
    const float saturation = maximum <= 0.001F ? 0.0F : (maximum - minimum) / maximum;
    const float luminance = 0.299F * red + 0.587F * green + 0.114F * blue;
    if (luminance < 0.13F) {
        return D2D1::ColorF(0x30333A, 1.0F);
    }
    if (saturation < 0.10F) {
        return kIconColors[index % std::size(kIconColors)];
    }
    // 原图主色只作为底板色相来源，适当提亮后保持一组图标的清透感。
    const float whiteMix = dark ? 0.20F : 0.12F;
    red = red * (1.0F - whiteMix) + whiteMix;
    green = green * (1.0F - whiteMix) + whiteMix;
    blue = blue * (1.0F - whiteMix) + whiteMix;
    return D2D1::ColorF(red, green, blue, 1.0F);
}

} // namespace

DockRenderer::~DockRenderer() {
    releaseSurface();
}

bool DockRenderer::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    if (hwnd_ == nullptr) {
        return false;
    }

    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       d2dFactory_.GetAddressOf());
    if (FAILED(result)) {
        return false;
    }

    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                 __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (FAILED(result)) {
        return false;
    }

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    width_ = static_cast<UINT>(std::max<LONG>(1L, clientRect.right - clientRect.left));
    height_ = static_cast<UINT>(std::max<LONG>(1L, clientRect.bottom - clientRect.top));

    result = writeFactory_->CreateTextFormat(L"Microsoft YaHei UI",
                                              nullptr,
                                              DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                              DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL,
                                              15.0F,
                                              L"zh-CN",
                                              textFormat_.GetAddressOf());
    if (FAILED(result)) {
        return false;
    }
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (!createRenderTarget()) {
        return false;
    }
    if (!createSurface()) {
        return false;
    }
    createBrushes();
    return backgroundBrush_ != nullptr && iconBrush_ != nullptr && textBrush_ != nullptr;
}

void DockRenderer::applyAppearance(const domain::AppearanceSettings& appearance) {
    appearance_ = appearance;
    if (renderTarget_ != nullptr) {
        createBrushes();
    }
}

bool DockRenderer::createRenderTarget() {
    if (d2dFactory_ == nullptr || hwnd_ == nullptr) {
        return false;
    }
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            // DC 目标直接绘制到 32 位 DIB，再由 UpdateLayeredWindow
            // 做逐像素 Alpha 合成。这样底板可半透明而图标仍然清晰。
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0F,
            96.0F,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT);
    const HRESULT result = d2dFactory_->CreateDCRenderTarget(&properties,
                                                              renderTarget_.GetAddressOf());
    return SUCCEEDED(result);
}

bool DockRenderer::createSurface() {
    if (memoryDc_ != nullptr && width_ <= surfaceWidth_ && height_ <= surfaceHeight_) return true;
    const UINT capacityWidth = ((std::max(width_, surfaceWidth_) + 255U) / 256U) * 256U;
    const UINT capacityHeight = ((std::max(height_, surfaceHeight_) + 127U) / 128U) * 128U;
    releaseSurface();
    if (width_ == 0U || height_ == 0U) return false;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(capacityWidth);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(capacityHeight);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    memoryDc_ = screenDc == nullptr ? nullptr : CreateCompatibleDC(screenDc);
    surfaceBitmap_ = memoryDc_ == nullptr
                         ? nullptr
                         : CreateDIBSection(memoryDc_,
                                            &bitmapInfo,
                                            DIB_RGB_COLORS,
                                            &surfacePixels_,
                                            nullptr,
                                            0U);
    if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
    if (memoryDc_ == nullptr || surfaceBitmap_ == nullptr || surfacePixels_ == nullptr) {
        releaseSurface();
        return false;
    }
    previousBitmap_ = SelectObject(memoryDc_, surfaceBitmap_);
    surfaceWidth_ = capacityWidth;
    surfaceHeight_ = capacityHeight;
    std::memset(surfacePixels_, 0, static_cast<std::size_t>(surfaceWidth_) * surfaceHeight_ * 4U);
    return true;
}

void DockRenderer::releaseSurface() {
    if (memoryDc_ != nullptr && previousBitmap_ != nullptr) {
        SelectObject(memoryDc_, previousBitmap_);
    }
    previousBitmap_ = nullptr;
    if (surfaceBitmap_ != nullptr) DeleteObject(surfaceBitmap_);
    surfaceBitmap_ = nullptr;
    surfacePixels_ = nullptr;
    if (memoryDc_ != nullptr) DeleteDC(memoryDc_);
    memoryDc_ = nullptr;
    surfaceWidth_ = 0U;
    surfaceHeight_ = 0U;
}

bool DockRenderer::present() {
    if (hwnd_ == nullptr || memoryDc_ == nullptr) return false;
    RECT windowRect{};
    if (GetWindowRect(hwnd_, &windowRect) == FALSE) return false;
    POINT destination{windowRect.left, windowRect.top};
    POINT source{0, 0};
    SIZE size{static_cast<LONG>(width_), static_cast<LONG>(height_)};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255U;
    blend.AlphaFormat = AC_SRC_ALPHA;
    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) return false;
    const BOOL updated = UpdateLayeredWindow(hwnd_,
                                             screenDc,
                                             &destination,
                                             &size,
                                             memoryDc_,
                                             &source,
                                             0U,
                                             &blend,
                                             ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
    return updated != FALSE;
}

void DockRenderer::createBrushes() {
    if (renderTarget_ == nullptr) {
        return;
    }
    const bool highContrast = appearance_.themeMode == domain::ThemeMode::HighContrast;
    const bool dark = isDarkAppearance();
    darkAppearance_ = dark;
    const D2D1_COLOR_F background = highContrast
                                        ? colorFromSystem(COLOR_WINDOW)
                                        : tintedSurface(dark);
    const D2D1_COLOR_F text = highContrast
                                  ? colorFromSystem(COLOR_WINDOWTEXT)
                                  : (dark ? kDarkTextColor : kLightTextColor);
    backgroundBrush_.Reset();
    glassSheenBrush_.Reset();
    borderBrush_.Reset();
    topSheenBrush_.Reset();
    shadowBrush_.Reset();
    tileBrush_.Reset();
    tileBorderBrush_.Reset();
    indicatorBrush_.Reset();
    textBrush_.Reset();
    iconBrush_.Reset();
    const float panelAlpha = highContrast
                                 ? 1.0F
                                 : std::clamp(appearance_.dockOpacity * 0.86F, 0.25F, 0.85F);
    // 极光玻璃：颜色沿长轴展开，图标仍然保持完全不透明的原生色彩。
    const auto glassColor = [&](UINT32 light, UINT32 night) {
        return highContrast ? background : D2D1::ColorF(dark ? night : light, panelAlpha);
    };
    const D2D1_GRADIENT_STOP stops[] = {
        D2D1::GradientStop(0.0F, glassColor(0xAF98ED, 0x4D377C)),
        D2D1::GradientStop(0.32F, glassColor(0x9AABF0, 0x344D88)),
        D2D1::GradientStop(0.68F, glassColor(0x87D1EC, 0x246C83)),
        D2D1::GradientStop(1.0F, glassColor(0xB4B1ED, 0x594677)),
    };
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
    if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
            stops,
            static_cast<UINT32>(std::size(stops)),
            D2D1_GAMMA_2_2,
            D2D1_EXTEND_MODE_CLAMP,
            stopCollection.GetAddressOf()))) {
        renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0.0F, 1.0F),
                D2D1::Point2F(0.0F, static_cast<float>(std::max(height_, 1U)))),
            stopCollection.Get(),
            backgroundBrush_.GetAddressOf());
    }
    const D2D1_GRADIENT_STOP sheenStops[] = {
        D2D1::GradientStop(0.0F, D2D1::ColorF(0xFFFFFF, highContrast ? 0.0F : (dark ? 0.16F : 0.36F))),
        D2D1::GradientStop(0.42F, D2D1::ColorF(0xFFFFFF, 0.015F)),
        D2D1::GradientStop(1.0F, D2D1::ColorF(0xFFFFFF, highContrast ? 0.0F : 0.10F)),
    };
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> sheenCollection;
    if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(sheenStops, 3,
            sheenCollection.GetAddressOf()))) {
        renderTarget_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), sheenCollection.Get(),
            glassSheenBrush_.GetAddressOf());
    }
    renderTarget_->CreateSolidColorBrush(
        highContrast ? text : D2D1::ColorF(0xFFFFFF, dark ? 0.24F : 0.26F),
        borderBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        dark ? D2D1::ColorF(0xFFFFFF, 0.20F) : D2D1::ColorF(0xFFFFFF, 0.42F),
        topSheenBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x000000, dark ? 0.14F : 0.10F),
                                          shadowBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(kIconColors[0], tileBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0xFFFFFF, dark ? 0.30F : 0.46F),
        tileBorderBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(
        dark ? D2D1::ColorF(0xFFFFFF, 0.92F) : D2D1::ColorF(0x242532, 0.82F),
        indicatorBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(text, textBrush_.GetAddressOf());
    renderTarget_->CreateSolidColorBrush(kIconColors[0], iconBrush_.GetAddressOf());
}

bool DockRenderer::isDarkAppearance() const {
    const auto luminance = [](COLORREF color) {
        return 0.299F * static_cast<float>(GetRValue(color)) +
               0.587F * static_cast<float>(GetGValue(color)) +
               0.114F * static_cast<float>(GetBValue(color));
    };
    const auto systemIsDark = [&luminance]() {
        DWORD appsUseLightTheme = 1U;
        DWORD bytes = sizeof(appsUseLightTheme);
        if (RegGetValueW(HKEY_CURRENT_USER,
                         L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme",
                         RRF_RT_REG_DWORD,
                         nullptr,
                         &appsUseLightTheme,
                         &bytes) == ERROR_SUCCESS) {
            return appsUseLightTheme == 0U;
        }
        return luminance(GetSysColor(COLOR_WINDOW)) < luminance(GetSysColor(COLOR_WINDOWTEXT));
    };
    switch (appearance_.themeMode) {
    case domain::ThemeMode::Dark:
        return true;
    case domain::ThemeMode::Light:
        return false;
    case domain::ThemeMode::HighContrast:
        return systemIsDark();
    case domain::ThemeMode::FollowSystem:
        return systemIsDark();
    }
    return true;
}

void DockRenderer::resize(UINT width, UINT height) {
    const UINT newWidth = std::max(width, 1U);
    const UINT newHeight = std::max(height, 1U);
    if (newWidth == width_ && newHeight == height_) return;
    width_ = newWidth;
    height_ = newHeight;
    createSurface();
    if (backgroundBrush_ != nullptr) {
        backgroundBrush_->SetEndPoint(D2D1::Point2F(0.0F, static_cast<float>(height_)));
    }
}

void DockRenderer::setIconPixels(const IconPixels& pixels) {
    if (renderTarget_ == nullptr || pixels.itemId.empty() || pixels.size == 0U ||
        pixels.bgra.size() != static_cast<std::size_t>(pixels.size) * pixels.size * 4U) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    const HRESULT result = renderTarget_->CreateBitmap(
        D2D1::SizeU(pixels.size, pixels.size),
        pixels.bgra.data(),
        pixels.size * 4U,
        D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0F,
            96.0F),
        bitmap.GetAddressOf());
    if (SUCCEEDED(result)) {
        iconCache_[pixels.itemId] = std::move(bitmap);
        accentColors_[pixels.itemId] = pixels.accentRgb;
    }
}

void DockRenderer::clearIcon(const std::string& itemId) {
    if (!itemId.empty()) {
        iconCache_.erase(itemId);
        accentColors_.erase(itemId);
    }
}

ID2D1Bitmap* DockRenderer::iconFor(const domain::DockItem& item) {
    const auto cached = iconCache_.find(item.id);
    if (cached != iconCache_.end()) {
        return cached->second.Get();
    }
    return nullptr;
}

void DockRenderer::render(const layout::LayoutSnapshot& snapshot,
                          const std::vector<domain::DockItem>& items) {
    if (renderTarget_ == nullptr || backgroundBrush_ == nullptr || memoryDc_ == nullptr) {
        return;
    }

    const RECT targetRect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    if (FAILED(renderTarget_->BindDC(memoryDc_, &targetRect))) return;

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(0x000000, 0.0F));

    const float panelLeft = snapshot.panelLeft + 3.5F;
    const float panelTop = std::max(1.5F, static_cast<float>(height_) - snapshot.panelHeight + 1.5F);
    const float panelRight = std::max(panelLeft, snapshot.panelLeft + snapshot.panelWidth - 3.5F);
    const float panelBottom = std::max(panelTop, static_cast<float>(height_) - 5.5F);
    const float cornerRadius = std::clamp((panelBottom - panelTop) * 0.27F, 16.0F, 23.0F);
    const D2D1_ROUNDED_RECT background = D2D1::RoundedRect(
        D2D1::RectF(panelLeft, panelTop, panelRight, panelBottom),
        cornerRadius,
        cornerRadius);
    // 渐变以玻璃本身为坐标，透明放大预留区不参与着色。
    backgroundBrush_->SetStartPoint(D2D1::Point2F(panelLeft, panelTop));
    backgroundBrush_->SetEndPoint(D2D1::Point2F(panelRight, panelBottom));
    if (shadowBrush_ != nullptr) {
        shadowBrush_->SetColor(D2D1::ColorF(0x000000, 0.055F));
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(panelLeft - 2.0F,
                                          panelTop + 3.0F,
                                          panelRight + 2.0F,
                                          panelBottom + 4.0F),
                               cornerRadius + 2.0F,
                               cornerRadius + 2.0F),
            shadowBrush_.Get(), 2.0F);
        shadowBrush_->SetColor(D2D1::ColorF(0x000000, 0.065F));
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(panelLeft - 0.5F,
                                          panelTop + 2.0F,
                                          panelRight + 0.5F,
                                          panelBottom + 2.5F),
                               cornerRadius + 1.0F,
                               cornerRadius + 1.0F),
            shadowBrush_.Get(), 1.0F);
    }
    renderTarget_->FillRoundedRectangle(background, backgroundBrush_.Get());
    if (glassSheenBrush_ != nullptr) {
        glassSheenBrush_->SetStartPoint(D2D1::Point2F(0, panelTop));
        glassSheenBrush_->SetEndPoint(D2D1::Point2F(0, panelBottom));
        renderTarget_->FillRoundedRectangle(background, glassSheenBrush_.Get());
    }
    if (borderBrush_ != nullptr) {
        renderTarget_->DrawRoundedRectangle(background, borderBrush_.Get(), 0.75F);
    }
    if (topSheenBrush_ != nullptr) {
        topSheenBrush_->SetColor(D2D1::ColorF(0xFFFFFF, darkAppearance_ ? 0.18F : 0.58F));
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(panelLeft + 1.0F, panelTop + 1.0F,
                panelRight - 1.0F, panelBottom - 1.0F), cornerRadius - 1.0F, cornerRadius - 1.0F),
            topSheenBrush_.Get(), 0.6F);
    }
    if (topSheenBrush_ != nullptr && width_ > 24U) {
        renderTarget_->DrawLine(D2D1::Point2F(panelLeft + 13.0F, panelTop + 1.0F),
                                D2D1::Point2F(panelRight - 13.0F, panelTop + 1.0F),
                                topSheenBrush_.Get(),
                                1.0F);
    }

    for (std::size_t index = 0; index < snapshot.items.size(); ++index) {
        const layout::LayoutItem& layoutItem = snapshot.items[index];
        const auto itemIterator = std::find_if(items.begin(),
                                               items.end(),
                                               [&layoutItem](const domain::DockItem& item) {
                                                   return item.id == layoutItem.id;
                                               });
        if (itemIterator == items.end()) {
            continue;
        }

        if (index > 0U && itemIterator->type == domain::DockItemType::Folder &&
            borderBrush_ != nullptr) {
            const layout::LayoutItem& previousLayoutItem = snapshot.items[index - 1U];
            const auto previousItem = std::find_if(
                items.begin(), items.end(), [&previousLayoutItem](const domain::DockItem& item) {
                    return item.id == previousLayoutItem.id;
                });
            if (previousItem != items.end() && previousItem->type != domain::DockItemType::Folder) {
                const float separatorX =
                    (previousLayoutItem.right + layoutItem.left) * 0.5F;
                renderTarget_->DrawLine(
                    D2D1::Point2F(separatorX, panelTop + 13.0F),
                    D2D1::Point2F(separatorX, panelBottom - 13.0F),
                    borderBrush_.Get(),
                    1.0F);
            }
        }

        const float outerInset = std::max(0.75F, 1.0F * layoutItem.scale);
        const D2D1_RECT_F tileBounds = D2D1::RectF(layoutItem.left + outerInset,
                                                   layoutItem.top + outerInset,
                                                   layoutItem.right - outerInset,
                                                   layoutItem.bottom - outerInset);
        const float tileSize = std::max(1.0F, tileBounds.right - tileBounds.left);
        const float tileRadius = std::clamp(tileSize * 0.225F, 9.0F, 18.0F);
        const D2D1_ROUNDED_RECT iconRect = D2D1::RoundedRect(
            tileBounds,
            tileRadius,
            tileRadius);

        if (appearance_.unifiedIconTiles && tileBrush_ != nullptr) {
            const auto accent = accentColors_.find(itemIterator->id);
            const std::uint32_t rgb = accent == accentColors_.end() ? 0U : accent->second;
            tileBrush_->SetColor(tileColor(rgb, index, darkAppearance_));
            if (shadowBrush_ != nullptr) {
                shadowBrush_->SetColor(D2D1::ColorF(0x000000, 0.22F));
                renderTarget_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(tileBounds.left,
                                                  tileBounds.top + 2.0F * layoutItem.scale,
                                                  tileBounds.right,
                                                  tileBounds.bottom + 2.0F * layoutItem.scale),
                                       tileRadius,
                                       tileRadius),
                    shadowBrush_.Get());
            }
            renderTarget_->FillRoundedRectangle(iconRect, tileBrush_.Get());
            if (tileBorderBrush_ != nullptr) {
                renderTarget_->DrawRoundedRectangle(iconRect, tileBorderBrush_.Get(), 0.70F);
            }
            if (topSheenBrush_ != nullptr) {
                renderTarget_->DrawLine(
                    D2D1::Point2F(tileBounds.left + tileRadius * 0.72F,
                                  tileBounds.top + 1.4F),
                    D2D1::Point2F(tileBounds.right - tileRadius * 0.72F,
                                  tileBounds.top + 1.4F),
                    topSheenBrush_.Get(),
                    0.8F);
            }
        }

        const float contentInset = appearance_.unifiedIconTiles
                                       ? tileSize * 0.155F
                                       : outerInset;
        const D2D1_RECT_F bitmapBounds = appearance_.unifiedIconTiles
            ? D2D1::RectF(tileBounds.left + contentInset,
                          tileBounds.top + contentInset,
                          tileBounds.right - contentInset,
                          tileBounds.bottom - contentInset)
            : tileBounds;
        ID2D1Bitmap* bitmap = iconFor(*itemIterator);
        if (bitmap != nullptr) {
            renderTarget_->DrawBitmap(bitmap,
                                      bitmapBounds,
                                      1.0F,
                                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            if (!appearance_.unifiedIconTiles) {
                iconBrush_->SetColor(kIconColors[index % std::size(kIconColors)]);
                renderTarget_->FillRoundedRectangle(iconRect, iconBrush_.Get());
            }

            // 仅在 Shell 也无法提取图标时显示简洁首字占位，
            // 不再把整个文件名挤在图标里。
            const std::wstring fallbackLabel = itemIterator->displayName.empty()
                                                   ? L"?"
                                                   : itemIterator->displayName.substr(0U, 1U);
            renderTarget_->DrawText(fallbackLabel.c_str(),
                                     static_cast<UINT32>(fallbackLabel.size()),
                                     textFormat_.Get(),
                                     bitmapBounds,
                                     textBrush_.Get(),
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        if (itemIterator->running && indicatorBrush_ != nullptr) {
            const float indicatorRadius = std::clamp(1.7F * layoutItem.scale, 1.7F, 2.6F);
            renderTarget_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(layoutItem.centerX,
                                             std::min(panelBottom - 2.2F,
                                                      layoutItem.bottom + 3.2F)),
                              indicatorRadius,
                              indicatorRadius),
                indicatorBrush_.Get());
        }
    }

    const HRESULT result = renderTarget_->EndDraw();
    if (SUCCEEDED(result)) {
        GdiFlush();
        present();
    }
    if (result == D2DERR_RECREATE_TARGET) {
        renderTarget_.Reset();
        backgroundBrush_.Reset();
        borderBrush_.Reset();
        topSheenBrush_.Reset();
        shadowBrush_.Reset();
        tileBrush_.Reset();
        tileBorderBrush_.Reset();
        indicatorBrush_.Reset();
        iconBrush_.Reset();
        textBrush_.Reset();
        iconCache_.clear();
        accentColors_.clear();
        deviceReset_ = true;
        if (createRenderTarget()) {
            createBrushes();
        }
    }
}

bool DockRenderer::consumeDeviceReset() {
    const bool wasReset = deviceReset_;
    deviceReset_ = false;
    return wasReset;
}

} // namespace planetary::platform
