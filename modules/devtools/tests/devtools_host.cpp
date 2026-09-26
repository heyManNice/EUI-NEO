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
#include "modules/devtools/devtools_theme.h"

#include <cassert>
#include <vector>

static_assert(HasOverlayHooks<core::dsl::Runtime>::value, "Debug Runtime hooks are required");

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr float kDpiScale = 1.0f;

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

    // A hidden panel leaves the whole window to the page.
    assert(!host.update(kWindowWidth, kWindowHeight, kDpiScale));
    assert(!host.visible());
    assert(host.contentBounds().x == 0.0f);
    assert(host.contentBounds().width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds().height == static_cast<float>(kWindowHeight));
    assert(host.contentHeight() == kWindowHeight);

    // The hotkey toggles the panel and only reacts to presses.
    assert(!host.handleHotkey(F12Key(core::KeyAction::Release)));
    assert(host.handleHotkey(F12Key(core::KeyAction::Press)));
    assert(host.visible());
    assert(host.update(kWindowWidth, kWindowHeight, kDpiScale));
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
    host.update(kWindowWidth, kWindowHeight, kDpiScale);

    // A new sample repaints the panel once.
    app::PerformanceSnapshot sample;
    sample.revision = 1;
    sample.framesPerSecond = 60.0;
    host.setPerformanceSnapshot(sample);
    assert(host.update(kWindowWidth, kWindowHeight, kDpiScale));
    host.setPerformanceSnapshot(sample);
    assert(!host.update(kWindowWidth, kWindowHeight, kDpiScale));

    // Routes one event through the panel and returns the copy the page sees. The
    // caller keeps its own coordinates, because an event the panel consumes is
    // rewritten for the page.
    const auto routePointer = [&](const core::PointerEvent& event, core::ScrollEvent& scroll) {
        std::vector<core::PointerEvent> events{event};
        host.filterInput(events, scroll);
        return events.front();
    };
    const auto clickPanel = [&](double x, double y) {
        core::PointerEvent press = pressAt(x, y);
        core::ScrollEvent scroll;
        routePointer(press, scroll);
        host.update(kWindowWidth, kWindowHeight, kDpiScale);

        press.action = core::PointerAction::Release;
        press.buttons.set(core::PointerButton::Left, false);
        routePointer(press, scroll);
        host.update(kWindowWidth, kWindowHeight, kDpiScale);
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
    const bool presentedAfterInteraction = host.update(kWindowWidth, kWindowHeight, kDpiScale) ||
                                           host.update(kWindowWidth, kWindowHeight, kDpiScale);
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
    host.update(kWindowWidth, kWindowHeight, kDpiScale);

    sideResize.action = core::PointerAction::Move;
    sideResize.button = core::PointerButton::None;
    sideResize.x = 452.0;
    assert(routePointer(sideResize, sideScroll).x < 0.0);
    assert(host.contentBounds().width == 450.0f);
    host.update(kWindowWidth, kWindowHeight, kDpiScale);

    sideResize.action = core::PointerAction::Release;
    sideResize.button = core::PointerButton::Left;
    sideResize.buttons.set(core::PointerButton::Left, false);
    routePointer(sideResize, sideScroll);
    host.update(kWindowWidth, kWindowHeight, kDpiScale);

    chooseDock(2);
    assert(host.dockPosition() == DockPosition::Bottom);
    assert(host.contentBounds().height < static_cast<float>(kWindowHeight));

    // A floating panel leaves the whole window to the page and opens its window.
    chooseDock(0);
    assert(host.dockPosition() == DockPosition::Floating);
    assert(host.contentBounds().width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds().height == static_cast<float>(kWindowHeight));
    assert(detachedOpens == 1);

    // The detached window composes the same panel and can dock it back.
    core::dsl::Runtime detachedRuntime;
    const auto composeDetached = [&] {
        detachedRuntime.compose("eui.devtools.detached", 640.0f, 420.0f,
            [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
                host.composeDetached(ui, screen);
            });
    };
    const auto clickDetached = [&](double x, double y) {
        core::PointerEvent event = pressAt(x, y);
        detachedRuntime.pushPointerEvent(event);
        detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        event.action = core::PointerAction::Release;
        event.buttons.set(core::PointerButton::Left, false);
        detachedRuntime.pushPointerEvent(event);
        detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        if (detachedRuntime.composeRequested()) {
            composeDetached();
            detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        }
    };
    composeDetached();
    detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);

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
