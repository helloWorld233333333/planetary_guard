#include "layout/LayoutEngine.h"
#include "layout/MagnificationModel.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectNear(float actual, float expected, float epsilon, const char* message) {
    if (std::fabs(actual - expected) > epsilon) {
        throw std::runtime_error(message);
    }
}

void testMagnificationCenterReachesMaximum() {
    const planetary::layout::MagnificationModel model;
    const float scale = model.calculateScale(100.0F, 100.0F, 120.0F, 1.5F);
    expectNear(scale, 1.5F, 0.0001F, "pointer at icon center should reach maximum scale");
}

void testMagnificationOutsideRadiusIsNeutral() {
    const planetary::layout::MagnificationModel model;
    const float scale = model.calculateScale(0.0F, 200.0F, 100.0F, 1.5F);
    expectNear(scale, 1.0F, 0.0001F, "pointer outside radius should not scale icon");
}

void testMagnificationIsBoundedAndSymmetric() {
    const planetary::layout::MagnificationModel model;
    const float left = model.calculateScale(60.0F, 100.0F, 120.0F, 1.5F);
    const float right = model.calculateScale(140.0F, 100.0F, 120.0F, 1.5F);
    expectNear(left, right, 0.0001F, "magnification should be symmetric around icon center");
    expectTrue(left >= 1.0F && left <= 1.5F, "magnification should stay within configured bounds");
}

void testLayoutIsCentered() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    const std::vector<planetary::domain::DockItem> items = {
        {"a", planetary::domain::DockItemType::Application, L"A", L"a.exe"},
        {"b", planetary::domain::DockItemType::Application, L"B", L"b.exe"},
        {"c", planetary::domain::DockItemType::Application, L"C", L"c.exe"}
    };

    const planetary::layout::LayoutSnapshot snapshot = engine.calculate(items, -1.0F, 1.0F);
    expectTrue(snapshot.items.size() == 3U, "layout should contain every dock item");
    expectTrue(snapshot.items.front().left > 0.0F,
               "dock should keep breathing room before the first icon");
    expectNear(snapshot.items.front().left,
               snapshot.width - snapshot.items.back().right,
               0.0001F,
               "dock horizontal padding should be symmetrical");
}

void testEmptyLayoutHasNoItems() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    const std::vector<planetary::domain::DockItem> items;
    const planetary::layout::LayoutSnapshot snapshot = engine.calculate(items, -1.0F, 1.0F);
    expectTrue(snapshot.items.empty(), "empty input should produce an empty layout");
    expectNear(snapshot.width, 0.0F, 0.0001F, "empty layout width should be zero");
}

void testLayoutScalesWithDpi() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    const std::vector<planetary::domain::DockItem> items = {
        {"a", planetary::domain::DockItemType::Application, L"A", L"a.exe"}
    };
    const planetary::layout::LayoutSnapshot base = engine.calculate(items, -1.0F, 1.0F);
    const planetary::layout::LayoutSnapshot highDpi = engine.calculate(items, -1.0F, 2.0F);
    expectNear(highDpi.width, base.width * 2.0F, 0.0001F,
               "layout width should follow the DPI scale");
}

void testLayoutHoverChangesScaleWithoutChangingItemCount() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    const std::vector<planetary::domain::DockItem> items = {
        {"a", planetary::domain::DockItemType::Application, L"A", L"a.exe"},
        {"b", planetary::domain::DockItemType::Application, L"B", L"b.exe"},
        {"c", planetary::domain::DockItemType::Application, L"C", L"c.exe"}
    };

    const planetary::layout::LayoutSnapshot base = engine.calculate(items, -1.0F, 1.0F);
    const planetary::layout::LayoutSnapshot hover = engine.calculate(items, base.items[1].centerX, 1.0F);
    expectTrue(hover.items.size() == base.items.size(), "hover should not add or remove items");
    expectTrue(hover.items[1].scale > 1.0F, "hovered item should be magnified");
    expectNear(hover.width, base.width, 0.0001F, "hover must keep native window width fixed");
    expectNear(hover.height, base.height, 0.0001F, "hover must keep native window height fixed");
    expectNear(hover.panelLeft, base.panelLeft, 0.0001F, "blur panel must stay stationary");
    expectTrue(hover.items.back().right - hover.items.front().left >
               base.items.back().right - base.items.front().left, "icons still spread during magnification");
    expectNear(hover.panelHeight, base.panelHeight, 0.0001F,
               "hover must not stretch the material shelf");
    for (const auto& item : hover.items) {
        expectNear(item.bottom, hover.items.front().bottom, 0.0001F,
                   "magnified icons must share one baseline");
    }
}

void testDisabledItemsAreExcludedFromLayout() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    std::vector<planetary::domain::DockItem> items = {
        {"enabled", planetary::domain::DockItemType::Application, L"Enabled", L"a.exe"},
        {"disabled", planetary::domain::DockItemType::Application, L"Disabled", L"b.exe"}
    };
    items[1].enabled = false;

    const auto snapshot = engine.calculate(items, -1.0F, 1.0F);
    expectTrue(snapshot.items.size() == 1U, "disabled items must not be visible in the dock");
    expectTrue(snapshot.items.front().id == "enabled", "layout should retain the enabled item");
}

void testFolderSectionHasVisualSeparation() {
    planetary::layout::LayoutEngine engine(48.0F, 8.0F, 1.4F);
    const std::vector<planetary::domain::DockItem> items = {
        {"app", planetary::domain::DockItemType::Application, L"App", L"app.exe"},
        {"folder", planetary::domain::DockItemType::Folder, L"Folder", L"C:\\Temp"}
    };

    const auto snapshot = engine.calculate(items, -1.0F, 1.0F);
    expectTrue(snapshot.items.size() == 2U, "folder section should keep both items");
    const float gap = snapshot.items[1].left - snapshot.items[0].right;
    expectTrue(gap >= 17.0F, "folder section should have room for a divider");
}

void testPointerSweepStaysInsideFixedCanvas() {
    for (const float dpi : {1.0F, 1.5F, 2.0F}) {
        planetary::layout::LayoutEngine engine(54.0F, 8.0F, 1.8F);
        std::vector<planetary::domain::DockItem> items;
        for (int i = 0; i < 16; ++i) items.push_back({std::to_string(i), planetary::domain::DockItemType::Application, L"App", L"app.exe"});
        const auto rest = engine.calculate(items, -1.0F, dpi);
        for (float x = 0; x <= rest.width; x += 7.0F) {
            const auto frame = engine.calculate(items, x, dpi);
            expectNear(frame.width, rest.width, 0.001F, "sweep must not resize the canvas");
            expectNear(frame.height, rest.height, 0.001F, "sweep must not resize canvas height");
            for (const auto& icon : frame.items) {
                expectTrue(icon.left >= 0 && icon.top >= 0 && icon.right <= frame.width && icon.bottom <= frame.height,
                           "magnified icons must not be clipped");
            }
        }
    }
}

} // namespace

int main() {
    try {
        testMagnificationCenterReachesMaximum();
        testMagnificationOutsideRadiusIsNeutral();
        testMagnificationIsBoundedAndSymmetric();
        testLayoutIsCentered();
        testEmptyLayoutHasNoItems();
        testLayoutScalesWithDpi();
        testLayoutHoverChangesScaleWithoutChangingItemCount();
        testDisabledItemsAreExcludedFromLayout();
        testFolderSectionHasVisualSeparation();
        testPointerSweepStaysInsideFixedCanvas();
        std::cout << "All planetary core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
