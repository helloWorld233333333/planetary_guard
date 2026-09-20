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

    bool update(bool pointerInside, std::uint64_t now, std::uint64_t delay) {
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
