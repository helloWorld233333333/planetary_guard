#include "dock/AutoHideController.h"
#include "dock/HoverRevealController.h"

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

void testHoverRevealRequiresReentryAndContinuousDwell() {
    planetary::dock::HoverRevealController reveal;
    reveal.begin(true);
    expectTrue(!reveal.update(true, 1000, 80), "click pointer must not reopen immediately");
    expectTrue(!reveal.update(false, 1050, 80), "leaving should arm without opening");
    expectTrue(!reveal.update(true, 1100, 80), "entry must respect hover delay");
    expectTrue(!reveal.update(false, 1140, 80), "leaving cancels dwell");
    expectTrue(!reveal.update(true, 1200, 80), "reentry starts a fresh delay");
    expectTrue(!reveal.update(true, 1279, 80), "must not reveal before delay expires");
    expectTrue(reveal.update(true, 1280, 80), "continuous hover must reveal");
    reveal.begin(false);
    expectTrue(!reveal.update(true, 0, 80), "zero timestamp must be valid");
    expectTrue(reveal.update(true, 80, 80), "external activation allows first hover");
}

void testDragSuppressesRevealAndResetsDwell() {
    planetary::dock::HoverRevealController reveal;
    reveal.begin(false);
    expectTrue(!reveal.update(true, 0, 300), "entry must not reveal immediately");
    expectTrue(!reveal.update(true, 299, 300), "300ms continuous dwell required");
    expectTrue(reveal.update(true, 300, 300), "300ms should reveal");
    reveal.begin(false);
    expectTrue(!reveal.update(true, 1000, 300), "new entry starts dwell");
    expectTrue(!reveal.update(true, 1400, 300, true), "held button must suppress reveal");
    expectTrue(!reveal.update(true, 1800, 300, false), "releasing over edge must not resurrect old dwell");
    reveal.update(false, 1900, 300);
    expectTrue(!reveal.update(true, 2000, 300), "after dragging, reentry restarts dwell");
    expectTrue(reveal.update(true, 2300, 300), "normal hover should recover after drag");
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
    expectTrue(controller.wantsHide(), "activation request must survive a visibility lock");
    controller.onHideCompleted();
    controller.setFullscreen(true);
    controller.onApplicationActivated();
    expectTrue(!controller.wantsHide(), "fullscreen must not expose activation edge");
}

void testActivationDuringNestedMenuAndShowing() {
    AutoHideController controller(false);
    controller.acquireVisibilityLock();
    controller.acquireVisibilityLock();
    controller.onApplicationActivated();
    controller.releaseVisibilityLock();
    expectTrue(!controller.wantsHide(), "inner menu release must not dismiss outer interaction");
    controller.releaseVisibilityLock();
    expectTrue(controller.wantsHide(), "last lock release must execute queued dismissal");
    controller.onHideCompleted();
    controller.onMouseEnter();
    expectTrue(controller.wantsShow(), "edge should still wake the dock");
    controller.onApplicationActivated();
    expectTrue(controller.wantsHide(), "activation during showing must not be discarded");
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
        planetary::dock::HoverRevealController multi;
        multi.begin(false);
        expectTrue(!multi.update(true, 100, 300, false, 1), "first screen starts dwell");
        expectTrue(!multi.update(true, 350, 300, false, 2), "screen change resets dwell");
        expectTrue(!multi.update(true, 649, 300, false, 2), "second screen needs full 300ms");
        expectTrue(multi.update(true, 650, 300, false, 2), "second screen reveals after dwell");
        testLeaveAndTimeoutHide();
        testHoverRevealRequiresReentryAndContinuousDwell();
        testDragSuppressesRevealAndResetsDwell();
        testActivationDismissesWithoutAutoHideAndEdgeRestores();
        testActivationDuringNestedMenuAndShowing();
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
