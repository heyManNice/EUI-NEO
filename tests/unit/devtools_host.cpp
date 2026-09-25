#include "core/dsl_runtime.h"

#include <type_traits>

template <typename T, typename = void>
struct HasDevtoolsHooks : std::false_type {};

template <typename T>
struct HasDevtoolsHooks<T, std::void_t<
    decltype(&T::setInputFilter),
    decltype(&T::setOverlayRenderer),
    decltype(&T::renderDirectOverlay)>> : std::true_type {};

#if defined(EUI_DEBUG_BUILD) && defined(EUI_DEVTOOLS_AVAILABLE)

static_assert(HasDevtoolsHooks<core::dsl::Runtime>::value);

#include "core/debug/devtools_host.h"
#include "core/input/input_state.h"

#include <cassert>

int main() {
    int windowToken = 0;
    core::window::Handle window = &windowToken;
    core::debug::DevtoolsHost host;

    core::queueKeyInput(window, {core::InputKey::F12, core::KeyAction::Press, {}, 0});
    assert(host.beginFrame(window, 800, 600, 1.0f, true));
    assert(host.visible());
    assert(host.contentHeight() < 600);
    assert(core::detail::inputQueue(window).keys.empty());
    assert(host.update());
    assert(!host.update());

    core::PointerEvent toolbarPointer;
    toolbarPointer.x = 20.0;
    toolbarPointer.y = static_cast<double>(host.contentHeight() + 20);
    core::ScrollEvent toolbarScroll;
    std::vector<core::PointerEvent> toolbarEvents{toolbarPointer};
    host.filterInput(toolbarEvents, toolbarScroll);
    assert(host.update());
    assert(!host.update());

    const int initialContentHeight = host.contentHeight();
    core::PointerEvent resizePress;
    resizePress.x = 120.0;
    resizePress.y = static_cast<double>(initialContentHeight + 2);
    resizePress.action = core::PointerAction::Press;
    resizePress.button = core::PointerButton::Left;
    resizePress.buttons.set(core::PointerButton::Left, true);
    std::vector<core::PointerEvent> resizePressEvents{resizePress};
    core::ScrollEvent resizeScroll;
    host.filterInput(resizePressEvents, resizeScroll);
    assert(resizePressEvents.front().x < 0.0);

    core::PointerEvent resizeMove = resizePress;
    resizeMove.action = core::PointerAction::Move;
    resizeMove.button = core::PointerButton::None;
    resizeMove.y -= 100.0;
    std::vector<core::PointerEvent> resizeMoveEvents{resizeMove};
    host.filterInput(resizeMoveEvents, resizeScroll);
    assert(host.contentHeight() == initialContentHeight - 100);
    assert(resizeMoveEvents.front().x < 0.0);
    assert(host.update());

    core::PointerEvent resizeRelease = resizeMove;
    resizeRelease.y = 0.0;
    resizeRelease.action = core::PointerAction::Release;
    resizeRelease.button = core::PointerButton::Left;
    resizeRelease.buttons.set(core::PointerButton::Left, false);
    std::vector<core::PointerEvent> resizeReleaseEvents{resizeRelease};
    host.filterInput(resizeReleaseEvents, resizeScroll);
    assert(host.contentHeight() == 120);
    assert(resizeReleaseEvents.front().x < 0.0);
    assert(host.update());

    resizePress.y = static_cast<double>(host.contentHeight() + 2);
    resizePressEvents = {resizePress};
    host.filterInput(resizePressEvents, resizeScroll);
    resizeMove.y = resizePress.y + 1000.0;
    resizeMoveEvents = {resizeMove};
    host.filterInput(resizeMoveEvents, resizeScroll);
    assert(host.contentHeight() == 420);
    resizeRelease = resizeMove;
    resizeRelease.action = core::PointerAction::Release;
    resizeRelease.button = core::PointerButton::Left;
    resizeRelease.buttons.set(core::PointerButton::Left, false);
    resizeReleaseEvents = {resizeRelease};
    host.filterInput(resizeReleaseEvents, resizeScroll);
    assert(resizeReleaseEvents.front().x < 0.0);

    core::queueKeyInput(window, {core::InputKey::F12, core::KeyAction::Repeat, {}, 0});
    core::queueKeyInput(window, {core::InputKey::F12, core::KeyAction::Release, {}, 0});
    core::queueKeyInput(window, {core::InputKey::A, core::KeyAction::Press, {}, 0});
    assert(!host.beginFrame(window, 800, 600, 1.0f, true));
    assert(host.visible());
    assert(core::detail::inputQueue(window).keys.size() == 1);
    assert(core::detail::inputQueue(window).keys.front().key == core::InputKey::A);

    core::PointerEvent panelPointer;
    panelPointer.x = 100.0;
    panelPointer.y = 590.0;
    core::ScrollEvent panelScroll{0.0, 1.0};
    std::vector<core::PointerEvent> panelEvents{panelPointer};
    host.filterInput(panelEvents, panelScroll);
    assert(panelEvents.front().x < 0.0);
    assert(panelEvents.front().y < 0.0);
    assert(!panelScroll.active());

    core::PointerEvent appPointer;
    appPointer.x = 100.0;
    appPointer.y = 20.0;
    core::ScrollEvent appScroll{0.0, 1.0};
    std::vector<core::PointerEvent> appEvents{appPointer};
    host.filterInput(appEvents, appScroll);
    assert(appEvents.front().x == 100.0);
    assert(appEvents.front().y == 20.0);
    assert(appScroll.active());

    core::queueKeyInput(window, {core::InputKey::F12, core::KeyAction::Press, {}, 0});
    assert(host.beginFrame(window, 800, 600, 1.0f, true));
    assert(!host.visible());
    assert(host.contentHeight() == 600);

    core::KeyModifiers shortcutModifiers;
    shortcutModifiers.control = true;
    shortcutModifiers.shift = true;
    core::queueKeyInput(window, {core::InputKey::I, core::KeyAction::Press, shortcutModifiers, 0});
    assert(host.beginFrame(window, 800, 600, 1.0f, true));
    assert(host.visible());
    core::queueKeyInput(window, {core::InputKey::I, core::KeyAction::Repeat, shortcutModifiers, 0});
    core::queueKeyInput(window, {core::InputKey::I, core::KeyAction::Release, shortcutModifiers, 0});
    assert(!host.beginFrame(window, 800, 600, 1.0f, true));
    assert(host.visible());

    assert(host.update());
    core::PointerEvent morePress;
    morePress.x = 752.0;
    morePress.y = static_cast<double>(host.contentHeight() + 16);
    morePress.action = core::PointerAction::Press;
    morePress.button = core::PointerButton::Left;
    morePress.buttons.set(core::PointerButton::Left, true);
    std::vector<core::PointerEvent> moreEvents{morePress};
    core::ScrollEvent moreScroll;
    host.filterInput(moreEvents, moreScroll);
    assert(moreEvents.front().x < 0.0);
    host.update();

    core::PointerEvent moreRelease = morePress;
    moreRelease.action = core::PointerAction::Release;
    moreRelease.buttons.set(core::PointerButton::Left, false);
    moreEvents = {moreRelease};
    host.filterInput(moreEvents, moreScroll);
    assert(host.update());
    assert(host.visible());
    assert(host.contentHeight() < 600);
    assert(!host.update());

    core::PointerEvent closePress;
    closePress.x = 780.0;
    closePress.y = static_cast<double>(host.contentHeight() + 16);
    closePress.action = core::PointerAction::Press;
    closePress.button = core::PointerButton::Left;
    closePress.buttons.set(core::PointerButton::Left, true);
    std::vector<core::PointerEvent> closePressEvents{closePress};
    core::ScrollEvent closeScroll;
    host.filterInput(closePressEvents, closeScroll);
    assert(closePressEvents.front().x < 0.0);
    host.update();
    assert(host.visible());

    core::PointerEvent closeRelease = closePress;
    closeRelease.action = core::PointerAction::Release;
    closeRelease.buttons.set(core::PointerButton::Left, false);
    std::vector<core::PointerEvent> closeReleaseEvents{closeRelease};
    host.filterInput(closeReleaseEvents, closeScroll);
    assert(closeReleaseEvents.front().x < 0.0);
    assert(host.update());
    assert(!host.visible());
    assert(host.contentHeight() == 600);

    host.shutdown();

    core::debug::DevtoolsHost dockHost;
    int detachedOpens = 0;
    int detachedCloses = 0;
    dockHost.setDetachedWindowOpener([&] { ++detachedOpens; });
    dockHost.setDetachedWindowCloser([&] { ++detachedCloses; });
    core::queueKeyInput(window, {core::InputKey::F12, core::KeyAction::Press, {}, 0});
    assert(dockHost.beginFrame(window, 800, 600, 1.0f, true));
    assert(dockHost.update());

    const auto clickDocked = [&](double x, double y) {
        core::PointerEvent press;
        press.x = x;
        press.y = y;
        press.action = core::PointerAction::Press;
        press.button = core::PointerButton::Left;
        press.buttons.set(core::PointerButton::Left, true);
        std::vector<core::PointerEvent> events{press};
        core::ScrollEvent scroll;
        dockHost.filterInput(events, scroll);
        dockHost.update();

        press.action = core::PointerAction::Release;
        press.buttons.set(core::PointerButton::Left, false);
        events = {press};
        dockHost.filterInput(events, scroll);
        dockHost.update();
    };
    const auto chooseDock = [&](int row) {
        const core::Rect content = dockHost.contentBounds();
        const bool side = dockHost.dockPosition() != core::debug::DockPosition::Bottom;
        const double panelX = side && dockHost.dockPosition() == core::debug::DockPosition::Right
            ? content.width : 0.0;
        const double panelY = side ? 0.0 : content.height;
        const double panelWidth = side ? 800.0 - content.width : 800.0;
        clickDocked(panelX + panelWidth - 48.0, panelY + 16.0);
        clickDocked(panelX + panelWidth - 136.0 - 36.0 + 10.0,
                    panelY + 31.0 + 6.0 + 2.0 + (static_cast<double>(row) + 0.5) * 22.67);
    };

    chooseDock(1);
    assert(dockHost.dockPosition() == core::debug::DockPosition::Left);
    assert(dockHost.contentBounds().x == 300.0f);
    assert(dockHost.contentBounds().width == 500.0f);
    assert(dockHost.contentBounds().height == 600.0f);

    chooseDock(3);
    assert(dockHost.dockPosition() == core::debug::DockPosition::Right);
    assert(dockHost.contentBounds().x == 0.0f);
    assert(dockHost.contentBounds().width == 500.0f);

    core::PointerEvent sideResize;
    sideResize.x = 502.0;
    sideResize.y = 200.0;
    sideResize.action = core::PointerAction::Press;
    sideResize.button = core::PointerButton::Left;
    sideResize.buttons.set(core::PointerButton::Left, true);
    std::vector<core::PointerEvent> sideResizeEvents{sideResize};
    core::ScrollEvent sideResizeScroll;
    dockHost.filterInput(sideResizeEvents, sideResizeScroll);
    assert(sideResizeEvents.front().x < 0.0);
    dockHost.update();
    sideResize.action = core::PointerAction::Move;
    sideResize.button = core::PointerButton::None;
    sideResize.x = 452.0;
    sideResizeEvents = {sideResize};
    dockHost.filterInput(sideResizeEvents, sideResizeScroll);
    assert(dockHost.contentBounds().width == 450.0f);
    dockHost.update();
    sideResize.action = core::PointerAction::Release;
    sideResize.button = core::PointerButton::Left;
    sideResize.buttons.set(core::PointerButton::Left, false);
    sideResizeEvents = {sideResize};
    dockHost.filterInput(sideResizeEvents, sideResizeScroll);
    dockHost.update();

    chooseDock(2);
    assert(dockHost.dockPosition() == core::debug::DockPosition::Bottom);
    assert(dockHost.contentBounds().height < 600.0f);

    chooseDock(0);
    assert(dockHost.dockPosition() == core::debug::DockPosition::Floating);
    assert(dockHost.contentBounds().width == 800.0f);
    assert(dockHost.contentBounds().height == 600.0f);
    assert(detachedOpens == 1);

    core::dsl::Runtime detachedRuntime;
    const auto composeDetached = [&] {
        detachedRuntime.compose("eui.devtools.detached", 640.0f, 420.0f,
            [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
                dockHost.composeDetached(ui, screen);
            });
    };
    const auto clickDetached = [&](double x, double y) {
        core::queuePointerButton(nullptr, x, y, core::PointerButton::Left, core::PointerAction::Press, {});
        detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        core::queuePointerButton(nullptr, x, y, core::PointerButton::Left, core::PointerAction::Release, {});
        detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        if (detachedRuntime.composeRequested()) {
            composeDetached();
            detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
        }
    };
    composeDetached();
    detachedRuntime.update(nullptr, 0.0f, 1.0f, 1.0f);
    clickDetached(592.0, 16.0);
    clickDetached(478.0, 95.0);
    assert(dockHost.dockPosition() == core::debug::DockPosition::Bottom);
    assert(detachedCloses == 1);
    dockHost.detachedWindowClosed();
    assert(dockHost.visible());
    assert(dockHost.contentBounds().height < 600.0f);
    detachedRuntime.shutdown(false);
    dockHost.shutdown();
    core::detail::inputQueues().erase(window);
    core::detail::pointerStates().erase(window);
}

#else

static_assert(!HasDevtoolsHooks<core::dsl::Runtime>::value);

int main() { return 0; }

#endif
