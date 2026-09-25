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

    host.shutdown();
    core::detail::inputQueues().erase(window);
    core::detail::pointerStates().erase(window);
}

#else

static_assert(!HasDevtoolsHooks<core::dsl::Runtime>::value);

int main() { return 0; }

#endif
