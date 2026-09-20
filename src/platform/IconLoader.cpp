#include "platform/IconLoader.h"
#include "platform/IconNormalizer.h"

#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <objbase.h>
#include <wrl.h>

#include <algorithm>
#include <cstring>

namespace planetary::platform {

namespace {

constexpr UINT kIconSize = 128U;

HICON loadJumboIcon(const std::wstring& path) {
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(path.c_str(),
                       0U,
                       &fileInfo,
                       sizeof(fileInfo),
                       SHGFI_SYSICONINDEX) == 0U) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IImageList> imageList;
    if (FAILED(SHGetImageList(SHIL_JUMBO,
                              IID_PPV_ARGS(imageList.GetAddressOf())))) {
        return nullptr;
    }
    HICON icon = nullptr;
    return SUCCEEDED(imageList->GetIcon(fileInfo.iIcon, ILD_TRANSPARENT, &icon)) ? icon : nullptr;
}

HICON loadFallbackIcon(const std::wstring& path) {
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(path.c_str(),
                       0U,
                       &fileInfo,
                       sizeof(fileInfo),
                       SHGFI_ICON | SHGFI_LARGEICON) == 0U) {
        return nullptr;
    }
    return fileInfo.hIcon;
}

} // namespace

IconLoader::~IconLoader() {
    stop();
}

bool IconLoader::start(HWND owner) {
    stop();
    if (owner == nullptr) return false;
    owner_ = owner;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = false;
        started_ = true;
    }
    worker_ = std::thread(&IconLoader::workerMain, this);
    return true;
}

void IconLoader::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            owner_ = nullptr;
            return;
        }
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.clear();
        results_.clear();
        requestedPaths_.clear();
        started_ = false;
    }
    owner_ = nullptr;
}

void IconLoader::request(const std::vector<domain::DockItem>& items) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || stopping_) return;
    std::erase_if(requestedPaths_, [&](const auto& cached) {
        return std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.id == cached.first; });
    });

    for (const domain::DockItem& item : items) {
        if (!item.enabled || item.id.empty() || item.targetPath.empty()) {
            continue;
        }
        const std::wstring iconPath = resolveIconPath(item);
        if (iconPath.empty() || isNetworkPath(iconPath)) continue;
        const auto existing = requestedPaths_.find(item.id);
        if (existing != requestedPaths_.end() && existing->second == iconPath) {
            continue;
        }
        requestedPaths_[item.id] = iconPath;
        requests_.push_back(Request{item.id, iconPath});
    }
    condition_.notify_one();
}

std::vector<IconPixels> IconLoader::takeResults() {
    std::vector<IconPixels> output;
    std::lock_guard<std::mutex> lock(mutex_);
    output.reserve(results_.size());
    while (!results_.empty()) {
        output.push_back(std::move(results_.front()));
        results_.pop_front();
    }
    return output;
}

void IconLoader::invalidateRequests() {
    std::lock_guard<std::mutex> lock(mutex_);
    requestedPaths_.clear();
    requests_.clear();
    results_.clear();
}

void IconLoader::workerMain(IconLoader* loader) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(loader->mutex_);
            loader->condition_.wait(lock, [loader]() {
                return loader->stopping_ || !loader->requests_.empty();
            });
            if (loader->stopping_) break;
            request = std::move(loader->requests_.front());
            loader->requests_.pop_front();
        }

        IconPixels pixels;
        if (!loadPixels(request, pixels)) {
            std::lock_guard<std::mutex> lock(loader->mutex_);
            const auto current = loader->requestedPaths_.find(request.itemId);
            if (current != loader->requestedPaths_.end() && current->second == request.iconPath) {
                loader->requestedPaths_.erase(current);
            }
            continue;
        }
        HWND owner = nullptr;
        {
            std::lock_guard<std::mutex> lock(loader->mutex_);
            if (loader->stopping_) continue;
            const auto current = loader->requestedPaths_.find(request.itemId);
            if (current == loader->requestedPaths_.end() || current->second != request.iconPath) {
                continue;
            }
            loader->results_.push_back(std::move(pixels));
            owner = loader->owner_;
        }
        if (owner != nullptr) {
            PostMessageW(owner, kResultMessage, 0, 0);
        }
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
}

bool IconLoader::isNetworkPath(const std::wstring& path) {
    return path.size() >= 2U && path[0] == L'\\' && path[1] == L'\\';
}

std::wstring IconLoader::resolveIconPath(const domain::DockItem& item) {
    if (!item.customIconPath.empty()) {
        const DWORD attributes = GetFileAttributesW(item.customIconPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            return item.customIconPath;
        }
    }
    const DWORD attributes = GetFileAttributesW(item.targetPath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return item.targetPath;
    }

    // 默认项目和部分旧配置使用 notepad.exe 这类 PATH 中的程序名。
    // Shell 图标 API 不会自动按 PATH 解析，先转成绝对路径才能取到真实图标。
    const DWORD required = SearchPathW(nullptr,
                                       item.targetPath.c_str(),
                                       nullptr,
                                       0U,
                                       nullptr,
                                       nullptr);
    if (required > 0U) {
        std::vector<wchar_t> resolved(static_cast<std::size_t>(required) + 1U, L'\0');
        if (SearchPathW(nullptr,
                        item.targetPath.c_str(),
                        nullptr,
                        static_cast<DWORD>(resolved.size()),
                        resolved.data(),
                        nullptr) > 0U) {
            return resolved.data();
        }
    }
    return item.targetPath;
}

bool IconLoader::loadPixels(const Request& request, IconPixels& output) {
    HICON icon = loadJumboIcon(request.iconPath);
    if (icon == nullptr) icon = loadFallbackIcon(request.iconPath);
    if (icon == nullptr) return false;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(kIconSize);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(kIconSize);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc == nullptr ? nullptr : CreateCompatibleDC(screenDc);
    void* bits = nullptr;
    HBITMAP bitmap = memoryDc == nullptr
                         ? nullptr
                         : CreateDIBSection(memoryDc,
                                            &bitmapInfo,
                                            DIB_RGB_COLORS,
                                            &bits,
                                            nullptr,
                                            0U);
    HGDIOBJ previous = nullptr;
    bool succeeded = false;
    if (bitmap != nullptr && bits != nullptr) {
        previous = SelectObject(memoryDc, bitmap);
        std::memset(bits, 0, kIconSize * kIconSize * 4U);
        succeeded = DrawIconEx(memoryDc,
                               0,
                               0,
                               icon,
                               kIconSize,
                               kIconSize,
                               0U,
                               nullptr,
                               DI_NORMAL) != FALSE;
        if (succeeded) {
            output.itemId = request.itemId;
            output.sourcePath = request.iconPath;
            output.size = kIconSize;
            output.bgra.resize(kIconSize * kIconSize * 4U);
            std::memcpy(output.bgra.data(),
                        bits,
                        output.bgra.size());
            const bool hasAlpha = std::any_of(output.bgra.begin() + 3,
                                              output.bgra.end(),
                                              [channel = 0U](std::uint8_t value) mutable {
                                                  const bool isAlpha = channel == 0U;
                                                  channel = (channel + 1U) % 4U;
                                                  return isAlpha && value != 0U;
                                              });
            for (std::size_t index = 0; index < output.bgra.size(); index += 4U) {
                if (!hasAlpha && (output.bgra[index] != 0U || output.bgra[index + 1U] != 0U ||
                                  output.bgra[index + 2U] != 0U)) {
                    output.bgra[index + 3U] = 255U;
                }
                const std::uint32_t alpha = output.bgra[index + 3U];
                output.bgra[index] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(output.bgra[index]) * alpha + 127U) / 255U);
                output.bgra[index + 1U] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(output.bgra[index + 1U]) * alpha + 127U) / 255U);
                output.bgra[index + 2U] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(output.bgra[index + 2U]) * alpha + 127U) / 255U);
            }
            output.accentRgb = averageOpaqueColor(output.bgra, kIconSize);
            normalizeIconCanvas(output.bgra, kIconSize, 112U);
        }
    }

    if (previous != nullptr) SelectObject(memoryDc, previous);
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memoryDc != nullptr) DeleteDC(memoryDc);
    if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
    DestroyIcon(icon);
    return succeeded;
}

} // namespace planetary::platform
