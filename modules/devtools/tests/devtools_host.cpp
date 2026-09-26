#include "core/dsl_runtime.h"

#include <type_traits>

// The panel talks to the framework through Debug-only Runtime hooks; a Release
// build has neither the hooks nor the panel sources.
template <typename T, typename = void>
struct HasOverlayHooks : std::false_type {};

template <typename T>
struct HasOverlayHooks<T, std::void_t<decltype(&T::setInputFilter),
                                      decltype(&T::pushPointerEvent),
                                      decltype(&T::pushScrollEvent),
                                      decltype(&T::renderDirectOverlay)>> : std::true_type {};

#if defined(EUI_DEBUG_BUILD)

#include "modules/devtools/devtools.h"
#include "modules/devtools/devtools_host.h"
#include "modules/devtools/devtools_properties.h"
#include "modules/devtools/devtools_theme.h"

#include <algorithm>
#include <cassert>
#include <vector>

static_assert(HasOverlayHooks<core::dsl::Runtime>::value, "Debug Runtime hooks are required");

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr float kDpiScale = 1.0f;
constexpr float kFrameSeconds = 1.0f / 60.0f;

core::PointerEvent pressAt(double x, double y) {
    core::PointerEvent event;
    event.x = x;
    event.y = y;
    event.action = core::PointerAction::Press;
    event.button = core::PointerButton::Left;
    event.buttons.set(core::PointerButton::Left, true);
    return event;
}

core::KeyEvent F12Key(core::KeyAction action) {
    core::KeyEvent key;
    key.key = core::InputKey::F12;
    key.action = action;
    return key;
}

// Sees what the panel composed without a renderer: the panel composes into its
// own Runtime, so its element tree is the composed result.
bool hasPanelElement(const modules::devtools::DevtoolsHost& host, const std::string& part) {
    for (const core::dsl::runtime::ElementTreeNode& node : host.panelElementTree().nodes) {
        if (node.id.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Row slots of the element tree list. A slot is named `...slot.<n>`; the row it
// composes lives below it.
int countPanelRows(const modules::devtools::DevtoolsHost& host) {
    const std::string prefix = "elements.list.slot.";
    int count = 0;
    for (const core::dsl::runtime::ElementTreeNode& node : host.panelElementTree().nodes) {
        const std::size_t at = node.id.find(prefix);
        if (at == std::string::npos) {
            continue;
        }
        const std::string tail = node.id.substr(at + prefix.size());
        if (!tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos) {
            ++count;
        }
    }
    return count;
}

// Frames of the composed panel elements whose id contains `part`, in composition
// order. A test clicks where the panel put a control instead of recomputing the
// layout the composition already did.
std::vector<core::Rect> panelElementFrames(const modules::devtools::DevtoolsHost& host, const std::string& part) {
    std::vector<core::Rect> frames;
    for (const core::dsl::runtime::ElementTreeNode& node : host.panelElementTree().nodes) {
        if (node.id.find(part) != std::string::npos) {
            frames.push_back(node.frame);
        }
    }
    return frames;
}

core::Rect panelElementFrame(const modules::devtools::DevtoolsHost& host, const std::string& part) {
    const std::vector<core::Rect> frames = panelElementFrames(host, part);
    assert(!frames.empty());
    return frames.front();
}

// A drag queues one edit per pointer event that changed a value; the page ends up with
// the last one, so that is what a test looks at.
bool takeLastElementPropertyEdit(modules::devtools::DevtoolsHost& host,
                                 modules::devtools::DevtoolsHost::ElementPropertyEdit& edit) {
    bool any = false;
    modules::devtools::DevtoolsHost::ElementPropertyEdit current;
    while (host.takeElementPropertyEdit(current)) {
        edit = current;
        any = true;
    }
    return any;
}

} // namespace

int main() {
    using namespace modules::devtools;

    assert(modules::devtools::available());
    const DevtoolsTheme& theme = devtoolsTheme();
    DevtoolsHost host;
    int detachedOpens = 0;
    int detachedCloses = 0;
    host.setDetachedWindowOpener([&] { ++detachedOpens; });
    host.setDetachedWindowCloser([&] { ++detachedCloses; });

    // One app frame for the window the panel is docked into.
    const auto frame = [&] {
        return host.update(kWindowWidth, kWindowHeight, kDpiScale, kFrameSeconds);
    };

    // A hidden panel leaves the whole window to the page.
    assert(!frame());
    assert(!host.visible());
    assert(!host.wantsElementTree());
    assert(host.hoveredElement().empty());
    assert(host.contentBounds().x == 0.0f);
    assert(host.contentBounds().width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds().height == static_cast<float>(kWindowHeight));
    assert(host.contentHeight() == kWindowHeight);

    // The hotkey toggles the panel and only reacts to presses.
    assert(!host.handleHotkey(F12Key(core::KeyAction::Release)));
    assert(host.handleHotkey(F12Key(core::KeyAction::Press)));
    assert(host.visible());
    assert(frame());
    assert(host.contentHeight() < kWindowHeight);
    assert(host.activeTab() == DevtoolsTab::Performance);

    core::KeyEvent plainKey;
    plainKey.key = core::InputKey::A;
    plainKey.action = core::KeyAction::Press;
    assert(!host.handleHotkey(plainKey));

    core::KeyEvent shortcut;
    shortcut.key = core::InputKey::I;
    shortcut.action = core::KeyAction::Press;
    shortcut.modifiers.control = true;
    shortcut.modifiers.shift = true;
    assert(host.handleHotkey(shortcut));
    assert(!host.visible());
    assert(host.contentHeight() == kWindowHeight);

    assert(host.handleHotkey(F12Key(core::KeyAction::Press)));
    assert(host.visible());
    assert(host.contentHeight() < kWindowHeight);
    // The panel composes the same content again, so it has nothing new to
    // repaint: showing a panel repaints the window because the page area
    // changes, which the app layer reads from contentBounds().
    frame();

    // A new sample repaints the panel once.
    app::PerformanceSnapshot sample;
    sample.revision = 1;
    sample.framesPerSecond = 60.0;
    host.setPerformanceSnapshot(sample);
    assert(frame());
    host.setPerformanceSnapshot(sample);
    assert(!frame());

    // The wheel reaches the docked panel and actually advances its scroll. The
    // panel runtime is driven with the frame delta, so the impulse based scroll
    // moves: a zero delta would leave the offset where it was.
    {
        app::PerformanceSnapshot longSample;
        longSample.revision = 2;
        longSample.hasRenderStats = true;   // a metric list long enough to scroll
        host.setPerformanceSnapshot(longSample);
        frame();

        core::PointerEvent overPanel = pressAt(kWindowWidth * 0.5, static_cast<double>(host.contentHeight()) + 60.0);
        overPanel.action = core::PointerAction::Move;
        overPanel.button = core::PointerButton::None;
        overPanel.buttons = {};
        core::ScrollEvent wheel{0.0, -1.0};
        std::vector<core::PointerEvent> events{overPanel};
        host.filterInput(events, wheel);
        assert(!wheel.active());            // the panel owns the wheel, not the page
        frame();
        assert(host.performanceScrollOffset() > 0.0f);
    }

    // Routes one event through the panel and returns the copy the page sees. The
    // caller keeps its own coordinates, because an event the panel consumes is
    // rewritten for the page.
    const auto routePointer = [&](const core::PointerEvent& event, core::ScrollEvent& scroll) {
        std::vector<core::PointerEvent> events{event};
        host.filterInput(events, scroll);
        return events.front();
    };
    // Sliders report a value while the pointer is pressed and moved, so editing one
    // is a drag rather than a single click.
    const auto dragPanel = [&](double fromX, double fromY, double toX, double toY) {
        core::ScrollEvent scroll;
        core::PointerEvent press = pressAt(fromX, fromY);
        routePointer(press, scroll);
        frame();

        press.action = core::PointerAction::Move;
        press.x = toX;
        press.y = toY;
        routePointer(press, scroll);
        frame();

        press.action = core::PointerAction::Release;
        press.buttons.set(core::PointerButton::Left, false);
        routePointer(press, scroll);
        frame();
    };
    const auto clickPanel = [&](double x, double y) {
        core::PointerEvent press = pressAt(x, y);
        core::ScrollEvent scroll;
        routePointer(press, scroll);
        frame();

        press.action = core::PointerAction::Release;
        press.buttons.set(core::PointerButton::Left, false);
        routePointer(press, scroll);
        frame();
    };

    // The panel keeps pointer and wheel while the pointer is on it.
    core::PointerEvent overPanel = pressAt(100.0, static_cast<double>(host.contentHeight()) + 20.0);
    core::ScrollEvent panelScroll{0.0, 1.0};
    const core::PointerEvent routedPanel = routePointer(overPanel, panelScroll);
    assert(routedPanel.x < 0.0 && routedPanel.y < 0.0);
    assert(!panelScroll.active());

    // The page keeps both outside the panel.
    core::PointerEvent overPage = pressAt(100.0, 20.0);
    core::ScrollEvent pageScroll{0.0, 1.0};
    const core::PointerEvent routedPage = routePointer(overPage, pageScroll);
    assert(routedPage.x == 100.0 && routedPage.y == 20.0);
    assert(pageScroll.active());

    // Dragging the panel edge resizes the page area and clamps at both limits.
    const int initialContentHeight = host.contentHeight();
    core::PointerEvent resizePress = pressAt(120.0, static_cast<double>(initialContentHeight) + 2.0);
    core::ScrollEvent resizeScroll;
    assert(routePointer(resizePress, resizeScroll).x < 0.0);

    core::PointerEvent resizeMove = resizePress;
    resizeMove.action = core::PointerAction::Move;
    resizeMove.button = core::PointerButton::None;
    resizeMove.y -= 100.0;
    assert(routePointer(resizeMove, resizeScroll).x < 0.0);
    assert(host.contentHeight() == initialContentHeight - 100);

    core::PointerEvent resizeRelease = resizeMove;
    resizeRelease.y = 0.0;
    resizeRelease.action = core::PointerAction::Release;
    resizeRelease.button = core::PointerButton::Left;
    resizeRelease.buttons.set(core::PointerButton::Left, false);
    assert(routePointer(resizeRelease, resizeScroll).x < 0.0);
    assert(host.contentHeight() == 120);

    // Opening the more menu repaints the panel without disturbing the host page:
    // an overlay change asks for a frame, not for an app UI update, because a UI
    // update makes the app layer recompose the page and repaint the whole window.
    core::platform::consumeFrameRequest();
    core::platform::consumeUiUpdate();
    clickPanel(kWindowWidth - 48.0, host.contentBounds().height + 16.0);
    assert(core::platform::consumeFrameRequest());
    assert(!core::platform::consumeUiUpdate());

    // The frame that carries the interaction still draws the tree the panel already
    // had, and a following frame presents the new state. Composing a second time
    // inside the interaction frame would leave the panel's primitive bounds unusable
    // for that frame's draw, so the renderer culls the whole panel and the panel area
    // shows the cleared render cache for one frame.
    const bool presentedAfterInteraction = frame() || frame();
    assert(presentedAfterInteraction);

    clickPanel(kWindowWidth - 48.0, host.contentBounds().height + 16.0);

    // The more menu moves the panel to another edge.
    const auto chooseDock = [&](int row) {
        const core::Rect content = host.contentBounds();
        const bool side = host.dockPosition() != DockPosition::Bottom;
        const double panelX = side && host.dockPosition() == DockPosition::Right ? content.width : 0.0;
        const double panelY = side ? 0.0 : content.height;
        const double panelWidth = side ? kWindowWidth - content.width : kWindowWidth;
        // Toolbar trailing row: settings, more, close, right aligned.
        clickPanel(panelX + panelWidth - 48.0, panelY + 16.0);
        clickPanel(panelX + panelWidth - theme.menuWidth - 36.0 + 10.0,
                   panelY + theme.toolbarHeight + 6.0 + theme.menuPadding +
                       (static_cast<double>(row) + 0.5) * theme.menuRowHeight);
    };

    assert(host.dockPosition() == DockPosition::Bottom);
    chooseDock(1);
    assert(host.dockPosition() == DockPosition::Left);
    assert(host.contentBounds().x == 300.0f);
    assert(host.contentBounds().width == 500.0f);
    assert(host.contentBounds().height == static_cast<float>(kWindowHeight));

    chooseDock(3);
    assert(host.dockPosition() == DockPosition::Right);
    assert(host.contentBounds().x == 0.0f);
    assert(host.contentBounds().width == 500.0f);

    // A side panel resizes along x.
    core::PointerEvent sideResize = pressAt(502.0, 200.0);
    core::ScrollEvent sideScroll;
    assert(routePointer(sideResize, sideScroll).x < 0.0);
    frame();

    sideResize.action = core::PointerAction::Move;
    sideResize.button = core::PointerButton::None;
    sideResize.x = 452.0;
    assert(routePointer(sideResize, sideScroll).x < 0.0);
    assert(host.contentBounds().width == 450.0f);
    frame();

    sideResize.action = core::PointerAction::Release;
    sideResize.button = core::PointerButton::Left;
    sideResize.buttons.set(core::PointerButton::Left, false);
    routePointer(sideResize, sideScroll);
    frame();

    chooseDock(2);
    assert(host.dockPosition() == DockPosition::Bottom);
    assert(host.contentBounds().height < static_cast<float>(kWindowHeight));

    // The panel asks the app layer for the element tree only while it shows the tab
    // that displays it. The toolbar tabs are hit where a user would click them.
    {
        const double tabY = static_cast<double>(host.contentBounds().height) + 16.0;
        assert(!host.wantsElementTree());
        clickPanel(120.0, tabY);
        assert(host.activeTab() == DevtoolsTab::Performance);
        assert(!host.wantsElementTree());

        clickPanel(180.0, tabY);
        assert(host.activeTab() == DevtoolsTab::Elements);
        assert(host.wantsElementTree());

        core::dsl::runtime::ElementTreeSnapshot tree;
        tree.revision = 7;
        tree.nodes.push_back({"page.root", core::dsl::ElementKind::Column, {}, 0, 0, false, false, false,
                              {0.0f, 0.0f, 800.0f, 600.0f}});
        host.setElementTree(tree);
        assert(frame());                       // the new tree is worth a repaint
        assert(host.elementTree().revision == 7);
        assert(host.elementTree().nodes.size() == 1);
        assert(host.elementTree().nodes[0].id == "page.root");

        // The tab lists what the app published: one row per visible node.
        assert(countPanelRows(host) == 1);
        assert(!hasPanelElement(host, "elements.details"));

        tree.revision = 8;
        tree.nodes.push_back({"page.title", core::dsl::ElementKind::Text, "Hello", 1, 0, false, false, false,
                              {0.0f, 0.0f, 120.0f, 20.0f}});
        tree.nodes.push_back({"page.ok", core::dsl::ElementKind::Rect, {}, 1, 0, false, true, false,
                              {0.0f, 20.0f, 64.0f, 32.0f}});
        host.setElementTree(tree);
        frame();
        // Every node with children starts collapsed, so three published nodes show
        // up as the root row alone until its disclosure is opened.
        assert(countPanelRows(host) == 1);

        // Clicking a row selects that element and shows its details. A panel state
        // change lands on the frame after the click, so the frame is run first.
        const double rowY = host.contentBounds().height + theme.toolbarHeight + 1.0 +
                            theme.elementRowHeight * 0.5;
        clickPanel(200.0, rowY);
        assert(host.selectedElement() == "page.root");
        frame();

        // The property area asks the app layer for the selected element, and the app
        // layer hands the values back. Nothing is published before that.
        assert(host.propertiesElement() == "page.root");
        assert(!host.properties().active);
        assert(!hasPanelElement(host, "elements.properties.footer.inner.text"));

        core::dsl::runtime::DebugElementProperties values;
        values.active = true;
        values.id = "page.root";
        values.kind = core::dsl::ElementKind::Column;
        values.frame = {0.0f, 0.0f, 800.0f, 600.0f};
        values.color = {0.2f, 0.4f, 0.6f, 1.0f};
        values.opacity = 0.5f;
        values.radius = 6.0f;
        host.setElementProperties(values);
        frame();
        assert(host.properties().active);
        assert(host.properties().radius == 6.0f);
        assert(hasPanelElement(host, "elements.properties.footer.inner.text"));
        assert(!hasPanelElement(host, "elements.properties.footer.inner.reset"));

        // Every property the runtime can write has exactly one row the panel can show.
        {
            const std::vector<core::dsl::runtime::DebugPropertyId>& ids = elementPropertyIds();
            assert(ids.size() == static_cast<std::size_t>(core::dsl::runtime::kDebugPropertyCount));
            std::vector<core::dsl::runtime::DebugPropertyId> sorted = ids;
            std::sort(sorted.begin(), sorted.end());
            assert(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
        }

        // A drag on a number editor queues one edit for the app layer to write. The
        // first slider row is the first number property the panel declares, opacity.
        {
            const std::vector<core::Rect> sliders = panelElementFrames(host, ".slider");
            assert(!sliders.empty());
            const core::Rect slider = sliders.front();
            assert(slider.width > 0.0f && slider.height > 0.0f);
            // Away from the value the row shows: a slider only reports a change.
            dragPanel(slider.x + slider.width * 0.9, slider.y + slider.height * 0.5,
                      slider.x + slider.width * 0.15, slider.y + slider.height * 0.5);
            DevtoolsHost::ElementPropertyEdit edit;
            assert(takeLastElementPropertyEdit(host, edit));
            assert(edit.id == "page.root");
            assert(edit.property == core::dsl::runtime::DebugPropertyId::Opacity);
            assert(!edit.clear);
            assert(edit.number >= 0.0f && edit.number <= 1.0f);
        }

        // A colour row opens its channels under itself, and dragging a channel queues a
        // colour edit: the first slider row is the hue of the colour being edited.
        {
            const core::Rect swatch = panelElementFrame(host, ".swatch");
            clickPanel(swatch.x + swatch.width * 0.5, swatch.y + swatch.height * 0.5);
            frame();
            const std::vector<core::Rect> channels = panelElementFrames(host, ".slider");
            assert(!channels.empty());
            const core::Rect hue = channels.front();
            dragPanel(hue.x + hue.width * 0.9, hue.y + hue.height * 0.5, hue.x + hue.width * 0.15,
                      hue.y + hue.height * 0.5);
            DevtoolsHost::ElementPropertyEdit edit;
            assert(takeLastElementPropertyEdit(host, edit));
            assert(edit.id == "page.root");
            assert(edit.property == core::dsl::runtime::DebugPropertyId::Color);
            assert(!edit.clear);
        }

        // The footer counts what a debug session replaced and offers to put it all back.
        host.setElementPropertyOverrideCount(2);
        assert(host.propertyOverrideCount() == 2);
        frame();
        {
            const core::Rect reset = panelElementFrame(host, "elements.properties.footer.inner.reset");
            clickPanel(reset.x + reset.width * 0.5, reset.y + reset.height * 0.5);
            DevtoolsHost::ElementPropertyEdit edit;
            assert(takeLastElementPropertyEdit(host, edit));
            assert(edit.clear);
            assert(edit.id.empty());
        }

        // Hovering a row previews that element in the page.
        {
            core::PointerEvent overRow = pressAt(200.0, rowY);
            overRow.action = core::PointerAction::Move;
            overRow.button = core::PointerButton::None;
            overRow.buttons = {};
            core::ScrollEvent hoverScroll;
            routePointer(overRow, hoverScroll);
            frame();
            assert(host.hoveredElement() == "page.root");

            // Leaving the list takes the preview back without touching the selection.
            core::PointerEvent overPage = overRow;
            overPage.y = 20.0;
            routePointer(overPage, hoverScroll);
            frame();
            assert(host.hoveredElement().empty());
            assert(host.selectedElement() == "page.root");
        }

        // The disclosure glyph of the root opens its subtree, and clicking it again
        // puts the subtree back. Leaf rows have no glyph to click.
        clickPanel(theme.elementDisclosureSize * 0.5, rowY);
        assert(host.expandedElements().size() == 1);
        assert(host.expandedElements()[0] == "page.root");
        frame();
        assert(countPanelRows(host) == 3);

        clickPanel(theme.elementDisclosureSize * 0.5, rowY);
        assert(host.expandedElements().empty());
        frame();
        assert(countPanelRows(host) == 1);

        clickPanel(120.0, tabY);
        assert(host.activeTab() == DevtoolsTab::Performance);
        assert(!host.wantsElementTree());
        // Leaving the tab drops the preview, so a page is never marked while nobody
        // looks at the tree, while the selection itself is remembered.
        assert(host.hoveredElement().empty());
        // The property area only asks for values while it is on screen.
        assert(host.propertiesElement().empty());
        assert(host.selectedElement() == "page.root");
    }

    // A floating panel leaves the whole window to the page and opens its window.
    chooseDock(0);
    assert(host.dockPosition() == DockPosition::Floating);
    assert(host.contentBounds().width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds().height == static_cast<float>(kWindowHeight));
    assert(detachedOpens == 1);

    // The detached window composes the same panel and can dock it back.
    core::dsl::Runtime detachedRuntime;
    // A detached panel is a normal window, so the frame loop drives it with a real
    // frame delta. That is also why its scrolling and transitions work.
    const auto frameDetached = [&] {
        return detachedRuntime.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
    };
    const auto composeDetached = [&] {
        detachedRuntime.compose("eui.devtools.detached", 640.0f, 420.0f,
            [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
                host.composeDetached(ui, screen);
            });
    };
    const auto clickDetached = [&](double x, double y) {
        core::PointerEvent event = pressAt(x, y);
        detachedRuntime.pushPointerEvent(event);
        frameDetached();
        event.action = core::PointerAction::Release;
        event.buttons.set(core::PointerButton::Left, false);
        detachedRuntime.pushPointerEvent(event);
        frameDetached();
        if (detachedRuntime.composeRequested()) {
            composeDetached();
            frameDetached();
        }
    };
    composeDetached();
    frameDetached();

    const double detachedMoreX = 640.0 - 48.0;
    const double detachedMenuX = 640.0 - theme.menuWidth - 36.0 + 10.0;
    const double detachedRowY = theme.toolbarHeight + 6.0 + theme.menuPadding +
                                (2.0 + 0.5) * theme.menuRowHeight;

    // Clicking the panel background dismisses the menu and selects nothing.
    clickDetached(detachedMoreX, 16.0);
    clickDetached(300.0, 200.0);
    clickDetached(detachedMenuX, detachedRowY);
    assert(host.dockPosition() == DockPosition::Floating);

    clickDetached(detachedMoreX, 16.0);
    clickDetached(detachedMenuX, detachedRowY);
    assert(host.dockPosition() == DockPosition::Bottom);
    assert(detachedCloses == 1);
    assert(host.visible());

    // Closing the detached window while the panel is floating hides the panel.
    chooseDock(0);
    assert(detachedOpens == 2);
    host.detachedWindowClosed();
    assert(!host.visible());
    assert(host.contentHeight() == kWindowHeight);

    detachedRuntime.shutdown(false);
    host.shutdown();
    assert(!host.visible());
    return 0;
}

#else

static_assert(!HasOverlayHooks<core::dsl::Runtime>::value, "Release Runtime must not carry the hooks");

int main() { return 0; }

#endif
