#include "dock/AutoHideController.h"

namespace planetary::dock {

AutoHideController::AutoHideController(bool enabled) : enabled_(enabled) {
    if (!enabled_) {
        state_ = AutoHideState::Visible;
    }
}

void AutoHideController::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        state_ = AutoHideState::Visible;
        return;
    }
    if (fullscreen_) {
        state_ = AutoHideState::Suspended;
    } else if (state_ == AutoHideState::Hidden) {
        state_ = AutoHideState::Showing;
    }
}

bool AutoHideController::enabled() const {
    return enabled_;
}

void AutoHideController::onMouseLeave() {
    if (enabled_ && !fullscreen_ && state_ == AutoHideState::Visible && canHide()) {
        state_ = AutoHideState::HidePending;
    }
}

void AutoHideController::onMouseEnter() {
    if (state_ == AutoHideState::HidePending) {
        state_ = AutoHideState::Visible;
    } else if (state_ == AutoHideState::Hidden || state_ == AutoHideState::Hiding) {
        state_ = AutoHideState::Showing;
    }
}

void AutoHideController::onHideTimer() {
    if (state_ == AutoHideState::HidePending && canHide()) {
        state_ = AutoHideState::Hiding;
    }
}

void AutoHideController::onApplicationActivated() {
    if (!fullscreen_ && visibilityLockCount_ == 0U &&
        (state_ == AutoHideState::Visible || state_ == AutoHideState::HidePending)) {
        state_ = AutoHideState::Hiding;
    }
}

void AutoHideController::onHideCompleted() {
    if (state_ == AutoHideState::Hiding) {
        state_ = fullscreen_ ? AutoHideState::Suspended : AutoHideState::Hidden;
    }
}

void AutoHideController::onShowCompleted() {
    if (state_ == AutoHideState::Showing) {
        state_ = fullscreen_ ? AutoHideState::Suspended : AutoHideState::Visible;
    }
}

void AutoHideController::setFullscreen(bool fullscreen) {
    fullscreen_ = fullscreen;
    if (fullscreen_) {
        if (enabled_) {
            state_ = AutoHideState::Suspended;
        }
        return;
    }

    if (state_ == AutoHideState::Suspended) {
        state_ = enabled_ ? AutoHideState::Hidden : AutoHideState::Visible;
    }
}

void AutoHideController::forceShow() {
    if (state_ == AutoHideState::Hidden || state_ == AutoHideState::Hiding ||
        state_ == AutoHideState::HidePending || state_ == AutoHideState::Suspended) {
        state_ = AutoHideState::Showing;
    }
}

void AutoHideController::acquireVisibilityLock() {
    ++visibilityLockCount_;
    if (state_ == AutoHideState::HidePending) {
        state_ = AutoHideState::Visible;
    }
}

void AutoHideController::releaseVisibilityLock() {
    if (visibilityLockCount_ > 0U) {
        --visibilityLockCount_;
    }
}

std::size_t AutoHideController::visibilityLockCount() const {
    return visibilityLockCount_;
}

AutoHideState AutoHideController::state() const {
    return state_;
}

bool AutoHideController::wantsHide() const {
    return state_ == AutoHideState::Hiding;
}

bool AutoHideController::wantsShow() const {
    return state_ == AutoHideState::Showing;
}

bool AutoHideController::canHide() const {
    return enabled_ && !fullscreen_ && visibilityLockCount_ == 0U;
}

} // namespace planetary::dock
