#pragma once

#include <cstdint>
#include <optional>

namespace planetary::dock {

/** 隐藏后的悬停唤出：先离开再进入，连续停留达到延迟才触发。 */
class HoverRevealController {
public:
    void begin(bool pointerInside) {
        armed_ = !pointerInside;
        enteredAt_.reset();
    }

    bool update(bool pointerInside, std::uint64_t now, std::uint64_t delay,
                bool pointerPressed = false) {
        if (pointerPressed) {
            // 拖动取消本次停留；在边缘松手后必须重新进入，不能突然弹出。
            begin(pointerInside);
            return false;
        }
        if (!pointerInside) {
            armed_ = true;
            enteredAt_.reset();
            return false;
        }
        if (!armed_) return false;
        if (!enteredAt_) enteredAt_ = now;
        return now - *enteredAt_ >= delay;
    }

private:
    bool armed_ = false;
    std::optional<std::uint64_t> enteredAt_;
};
}
