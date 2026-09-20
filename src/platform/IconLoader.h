#pragma once

#include "domain/DockItem.h"

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace planetary::platform {

/** 后台提取的预乘 BGRA 图标像素。Direct2D 位图只在 UI 线程创建。 */
struct IconPixels {
    std::string itemId;
    std::wstring sourcePath;
    UINT size = 0U;
    /** 可见图标内容的平均颜色（0xRRGGBB）。 */
    std::uint32_t accentRgb = 0U;
    std::vector<std::uint8_t> bgra;
};

/**
 * Shell 图标后台加载器。
 *
 * 工作线程只访问 Shell/GDI 并复制 CPU 像素，不触碰 Direct2D 设备资源；
 * 完成后向窗口投递一次轻量消息，避免图标提取阻塞鼠标和绘制。
 */
class IconLoader {
public:
    static constexpr UINT kResultMessage = WM_APP + 43U;

    IconLoader() = default;
    ~IconLoader();

    IconLoader(const IconLoader&) = delete;
    IconLoader& operator=(const IconLoader&) = delete;

    bool start(HWND owner);
    void stop();
    void request(const std::vector<domain::DockItem>& items);
    void invalidateRequests();
    std::vector<IconPixels> takeResults();

private:
    struct Request {
        std::string itemId;
        std::wstring iconPath;
    };

    static void workerMain(IconLoader* loader);
    static bool isNetworkPath(const std::wstring& path);
    static std::wstring resolveIconPath(const domain::DockItem& item);
    static bool loadPixels(const Request& request, IconPixels& output);

    HWND owner_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Request> requests_;
    std::deque<IconPixels> results_;
    std::unordered_map<std::string, std::wstring> requestedPaths_;
    bool stopping_ = false;
    bool started_ = false;
};

} // namespace planetary::platform
