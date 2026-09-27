#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
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
#include <cmath>
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
int countPanelRows(const modules::devtools::DevtoolsHost& host) {    const std::string prefix = "elements.list.slot.";
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

// The row slot of the property with this label, read from the composed panel itself: a
// test that wants a row finds it by its name instead of counting rows, because which rows
// are on screen depends on what the panel is showing.
int panelPropertySlot(const modules::devtools::DevtoolsHost& host, const std::string& label) {
    const std::string prefix = "elements.properties.list.slot.";
    for (const core::dsl::runtime::ElementTreeNode& node : host.panelElementTree().nodes) {
        if (node.kind != core::dsl::ElementKind::Text || node.text != label) {
            continue;
        }
        const std::size_t at = node.id.find(prefix);
        if (at == std::string::npos || node.id.find(".label") == std::string::npos) {
            continue;
        }
        return std::atoi(node.id.substr(at + prefix.size()).c_str());
    }
    return -1;
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
    // The panel is attached to no page here: this test drives the panel itself, and the
    // window services the app layer would provide are recorded instead.
    app::detail::OverlayWindows windows;
    windows.open = [&](const app::detail::OverlayWindowRequest&) { ++detachedOpens; };
    windows.close = [&] { ++detachedCloses; };
    host.attach(nullptr, windows);

    // One app frame for the window the panel is docked into. A test that needs a
    // differently scaled window drives the host with its own factor.
    const auto frame = [&](float dpiScale = kDpiScale) {
        return host.frame(kWindowWidth, kWindowHeight, dpiScale, kFrameSeconds);
    };

    // A hidden panel leaves the whole window to the page.
    assert(!frame());
    assert(!host.visible());
    assert(!host.wantsElementTree());
    assert(host.hoveredElement().empty());
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).x == 0.0f);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height == static_cast<float>(kWindowHeight));
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
    const auto dragPanel = [&](double fromX, double fromY, double toX, double toY,
                               float dpiScale = kDpiScale) {
        core::ScrollEvent scroll;
        core::PointerEvent press = pressAt(fromX, fromY);
        routePointer(press, scroll);
        frame(dpiScale);

        press.action = core::PointerAction::Move;
        press.x = toX;
        press.y = toY;
        routePointer(press, scroll);
        frame(dpiScale);

        press.action = core::PointerAction::Release;
        press.buttons.set(core::PointerButton::Left, false);
        routePointer(press, scroll);
        frame(dpiScale);
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
    clickPanel(kWindowWidth - 48.0, host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height + 16.0);
    assert(core::platform::consumeFrameRequest());
    assert(!core::platform::consumeUiUpdate());

    // The frame that carries the interaction still draws the tree the panel already
    // had, and a following frame presents the new state. Composing a second time
    // inside the interaction frame would leave the panel's primitive bounds unusable
    // for that frame's draw, so the renderer culls the whole panel and the panel area
    // shows the cleared render cache for one frame.
    const bool presentedAfterInteraction = frame() || frame();
    assert(presentedAfterInteraction);

    clickPanel(kWindowWidth - 48.0, host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height + 16.0);

    // The more menu moves the panel to another edge.
    const auto chooseDock = [&](int row) {
        const core::Rect content = host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale);
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
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).x == 300.0f);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).width == 500.0f);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height == static_cast<float>(kWindowHeight));

    chooseDock(3);
    assert(host.dockPosition() == DockPosition::Right);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).x == 0.0f);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).width == 500.0f);

    // A side panel resizes along x.
    core::PointerEvent sideResize = pressAt(502.0, 200.0);
    core::ScrollEvent sideScroll;
    assert(routePointer(sideResize, sideScroll).x < 0.0);
    frame();

    sideResize.action = core::PointerAction::Move;
    sideResize.button = core::PointerButton::None;
    sideResize.x = 452.0;
    assert(routePointer(sideResize, sideScroll).x < 0.0);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).width == 450.0f);
    frame();

    sideResize.action = core::PointerAction::Release;
    sideResize.button = core::PointerButton::Left;
    sideResize.buttons.set(core::PointerButton::Left, false);
    routePointer(sideResize, sideScroll);
    frame();

    chooseDock(2);
    assert(host.dockPosition() == DockPosition::Bottom);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height < static_cast<float>(kWindowHeight));

    // The panel asks the app layer for the element tree only while it shows the tab
    // that displays it. The toolbar tabs are hit where a user would click them.
    {
        const double tabY = static_cast<double>(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height) + 16.0;
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

        // Nothing is selected yet, so the property area and its divider take no room
        // at all: the tree keeps the whole tab.
        assert(!hasPanelElement(host, "elements.properties"));
        assert(!hasPanelElement(host, "elements.properties.handle"));

        // Clicking a row selects that element and shows its properties. A panel state
        // change lands on the frame after the click, so the frame is run first.
        const double rowY = host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height + theme.toolbarHeight + 1.0 +
                            theme.elementRowHeight * 0.5;
        clickPanel(200.0, rowY);
        assert(host.selectedElement() == "page.root");
        frame();

        // The property area asks the app layer for the selected element, and the app
        // layer hands the values back. Nothing is published before that.
        assert(host.propertiesElement() == "page.root");
        assert(!host.properties().active);
        assert(!hasPanelElement(host, "elements.properties.footer.inner.text"));
        core::dsl::runtime::ElementValues values;
        values.active = true;
        values.id = "page.root";
        // A rect is the element that carries the whole box, so the rows a rect offers are
        // the ones this test exercises. What a container shows instead is asserted with
        // the other kinds below.
        values.kind = core::dsl::ElementKind::Rect;
        values.frame = {0.0f, 0.0f, 800.0f, 600.0f};
        values.setField(core::dsl::runtime::ElementField::Color,
                        core::dsl::runtime::fieldValueOf(core::Color{0.2f, 0.4f, 0.6f, 1.0f}));
        values.setField(core::dsl::runtime::ElementField::Opacity, core::dsl::runtime::fieldValueOf(0.5f));
        values.setField(core::dsl::runtime::ElementField::Radius, core::dsl::runtime::fieldValueOf(6.0f));
        host.setElementProperties(values);
        frame();
        assert(host.properties().active);
        assert(host.properties().field(core::dsl::runtime::ElementField::Radius).number == 6.0f);
        assert(hasPanelElement(host, "elements.properties.footer.inner.text"));
        assert(!hasPanelElement(host, "elements.properties.footer.inner.reset"));

        // The property list scrolls inside the whole area, so its scrollbar ends on
        // the panel edge instead of a padding away from it, like the tree's. The rows
        // keep the padding, so they stop short of the scrollbar.
        {
            const core::Rect scroll = panelElementFrame(host, "elements.properties.list.scroll");
            const core::Rect area = panelElementFrame(host, "elements.properties.background");
            assert(std::fabs((scroll.x + scroll.width) - (area.x + area.width)) < 0.5f);
            for (const core::Rect& row : panelElementFrames(host, "elements.properties.list.slot.0.row")) {
                assert(row.x + row.width <= scroll.x);
            }
        }

        // Every property the runtime can write has exactly one row the panel can show.
        {
            const std::vector<core::dsl::runtime::ElementField>& ids = elementPropertyIds();
            assert(ids.size() == static_cast<std::size_t>(core::dsl::runtime::kElementFieldCount));
            std::vector<core::dsl::runtime::ElementField> sorted = ids;
            std::sort(sorted.begin(), sorted.end());
            assert(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

            // A row only appears for the kinds the property can change something on: a
            // rect carries the box, a text element carries its own colour instead, and a
            // layout container paints nothing but its opacity.
            const auto has = [](const std::vector<core::dsl::runtime::ElementField>& list,
                                core::dsl::runtime::ElementField id) {
                return std::find(list.begin(), list.end(), id) != list.end();
            };
            const std::vector<core::dsl::runtime::ElementField> box =
                elementPropertyIds(core::dsl::ElementKind::Rect);
            const std::vector<core::dsl::runtime::ElementField> text =
                elementPropertyIds(core::dsl::ElementKind::Text);
            const std::vector<core::dsl::runtime::ElementField> container =
                elementPropertyIds(core::dsl::ElementKind::Column);
            assert(container.size() == 1 && has(container, core::dsl::runtime::ElementField::Opacity));
            assert(text.size() == 2 && has(text, core::dsl::runtime::ElementField::TextColor));
            assert(!has(text, core::dsl::runtime::ElementField::BorderWidth));
            assert(box.size() == static_cast<std::size_t>(core::dsl::runtime::kElementFieldCount) - 1);
            assert(has(box, core::dsl::runtime::ElementField::BorderWidth));
            assert(has(box, core::dsl::runtime::ElementField::ShadowSpread));
            assert(has(box, core::dsl::runtime::ElementField::GradientStart));
            assert(!has(box, core::dsl::runtime::ElementField::TextColor));
            for (core::dsl::runtime::ElementField id : box) {
                assert(has(ids, id));
            }
        }

        // The same element as a layout container: the box rows are gone from the area, so
        // nothing that would change nothing on it is offered.
        {
            assert(hasPanelElement(host, ".swatch"));
            assert(hasPanelElement(host, ".switch"));
            core::dsl::runtime::ElementValues container = values;
            container.kind = core::dsl::ElementKind::Column;
            host.setElementProperties(container);
            frame();
            assert(!hasPanelElement(host, ".swatch"));
            assert(!hasPanelElement(host, ".switch"));
            assert(!hasPanelElement(host, ".shadow"));
            // Opacity applies to every element, so its row is still there.
            assert(hasPanelElement(host, ".slider"));
            host.setElementProperties(values);
            frame();
            assert(hasPanelElement(host, ".swatch"));
        }

        // Every property row puts its control in the same column and ends it on the same
        // line: the slider of a number row, the track of a switch, the box of a colour and
        // the value text beside them. A smaller scale factor gives the panel a taller
        // logical area, which is what brings the rows far enough down the list — the first
        // switch is the shadow flag — into the composed window.
        {
            frame(0.5f);
            // The colour row is the first property the panel declares and the number row
            // under it is opacity; the summaries and the section header take slots 0 to 5.
            const core::Rect colourColumn = panelElementFrame(host, "elements.properties.list.slot.6.control");
            const core::Rect numberColumn = panelElementFrame(host, "elements.properties.list.slot.7.control");
            assert(std::fabs(colourColumn.x - numberColumn.x) < 0.5f);
            assert(std::fabs(colourColumn.width - numberColumn.width) < 0.5f);

            const core::Rect slider = panelElementFrame(host, ".slider");
            const core::Rect track = panelElementFrame(host, ".switch.track");
            const core::Rect swatch = panelElementFrame(host, ".swatch.box");
            const float columnRight = numberColumn.x + numberColumn.width;
            assert(slider.x + slider.width < columnRight);      // the slider is inset inside it
            assert(std::fabs((slider.x + slider.width) - (track.x + track.width)) < 1.5f);
            assert(std::fabs((slider.x + slider.width) - (swatch.x + swatch.width)) < 1.5f);

            // The value column is one line as well, and it starts to the right of the
            // controls: the hex of the colour and the number of the slider end together.
            const core::Rect hex = panelElementFrame(host, "elements.properties.list.slot.6.value");
            const core::Rect number = panelElementFrame(host, "elements.properties.list.slot.7.value");
            assert(std::fabs((hex.x + hex.width) - (number.x + number.width)) < 0.5f);
            assert(hex.x > swatch.x + swatch.width);
            frame();
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
            assert(edit.field == core::dsl::runtime::ElementField::Opacity);
            assert(!edit.clear);
            assert(edit.value.kind == core::dsl::runtime::FieldKind::Number);
            assert(edit.value.number >= 0.0f && edit.value.number <= 1.0f);
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
            assert(edit.field == core::dsl::runtime::ElementField::Color);
            assert(!edit.clear);
            assert(edit.value.kind == core::dsl::runtime::FieldKind::Color);
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

        // The divider resizes the area: dragging it up gives the area more room, and
        // dragging down past the minimum stops there instead of collapsing the area.
        // The area's background spans it, so its frame is the height the divider set.
        {
            const float before = panelElementFrame(host, "elements.properties.background").height;
            assert(before > 0.0f);
            const core::Rect handle = panelElementFrame(host, "elements.properties.handle");
            assert(handle.height > 0.0f);
            dragPanel(handle.x + handle.width * 0.5, handle.y + handle.height * 0.5, handle.x + handle.width * 0.5,
                      handle.y + handle.height * 0.5 - 30.0);
            const float taller = panelElementFrame(host, "elements.properties.background").height;
            assert(taller > before);

            const core::Rect moved = panelElementFrame(host, "elements.properties.handle");
            // Far enough to reach the minimum, and short enough to stay inside the
            // window: a pointer that leaves the panel cancels the drag.
            const double dragDown = taller - theme.propertiesMinimumHeight + 20.0;
            dragPanel(moved.x + moved.width * 0.5, moved.y + moved.height * 0.5, moved.x + moved.width * 0.5,
                      moved.y + moved.height * 0.5 + dragDown);
            const float shorter = panelElementFrame(host, "elements.properties.background").height;
            assert(shorter < taller);
            assert(shorter >= theme.propertiesMinimumHeight);
        }

        // A pointer event is in framebuffer pixels while the area is measured in the
        // units the panel composes in, so a scaled window must not resize the area
        // faster than the pointer: on a 2x window a 10 pixel drag grows it by 5.
        {
            constexpr float kScaledDpi = 2.0f;
            frame(kScaledDpi);
            const float before = panelElementFrame(host, "elements.properties.background").height;
            const core::Rect handle = panelElementFrame(host, "elements.properties.handle");
            const double pointerX = (handle.x + handle.width * 0.5) * kScaledDpi;
            const double pointerY = (handle.y + handle.height * 0.5) * kScaledDpi;
            dragPanel(pointerX, pointerY, pointerX, pointerY - 10.0, kScaledDpi);
            const float taller = panelElementFrame(host, "elements.properties.background").height;
            assert(taller - before > 4.5f);
            assert(taller - before < 5.5f);
            // Put the window back the way the rest of the test expects it.
            frame();
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

        // The tree list spans the whole panel, so its scrollbar ends on the panel edge
        // and a row highlight reaches it instead of stopping a gap short, the way the
        // property list already behaves. The numbers keep the inset the row content
        // reserves for them, so they stop short of the scrollbar themselves.
        {
            const core::dsl::runtime::ElementTreeSnapshot narrow = host.elementTree();
            core::dsl::runtime::ElementTreeSnapshot wide;
            wide.revision = narrow.revision + 1;
            for (int index = 0; index < 40; ++index) {
                wide.nodes.push_back({"page.row" + std::to_string(index), core::dsl::ElementKind::Rect, {}, 0, 0,
                                      false, false, false, {0.0f, 0.0f, 10.0f, 10.0f}});
            }
            host.setElementTree(wide);
            frame();

            const core::Rect list = panelElementFrame(host, "elements.list");
            const core::Rect scroll = panelElementFrame(host, "elements.list.scroll");
            assert(countPanelRows(host) > 0);
            assert(std::fabs((scroll.x + scroll.width) - (list.x + list.width)) < 0.5f);

            const core::Rect highlight = panelElementFrame(host, "elements.list.slot.0.row.background");
            assert(std::fabs((highlight.x + highlight.width) - scroll.x) < 0.5f);

            const core::Rect numbers = panelElementFrame(host, "elements.list.slot.0.row.size");
            assert(std::fabs((numbers.x + numbers.width) - (scroll.x - theme.elementDetailsPadding)) < 0.5f);
            assert(numbers.x + numbers.width < highlight.x + highlight.width);

            host.setElementTree(narrow);
            frame();
        }

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

    // The arrow in the toolbar picks elements on the page: the panel owns the pointer,
    // the page is told the pointer left, the element under it is previewed while the
    // picker is armed, and the click that picks hands the element to the tree, which
    // opens to show it.
    chooseDock(2);
    assert(host.dockPosition() == DockPosition::Bottom);
    {
        const core::Rect arrow = panelElementFrame(host, "selectElement");
        clickPanel(arrow.x + arrow.width * 0.5, arrow.y + arrow.height * 0.5);
        assert(host.pickingElement());
        assert(host.activeTab() == DevtoolsTab::Elements);

        // The page never sees the pointer while the panel picks, so a pick does not
        // also press what is under it. The point is above the docked panel.
        core::PointerEvent press = pressAt(700.0, 60.0);
        core::ScrollEvent pickScroll;
        const core::PointerEvent routed = routePointer(press, pickScroll);
        assert(routed.x < 0.0 && routed.y < 0.0);
        assert(host.pickedPointer().x == 700.0);
        frame();

        // The app layer answers with the element under the pointer, which the page then
        // previews.
        host.setElementUnderPointer("page.title");
        assert(host.hoveredElement() == "page.title");

        core::PointerEvent release = press;
        release.action = core::PointerAction::Release;
        release.buttons.set(core::PointerButton::Left, false);
        routePointer(release, pickScroll);
        frame();
        host.setElementUnderPointer("page.title");
        frame();
        frame();

        // The pick landed: the picker turned itself off, the selection moved, the tree
        // opened the ancestor that hid the row, and the row is there to be seen.
        assert(!host.pickingElement());
        assert(host.selectedElement() == "page.title");
        assert(host.expandedElements().size() == 1);
        assert(host.expandedElements()[0] == "page.root");
        assert(countPanelRows(host) == 3);

        // Escape leaves the picker without touching the selection.
        const core::Rect arrowAgain = panelElementFrame(host, "selectElement");
        clickPanel(arrowAgain.x + arrowAgain.width * 0.5, arrowAgain.y + arrowAgain.height * 0.5);
        assert(host.pickingElement());
        core::KeyEvent escape;
        escape.key = core::InputKey::Escape;
        escape.action = core::KeyAction::Press;
        assert(host.handleHotkey(escape));
        assert(!host.pickingElement());
        assert(host.selectedElement() == "page.title");
    }

    // Every tab the panel names is reachable in a docked panel of the default size:
    // the strip is what carries them, so a tab that is clipped out of it cannot be
    // opened. Only Performance and Elements have content; the rest say what they will
    // read once their panel exists, and the plan behind each name is in the module
    // README.
    {
        const struct {
            const char* id;
            DevtoolsTab tab;
        } tabs[] = {
            {"performance.tab", DevtoolsTab::Performance},
            {"elements.tab", DevtoolsTab::Elements},
            {"state.tab", DevtoolsTab::State},
            {"input.tab", DevtoolsTab::Input},
            {"frames.tab", DevtoolsTab::Frames},
            {"layout.tab", DevtoolsTab::Layout},
            {"animations.tab", DevtoolsTab::Animations},
            {"resources.tab", DevtoolsTab::Resources},
            {"windows.tab", DevtoolsTab::Windows},
            {"scale.tab", DevtoolsTab::Scale},
        };
        const core::Rect strip = panelElementFrame(host, "toolbar.tabs");
        for (const auto& entry : tabs) {
            const core::Rect tab = panelElementFrame(host, entry.id);
            assert(tab.x >= strip.x);
            assert(tab.x + tab.width <= strip.x + strip.width);
            assert(tab.width > 0.0f);

            clickPanel(tab.x + tab.width * 0.5, tab.y + tab.height * 0.5);
            assert(host.activeTab() == entry.tab);
            frame();
            // Only the tabs with content show a page of their own.
            if (entry.tab != DevtoolsTab::Performance && entry.tab != DevtoolsTab::Elements) {
                assert(hasPanelElement(host, std::string(entry.id) + ".planned"));
                assert(!hasPanelElement(host, "elements.list"));
            }
        }

        // Back to the performance page for the tests that follow.
        const core::Rect performanceTab = panelElementFrame(host, "performance.tab");
        clickPanel(performanceTab.x + performanceTab.width * 0.5,
                   performanceTab.y + performanceTab.height * 0.5);
        assert(host.activeTab() == DevtoolsTab::Performance);
        frame();
    }

    // A floating panel leaves the whole window to the page and opens its window.
    chooseDock(0);
    assert(host.dockPosition() == DockPosition::Floating);
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).width == static_cast<float>(kWindowWidth));
    assert(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height == static_cast<float>(kWindowHeight));
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

    // A panel in its own window has no edge inside the app's window, but its picker
    // still owns the pointer over the page: the app layer asks the page what is under
    // it, and the click that picks does not reach the app.
    {
        const auto detachedFrame = [&](const std::string& part) {
            core::Rect found;
            for (const core::dsl::runtime::ElementTreeNode& node : detachedRuntime.elementTree().nodes) {
                if (node.id.find(part) != std::string::npos) {
                    found = node.frame;
                    break;
                }
            }
            return found;
        };
        const core::Rect arrow = detachedFrame("selectElement");
        clickDetached(arrow.x + arrow.width * 0.5, arrow.y + arrow.height * 0.5);
        assert(host.pickingElement());

        core::PointerEvent press = pressAt(700.0, 60.0);
        core::ScrollEvent pickScroll;
        const core::PointerEvent routed = routePointer(press, pickScroll);
        assert(routed.x < 0.0 && routed.y < 0.0);
        assert(host.pickedPointer().x == 700.0);
        frame();

        host.setElementUnderPointer("page.ok");
        core::PointerEvent release = press;
        release.action = core::PointerAction::Release;
        release.buttons.set(core::PointerButton::Left, false);
        routePointer(release, pickScroll);
        frame();
        host.setElementUnderPointer("page.ok");
        frame();
        assert(!host.pickingElement());
        assert(host.selectedElement() == "page.ok");
    }

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

    // The panel against a real page. Nothing is injected here, so this is the data path end
    // to end: the panel copies what it shows from the page runtime, and what the user edits
    // lands on the page — with no framework interface and no bridge in between.
    {
        core::dsl::Runtime page;
        page.compose("page", 300.0f, 200.0f, [](core::dsl::Ui& ui, const core::dsl::Screen&) {
            ui.rect("page.panel")
                .size(160.0f, 90.0f)
                .color(core::Color{0.2f, 0.4f, 0.6f, 1.0f})
                .radius(4.0f)
                .build();
        });
        page.update(nullptr, kFrameSeconds, 1.0f, 1.0f);

        host.detach();
        // Forget what the sections above injected: from here on the panel has no data of
        // its own, so everything it shows can only have come from the page.
        host.setElementTree({});
        host.setElementProperties({});
        host.attach(&page, {});

        // A hidden panel copies nothing, and marks nothing on the page.
        frame();
        assert(host.elementTree().nodes.empty());
        assert(!host.properties().active);
        assert(page.hoveredElement().empty());

        // Shown and docked at the bottom, on the Elements tab, one frame copies the page's
        // own tree. The section above left the panel floating, so it is docked again first.
        assert(host.handleHotkey(F12Key(core::KeyAction::Press)));
        frame();
        clickDetached(detachedMoreX, 16.0);
        clickDetached(detachedMenuX, detachedRowY);
        assert(host.dockPosition() == DockPosition::Bottom);
        frame();
        const double pageTabY =
            static_cast<double>(host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height) + 16.0;
        clickPanel(180.0, pageTabY);
        assert(host.activeTab() == DevtoolsTab::Elements);
        frame();
        assert(host.elementTree().nodes.size() == 1);
        assert(host.elementTree().nodes[0].id == "page.panel");
        assert(countPanelRows(host) == 1);

        // Selecting the row reads that element's values from the page.
        const double pageRowY =
            host.contentBounds(kWindowWidth, kWindowHeight, kDpiScale).height + theme.toolbarHeight + 1.0 +
            theme.elementRowHeight * 0.5;
        clickPanel(200.0, pageRowY);
        assert(host.selectedElement() == "page.panel");
        frame();
        assert(host.properties().active);
        assert(host.properties().field(core::dsl::runtime::ElementField::Radius).number == 4.0f);
        assert(host.properties().field(core::dsl::runtime::ElementField::Opacity).number == 1.0f);

        // Hovering the row marks the element on the page, and the page resolves its own
        // geometry for the preview: the panel never computes where an element is. The
        // pointer is moved away first: a motion into the position it already holds is not a
        // hover.
        core::ScrollEvent hoverScroll;
        core::PointerEvent away = pressAt(200.0, pageRowY + theme.elementRowHeight * 4.0);
        away.action = core::PointerAction::Move;
        away.button = core::PointerButton::None;
        away.buttons = {};
        routePointer(away, hoverScroll);
        frame();
        assert(host.hoveredElement().empty());

        core::PointerEvent overRow = pressAt(200.0, pageRowY);
        overRow.action = core::PointerAction::Move;
        overRow.button = core::PointerButton::None;
        overRow.buttons = {};
        routePointer(overRow, hoverScroll);
        frame();
        assert(host.hoveredElement() == "page.panel");
        // The page hears about it on the frame after the panel state changed: the panel's
        // own hover is routed during its update, and the next frame is what tells the page.
        frame();
        assert(page.hoveredElement() == "page.panel");
        assert(page.hoveredBox(kDpiScale).active);

        // A drag on the first number row writes that field on the page, which is the only
        // writer there is: the panel asked, and the page holds the value. The property area
        // is grown first: the row list is virtualized, so a row is only composed while it
        // has room on screen.
        const core::Rect handle = panelElementFrame(host, "elements.properties.handle");
        dragPanel(handle.x + handle.width * 0.5, handle.y + handle.height * 0.5,
                  handle.x + handle.width * 0.5, handle.y + handle.height * 0.5 - 300.0);
        frame();
        const int opacitySlot = panelPropertySlot(host, "Opacity");
        assert(opacitySlot >= 0);
        // The control column of that row: the slider fills it, so a drag across the column
        // is a drag across the slider.
        const core::Rect slider = panelElementFrame(
            host, "elements.properties.list.slot." + std::to_string(opacitySlot) + ".control");
        assert(slider.width > 0.0f && slider.height > 0.0f);
        dragPanel(slider.x + slider.width * 0.9, slider.y + slider.height * 0.5,
                  slider.x + slider.width * 0.15, slider.y + slider.height * 0.5);
        assert(page.elementPatchCount() == 1);
        assert(page.elementValues("page.panel").field(core::dsl::runtime::ElementField::Opacity).number < 1.0f);

        // The panel shows what its own edit did, in the same frame, and offers the way back.
        frame();
        assert(host.properties().field(core::dsl::runtime::ElementField::Opacity).number < 1.0f);
        assert(host.properties().wasWritten(core::dsl::runtime::ElementField::Opacity));
        assert(host.propertyOverrideCount() == 1);

        // Reset drops the patch; the page keeps the written value until it composes again,
        // which is the app's own frame and the only thing that can put it back.
        const core::Rect reset = panelElementFrame(host, "elements.properties.footer.inner.reset");
        clickPanel(reset.x + reset.width * 0.5, reset.y + reset.height * 0.5);
        frame();
        assert(page.elementPatchCount() == 0);
        page.compose("page", 300.0f, 200.0f, [](core::dsl::Ui& ui, const core::dsl::Screen&) {
            ui.rect("page.panel")
                .size(160.0f, 90.0f)
                .color(core::Color{0.2f, 0.4f, 0.6f, 1.0f})
                .radius(4.0f)
                .build();
        });
        page.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
        assert(page.elementValues("page.panel").field(core::dsl::runtime::ElementField::Opacity).number == 1.0f);

        // Detaching lets go of the page: the mark goes with the panel, and the panel stops
        // touching the page it inspected.
        host.detach();
        assert(page.hoveredElement().empty());
        page.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
        assert(page.hoveredElement().empty());
        page.shutdown(false);
        host.attach(nullptr, {});
        // The panel is still shown; with no page behind it, the hotkey still closes it.
        assert(host.visible());
        assert(host.handleHotkey(F12Key(core::KeyAction::Press)));
        assert(!host.visible());
    }

    detachedRuntime.shutdown(false);
    host.shutdown();
    assert(!host.visible());
    return 0;
}

#else

// A build without tooling still exposes the seam's API — a tool is written once and
// compiled against one set of headers — but nothing behind it: no state is allocated,
// nothing can be marked, replaced or picked, and the panel is not part of the build.
static_assert(HasOverlayHooks<core::dsl::Runtime>::value, "Release Runtime must still expose the tooling seam");
static_assert(sizeof(core::dsl::runtime::ToolingState) <= sizeof(void*),
              "A Release build must not carry tool state");
static_assert(!core::dsl::tooling::kToolingEnabled, "This configuration has no tooling");

int main() {
    using core::dsl::runtime::ElementField;

    using core::dsl::runtime::fieldValueOf;

    core::dsl::Runtime runtime;
    assert(runtime.tooling() == nullptr);
    assert(runtime.elementStructureRevision() == 0);
    assert(runtime.hoveredElement().empty());
    assert(runtime.elementTree().nodes.empty());
    assert(!runtime.elementValues("page.root").active);
    assert(runtime.elementIdAt(0.0, 0.0, 1.0f).empty());
    assert(runtime.elementPatchCount() == 0);

    // Every entry point is inert, and none of them allocates the state the seam would
    // keep: a build without tooling pays one pointer and nothing else.
    runtime.setHoveredElement("page.root");
    runtime.setElementField("page.root", ElementField::Radius, fieldValueOf(4.0f));
    runtime.clearElementField("page.root", ElementField::Radius);
    runtime.clearElementFields();
    assert(runtime.hoveredElement().empty());
    assert(runtime.tooling() == nullptr);
    return 0;
}

#endif
