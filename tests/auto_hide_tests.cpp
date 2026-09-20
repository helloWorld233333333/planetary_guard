#include "dock/AutoHideController.h"

#include <iostream>
#include <stdexcept>

namespace {

using planetary::dock::AutoHideController;
using planetary::dock::AutoHideState;

void expectTrue(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testLeaveAndTimeoutHide() {
    AutoHideController controller(true);
    controller.onMouseLeave();
    expectTrue(controller.state() == AutoHideState::HidePending,
               "mouse leave should enter pending state");
    controller.onHideTimer();
    expectTrue(controller.wantsHide(), "hide timer should request hiding");
    controller.onHideCompleted();
    expectTrue(controller.state() == AutoHideState::Hidden,
               "completed hide should become hidden");
}

void testActivationDismissesWithoutAutoHideAndEdgeRestores() {
    AutoHideController controller(false);
    controller.onApplicationActivated();
    expectTrue(controller.wantsHide(), "activation should dismiss even with auto hide disabled");
    controller.onHideCompleted();
    expectTrue(controller.state() == AutoHideState::Hidden, "dismissal should finish hidden");
    controller.onMouseEnter();
    expectTrue(controller.wantsShow(), "edge must restore dismissed dock");
    controller.onShowCompleted();
    expectTrue(controller.state() == AutoHideState::Visible, "edge show should finish visible");
    expectTrue(!controller.enabled(), "dismissal must not change saved auto hide preference");
    controller.acquireVisibilityLock();
    controller.onApplicationActivated();
    expectTrue(!controller.wantsHide(), "menu and drag visibility locks must be respected");
    controller.releaseVisibilityLock();
    controller.setFullscreen(true);
    controller.onApplicationActivated();
    expectTrue(!controller.wantsHide(), "fullscreen must not expose activation edge");
}

void testEnterCancelsPendingAndShowsHiddenDock() {
    AutoHideController controller(true);
    controller.onMouseLeave();
    controller.onMouseEnter();
    expectTrue(controller.state() == AutoHideState::Visible,
               "enter during delay should cancel hiding");

    controller.onMouseLeave();
    controller.onHideTimer();
    controller.onHideCompleted();
    controller.onMouseEnter();
    expectTrue(controller.wantsShow(), "edge enter should request showing");
}

void testVisibilityLockPreventsHide() {
    AutoHideController controller(true);
    controller.acquireVisibilityLock();
    controller.onMouseLeave();
    expectTrue(controller.state() == AutoHideState::Visible,
               "visibility lock should prevent pending hide");
    controller.releaseVisibilityLock();
    controller.onMouseLeave();
    expectTrue(controller.state() == AutoHideState::HidePending,
               "released lock should allow hiding");
}

void testFullscreenSuspendsAndTrayCanShow() {
    AutoHideController controller(true);
    controller.setFullscreen(true);
    expectTrue(controller.state() == AutoHideState::Suspended,
               "fullscreen should suspend auto hide");
    controller.forceShow();
    expectTrue(controller.wantsShow(), "tray show should work while suspended");
    controller.onShowCompleted();
    expectTrue(controller.state() == AutoHideState::Suspended,
               "fullscreen should remain suspended after showing");
    controller.setFullscreen(false);
    expectTrue(controller.state() == AutoHideState::Hidden,
               "leaving fullscreen should return to hidden when enabled");
}

void testDisablingAutoHideWhileFullscreenRestoresVisibleState() {
    AutoHideController controller(true);
    controller.setFullscreen(true);
    controller.setEnabled(false);
    expectTrue(controller.state() == AutoHideState::Visible,
               "disabling auto hide in fullscreen should restore visible state");
    controller.setFullscreen(false);
    expectTrue(controller.state() == AutoHideState::Visible,
               "leaving fullscreen must not re-hide a disabled dock");
}

} // namespace

int main() {
    try {
        testLeaveAndTimeoutHide();
        testActivationDismissesWithoutAutoHideAndEdgeRestores();
        testEnterCancelsPendingAndShowsHiddenDock();
        testVisibilityLockPreventsHide();
        testFullscreenSuspendsAndTrayCanShow();
        testDisablingAutoHideWhileFullscreenRestoresVisibleState();
        std::cout << "All auto-hide tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Auto-hide test failure: " << error.what() << '\n';
        return 1;
    }
}
