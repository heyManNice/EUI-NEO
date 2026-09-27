#include "modules/devtools/devtools_host.h"

#if defined(EUI_DEBUG_BUILD)

#include "modules/devtools/devtools_preview.h"
#include "modules/devtools/devtools_theme.h"
#include "modules/devtools/devtools_tree.h"

#include <algorithm>
#include <cmath>

namespace modules::devtools {

namespace {

// Pointer motion that belongs to the panel is moved far outside the window for
// the page, which is the same state the page would see when the pointer leaves
// the window: hover clears, an active press capture stays.
constexpr double kOutsidePointer = -1000000.0;
constexpr float kResizeBoundaryHalfWidth = 4.0f;

bool isHotkey(const core::KeyEvent& key) {
    return key.action == core::KeyAction::Press &&
           (key.key == core::InputKey::F12 ||
            (key.key == core::InputKey::I && key.modifiers.control && key.modifiers.shift));
}

} // namespace

namespace {

// The panel host lives as long as the module does, so the hooks can point at it without
// capturing anything that could go away.
DevtoolsHost& devtoolsHost() {
    static DevtoolsHost host;
    return host;
}

} // namespace

void attachDevtoolsHost() {
    // The framework's side of the panel: the app loop asks these, and the panel answers.
    // Everything the panel reads and writes it does on the page runtime it is handed in
    // `attach`, so nothing here is stored in the framework.
    static app::detail::OverlayHooks hooks = [] {
        app::detail::OverlayHooks value;
        value.attach = [](core::dsl::Runtime& page, const app::detail::OverlayWindows& windows) {
            devtoolsHost().attach(&page, windows);
        };
        value.detach = [] { devtoolsHost().detach(); };
        value.contentBounds = [](int windowWidth, int windowHeight, float dpiScale) {
            return devtoolsHost().contentBounds(windowWidth, windowHeight, dpiScale);
        };
        value.handleKey = [](const core::KeyEvent& key) { return devtoolsHost().handleHotkey(key); };
        value.update = [](int windowWidth, int windowHeight, float dpiScale, float deltaSeconds) {
            return devtoolsHost().frame(windowWidth, windowHeight, dpiScale, deltaSeconds);
        };
        value.render = [](int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect) {
            devtoolsHost().render(windowWidth, windowHeight, dpiScale, dirtyRect);
        };
        value.performance = [](const app::PerformanceSnapshot& snapshot) {
            devtoolsHost().setPerformanceSnapshot(snapshot);
        };
        value.releaseGraphics = [] { devtoolsHost().releaseGraphicsResources(); };
        return value;
    }();
    app::detail::setOverlayHooks(&hooks);
}

void detachDevtoolsHost() {
    app::detail::setOverlayHooks(nullptr);
}

void DevtoolsHost::attach(core::dsl::Runtime* page, const app::detail::OverlayWindows& windows) {
    page_ = page;
    windows_ = windows;
    if (page_ == nullptr) {
        return;
    }
    // The panel owns what it does to the page: the pointer it takes, the frame it draws,
    // and the preview. All three are runtime hooks, so there is no framework layer in
    // between the panel and the page.
    page_->setInputFilter([this](std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent) {
        filterInput(pointerEvents, scrollEvent);
    });
    page_->setOverlayRenderer([this](int width, int height, float dpiScale, const core::Rect* dirtyRect) {
        render(width, height, dpiScale, dirtyRect);
    });
    page_->setPassRenderer([this](const core::dsl::runtime::RenderPassContext& pass) {
        renderPageOverlay(pass);
    });
}

void DevtoolsHost::detach() {
    if (page_ != nullptr) {
        page_->setInputFilter(nullptr);
        page_->setOverlayRenderer(nullptr);
        page_->setPassRenderer(nullptr);
        page_->setHoveredElement(std::string{});
    }
    page_ = nullptr;
    windows_ = {};
    // What the panel knew about the page is stale the moment the page goes away.
    propertiesStale_ = true;
    treeRevision_ = 0;
    propertiesId_.clear();
}

bool DevtoolsHost::frame(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds) {
    // One frame of the panel, in the order the two sides depend on:
    //
    //   1. the tree of this frame, so the panel composes against what the user sees
    //   2. the edits the panel made, read back before the values below, so an edit shows
    //      its result in the same frame instead of the next one
    //   3. the values of the element it shows
    //   4. the element a picker points at, before the preview, so the page marks what the
    //      pointer is on in the frame it moved
    //   5. the preview it asked for, and then the panel itself
    if (page_ != nullptr) {
        publishElementTree();
        applyElementPropertyEdits();
        publishElementProperties();
        publishPickedElement();
        page_->setHoveredElement(hoveredElement());
    }
    return updatePanel(framebufferWidth, framebufferHeight, dpiScale, deltaSeconds);
}

void DevtoolsHost::publishElementTree() {
    if (page_ == nullptr || !wantsElementTree()) {
        return;
    }
    const std::uint64_t revision = page_->elementStructureRevision();
    const double now = core::window::timeSeconds();
    if (revision == treeRevision_ && now - treeRefreshTime_ < kRefreshSeconds) {
        return;
    }
    treeRevision_ = revision;
    treeRefreshTime_ = now;
    setElementTree(buildElementTree(*page_));
}

void DevtoolsHost::publishElementProperties() {
    if (page_ == nullptr) {
        return;
    }
    const std::string& id = propertiesElement();
    if (id.empty()) {
        return;
    }
    const std::uint64_t revision = page_->elementStructureRevision();
    const double now = core::window::timeSeconds();
    const bool sameElement = id == propertiesId_;
    const bool throttled = revision == propertiesRevision_ && now - propertiesRefreshTime_ < kRefreshSeconds;
    if (sameElement && !propertiesStale_ && throttled) {
        return;
    }
    propertiesId_ = id;
    propertiesRevision_ = revision;
    propertiesRefreshTime_ = now;
    propertiesStale_ = false;
    setElementProperties(page_->elementValues(id));
    setElementPropertyOverrideCount(page_->elementPatchCount());
}

// The page is the only writer's target: the panel asks for a change, the host puts it on
// the page, and the next read shows the result. Nothing is written back into app state.
void DevtoolsHost::applyElementPropertyEdits() {
    if (page_ == nullptr) {
        return;
    }
    ElementPropertyEdit edit;
    while (takeElementPropertyEdit(edit)) {
        if (edit.clear && edit.id.empty()) {
            page_->clearElementFields();
        } else if (edit.clear) {
            page_->clearElementField(edit.id, edit.field);
        } else {
            page_->setElementField(edit.id, edit.field, edit.value);
        }
        propertiesStale_ = true;
    }
    setElementPropertyOverrideCount(page_->elementPatchCount());
}

// A picking panel owns the pointer: the page is asked what is under it, and the answer
// comes from the page's own hit test, so it is the element the frame drew there.
void DevtoolsHost::publishPickedElement() {
    if (page_ == nullptr) {
        return;
    }
    if (!pickingElement()) {
        setElementUnderPointer(std::string{});
        return;
    }
    const core::PointerEvent pointer = pickedPointer();
    setElementUnderPointer(page_->elementIdAt(pointer.x, pointer.y, dpiScale_));
}

bool DevtoolsHost::handleHotkey(const core::KeyEvent& key) {
    if (key.action == core::KeyAction::Press && key.key == core::InputKey::Escape &&
        panelState_ != nullptr && panelState_->pickingElement) {
        // Escape leaves the picker: the panel keeps the pointer to itself only while
        // the user is picking an element.
        setPickingElement(false);
        return true;
    }
    if (!isHotkey(key)) {
        return false;
    }
    if (visible_) {
        close();
    } else {
        visible_ = true;
        requestCompose();
        if (dockPosition_ == DockPosition::Floating) {
            openDetachedWindow();
        }
    }
    return true;
}

void DevtoolsHost::setPerformanceSnapshot(const app::PerformanceSnapshot& snapshot) {
    if (performanceSnapshot_.revision == snapshot.revision) {
        return;
    }
    performanceSnapshot_ = snapshot;
    if (visible_) {
        requestCompose();
    }
}

bool DevtoolsHost::wantsElementTree() const {
    // The tree is only copied while the panel is visible and shows the tab that
    // displays it.
    return visible_ && panelState_ != nullptr && panelState_->activeTab == DevtoolsTab::Elements;
}

void DevtoolsHost::setElementTree(const ElementTreeSnapshot& tree) {
    elementTree_ = tree;
    if (visible_) {
        requestCompose();
    }
}

const std::string& DevtoolsHost::hoveredElement() const {
    static const std::string empty;
    if (!visible_ || panelState_ == nullptr) {
        return empty;
    }
    // While the panel picks, the preview follows the pointer on the page; otherwise it
    // is the tree row under the mouse.
    if (panelState_->pickingElement) {
        return elementUnderPointer_;
    }
    if (panelState_->activeTab != DevtoolsTab::Elements) {
        return empty;
    }
    return panelState_->hoveredElement;
}

bool DevtoolsHost::pickingElement() const {
    return visible_ && panelState_ != nullptr && panelState_->pickingElement;
}

core::PointerEvent DevtoolsHost::pickedPointer() const {
    return pickedPointer_;
}

void DevtoolsHost::setPickingElement(bool picking) {
    pickCommitPending_ = false;
    elementUnderPointer_.clear();
    if (panelState_ == nullptr || panelState_->pickingElement == picking) {
        return;
    }
    panelState_->pickingElement = picking;
    if (picking && panelState_->activeTab != DevtoolsTab::Elements) {
        // Picking is about the tree, so the panel shows the tab that will follow it.
        panelState_->activeTab = DevtoolsTab::Elements;
    }
    panelState_->moreMenuOpen = false;
    requestCompose();
}

void DevtoolsHost::setElementUnderPointer(const std::string& id) {
    if (panelState_ == nullptr) {
        return;
    }
    // A pick is committed by the answer that follows the click, so the selection is
    // the element the user saw under the pointer when they pressed.
    const bool commit = panelState_->pickingElement && pickCommitPending_ && !id.empty();
    if (elementUnderPointer_ == id && !commit) {
        return;
    }
    elementUnderPointer_ = id;
    if (!commit) {
        return;
    }
    panelState_->selectedElement = id;
    panelState_->revealedSelection.clear();
    // The picker turns itself off once it has picked, like the one in a browser.
    setPickingElement(false);
}

const std::string& DevtoolsHost::propertiesElement() const {
    // The property area shows the element the user selected, and only while the tab
    // that shows it is the one on screen: a hidden panel asks for nothing.
    static const std::string empty;
    if (!visible_ || panelState_ == nullptr || panelState_->activeTab != DevtoolsTab::Elements) {
        return empty;
    }
    return panelState_->selectedElement;
}

void DevtoolsHost::setElementProperties(const core::dsl::runtime::ElementValues& values) {
    properties_ = values;
    if (visible_) {
        requestCompose();
    }
}

bool DevtoolsHost::takeElementPropertyEdit(ElementPropertyEdit& edit) {
    if (propertyEdits_.empty()) {
        return false;
    }
    edit = propertyEdits_.front();
    propertyEdits_.pop_front();
    return true;
}

void DevtoolsHost::setElementPropertyOverrideCount(std::size_t count) {
    if (propertyOverrideCount_ == count) {
        return;
    }
    propertyOverrideCount_ = count;
    if (visible_) {
        requestCompose();
    }
}

void DevtoolsHost::queueElementPropertyEdit(const ElementPropertyEdit& edit) {
    propertyEdits_.push_back(edit);
    // The app layer pulls the edits while it owns the page, so the panel only has to
    // make sure another frame happens.
    requestCompose();
}

const core::dsl::runtime::ElementValues& DevtoolsHost::properties() const {
    return properties_;
}

std::size_t DevtoolsHost::propertyOverrideCount() const {
    return propertyOverrideCount_;
}

const ElementTreeSnapshot& DevtoolsHost::elementTree() const {
    return elementTree_;
}

ElementTreeSnapshot DevtoolsHost::panelElementTree() const {
    return buildElementTree(runtime_);
}

bool DevtoolsHost::visible() const {
    return visible_;
}

DockPosition DevtoolsHost::dockPosition() const {
    return dockPosition_;
}

DevtoolsTab DevtoolsHost::activeTab() const {
    return panelState_ != nullptr ? panelState_->activeTab : DevtoolsTab::Performance;
}

void DevtoolsHost::requestCompose() {
    composeRequested_ = true;
    if (dockPosition_ == DockPosition::Floating) {
        // A detached panel lives in a window the window manager composes, and that
        // only happens for a frame the app layer reports as an update. A frame
        // request alone would leave the detached window on its previous frame.
        core::platform::requestUiUpdate();
        return;
    }
    // The docked panel only needs another frame. Requesting a UI update instead
    // would be read by the app layer as "app state changed", which recomposes the
    // host page and repaints the whole window behind the panel.
    core::platform::requestFrame();
}

void DevtoolsHost::selectTab(DevtoolsTab tab) {
    if (panelState_ == nullptr) {
        return;
    }
    panelState_->moreMenuOpen = false;
    if (tab != DevtoolsTab::Elements) {
        // Picking is about the tree, so leaving that tab leaves the picker as well.
        setPickingElement(false);
    }
    if (panelState_->activeTab == tab) {
        return;
    }
    panelState_->activeTab = tab;
    requestCompose();
}

void DevtoolsHost::dismissMoreMenu() {
    if (panelState_ == nullptr || !panelState_->moreMenuOpen) {
        return;
    }
    panelState_->moreMenuOpen = false;
    requestCompose();
}

void DevtoolsHost::close() {
    visible_ = false;
    resizing_ = false;
    panelEdgeActive_ = false;
    setPickingElement(false);
    if (panelState_ != nullptr) {
        panelState_->moreMenuOpen = false;
    }
    if (dockPosition_ == DockPosition::Floating) {
        closeDetachedWindow();
    }
    requestCompose();
}

void DevtoolsHost::selectDockPosition(DockPosition position) {
    if (panelState_ != nullptr) {
        panelState_->moreMenuOpen = false;
    }
    if (position == dockPosition_) {
        requestCompose();
        return;
    }
    if (dockPosition_ == DockPosition::Floating) {
        closeDetachedWindow();
    }
    dockPosition_ = position;
    resizing_ = false;
    panelEdgeActive_ = false;
    if (position == DockPosition::Floating) {
        openDetachedWindow();
    }
    requestCompose();
}

void DevtoolsHost::openDetachedWindow() {
    if (!windows_.open) {
        return;
    }
    app::detail::OverlayWindowRequest request;
    request.title = "EUI DevTools";
    request.clearColor = devtoolsTheme().panelBackground;
    request.width = 640;
    request.height = 420;
    request.compose = [this](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        composeDetached(ui, screen);
    };
    request.closed = [this] { detachedWindowClosed(); };
    windows_.open(request);
}

void DevtoolsHost::detachedWindowClosed() {
    // The detached window and the panel Runtime it composed are gone, so the host
    // must forget the state that lived inside them before anything reads it.
    panelState_ = nullptr;
    if (dockPosition_ == DockPosition::Floating) {
        close();
    }
}

void DevtoolsHost::closeDetachedWindow() {
    if (windows_.close) {
        windows_.close();
    }
}

int DevtoolsHost::panelSize() const {
    if (!visible_ || dockPosition_ == DockPosition::Floating ||
        framebufferWidth_ <= 0 || framebufferHeight_ <= 0) {
        return 0;
    }
    const bool horizontal = dockPosition_ != DockPosition::Bottom;
    const int available = horizontal ? framebufferWidth_ : framebufferHeight_;
    const int maximum = maximumPanelSize();
    const int minimum = minimumPanelSize();
    const int preferred = std::clamp(static_cast<int>(std::lround(available * (horizontal ? 0.35f : 0.42f))),
                                     minimum, std::min(maximum, static_cast<int>(std::lround(
                                         (horizontal ? 460.0f : 320.0f) * dpiScale_))));
    const float savedLogical = horizontal ? panelWidthLogical_ : panelHeightLogical_;
    const int requested = savedLogical > 0.0f
        ? static_cast<int>(std::lround(savedLogical * dpiScale_)) : preferred;
    return std::clamp(requested, minimum, maximum);
}

int DevtoolsHost::minimumPanelSize() const {
    const float logical = dockPosition_ == DockPosition::Bottom ? 180.0f : 300.0f;
    return std::min(maximumPanelSize(), static_cast<int>(std::lround(logical * dpiScale_)));
}

int DevtoolsHost::maximumPanelSize() const {
    const int minimumContent = std::max(80, static_cast<int>(std::lround(120.0f * dpiScale_)));
    const int available = dockPosition_ == DockPosition::Bottom ? framebufferHeight_ : framebufferWidth_;
    return std::max(0, available - minimumContent);
}

core::Rect DevtoolsHost::panelBounds() const {
    const float size = static_cast<float>(panelSize());
    if (dockPosition_ == DockPosition::Left) {
        return {0.0f, 0.0f, size, static_cast<float>(framebufferHeight_)};
    }
    if (dockPosition_ == DockPosition::Right) {
        return {static_cast<float>(framebufferWidth_) - size, 0.0f, size,
                static_cast<float>(framebufferHeight_)};
    }
    if (dockPosition_ == DockPosition::Bottom) {
        return {0.0f, static_cast<float>(framebufferHeight_) - size,
                static_cast<float>(framebufferWidth_), size};
    }
    return {};
}

core::Rect DevtoolsHost::contentBounds(int windowWidth, int windowHeight, float dpiScale) const {
    (void)dpiScale;
    const float width = static_cast<float>(windowWidth);
    const float height = static_cast<float>(windowHeight);
    if (!visible_ || dockPosition_ == DockPosition::Floating) {
        return {0.0f, 0.0f, width, height};
    }
    // The panel is measured in the window that was last driven, so a differently sized
    // window has to be driven once before its content bounds mean anything.
    if (windowWidth != framebufferWidth_ || windowHeight != framebufferHeight_) {
        return {0.0f, 0.0f, width, height};
    }
    const core::Rect panel = panelBounds();
    if (dockPosition_ == DockPosition::Left) {
        return {panel.width, 0.0f, width - panel.width, height};
    }
    if (dockPosition_ == DockPosition::Right) {
        return {0.0f, 0.0f, width - panel.width, height};
    }
    return {0.0f, 0.0f, width, height - panel.height};
}

int DevtoolsHost::contentHeight() const {
    return static_cast<int>(contentBounds(framebufferWidth_, framebufferHeight_, dpiScale_).height);
}

float DevtoolsHost::performanceScrollOffset() const {
    return panelState_ != nullptr ? panelState_->performanceScrollOffset : 0.0f;
}

const std::string& DevtoolsHost::selectedElement() const {
    static const std::string empty;
    return panelState_ != nullptr ? panelState_->selectedElement : empty;
}

const std::vector<std::string>& DevtoolsHost::expandedElements() const {
    static const std::vector<std::string> empty;
    return panelState_ != nullptr ? panelState_->expandedElements : empty;
}

bool DevtoolsHost::overResizeBoundary(double x, double y) const {
    const core::Rect panel = panelBounds();
    if (dockPosition_ == DockPosition::Bottom) {
        return x >= 0.0 && x < framebufferWidth_ &&
               std::abs(y - panel.y) <= kResizeBoundaryHalfWidth * dpiScale_;
    }
    const float boundary = dockPosition_ == DockPosition::Left ? panel.width : panel.x;
    return y >= 0.0 && y < framebufferHeight_ &&
           std::abs(x - boundary) <= kResizeBoundaryHalfWidth * dpiScale_;
}

void DevtoolsHost::capturePickPointer(core::PointerEvent& event) {
    // The picker owns the pointer: it remembers where to ask, and the page never sees
    // the click that picks, so picking an element does not also press it.
    pickedPointer_ = event;
    if (event.isRelease(core::PointerButton::Left)) {
        pickCommitPending_ = true;
    }
    event.x = kOutsidePointer;
    event.y = kOutsidePointer;
    event.deltaX = 0.0;
    event.deltaY = 0.0;
}

void DevtoolsHost::filterInput(std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent) {
    if (!visible_) {
        return;
    }
    const bool picking = pickingElement();
    if (panelSize() <= 0) {
        // A panel in a window of its own has no edge inside this one, but its picker
        // still owns the pointer over the page: that is what the app layer hit tests.
        if (!picking) {
            return;
        }
        for (core::PointerEvent& event : pointerEvents) {
            capturePickPointer(event);
        }
        return;
    }
    bool pointerInPanel = false;
    for (core::PointerEvent& event : pointerEvents) {
        const bool overBoundary = overResizeBoundary(event.x, event.y);
        const bool captured = resizing_;
        if (event.isPress(core::PointerButton::Left) && overBoundary) {
            resizing_ = true;
            dragStartX_ = event.x;
            dragStartY_ = event.y;
            dragStartSize_ = panelSize();
        }
        if (resizing_ && (event.action == core::PointerAction::Move || event.isRelease(core::PointerButton::Left))) {
            const double delta = dockPosition_ == DockPosition::Bottom ? dragStartY_ - event.y
                : dockPosition_ == DockPosition::Left ? event.x - dragStartX_ : dragStartX_ - event.x;
            const int size = std::clamp(static_cast<int>(std::lround(dragStartSize_ + delta)),
                                        minimumPanelSize(), maximumPanelSize());
            if (size != panelSize()) {
                (dockPosition_ == DockPosition::Bottom ? panelHeightLogical_ : panelWidthLogical_) =
                    static_cast<float>(size) / dpiScale_;
                requestCompose();
            }
        }
        if (event.isRelease(core::PointerButton::Left) || event.action == core::PointerAction::Cancel ||
            (resizing_ && event.action == core::PointerAction::Move && !event.isDown(core::PointerButton::Left))) {
            resizing_ = false;
        }
        panelEdgeActive_ = resizing_ || overResizeBoundary(event.x, event.y);

        const core::Rect panel = panelBounds();
        const bool inside = event.x >= panel.x && event.x < panel.x + panel.width &&
                            event.y >= panel.y && event.y < panel.y + panel.height;
        if (panelState_ != nullptr && panelState_->moreMenuOpen &&
            event.action == core::PointerAction::Press && !inside) {
            dismissMoreMenu();
        }

        // The panel reads its own copy of the event; the page only keeps the
        // part of the stream that does not belong to the panel.
        core::PointerEvent panelEvent = event;
        if (!(inside && !panelEdgeActive_)) {
            panelEvent.x = kOutsidePointer;
            panelEvent.y = kOutsidePointer;
            panelEvent.deltaX = 0.0;
            panelEvent.deltaY = 0.0;
        }
        runtime_.pushPointerEvent(panelEvent);

        if (!inside && !overBoundary && !captured) {
            if (picking) {
                capturePickPointer(event);
            }
            pointerInPanel = false;
            continue;
        }
        pointerInPanel = true;
        event.x = kOutsidePointer;
        event.y = kOutsidePointer;
        event.deltaX = 0.0;
        event.deltaY = 0.0;
    }
    if (pointerInPanel) {
        // The pointer is on the panel: it owns the wheel until it leaves.
        runtime_.pushScrollEvent(scrollEvent);
        scrollEvent = {};
    }
}

void DevtoolsHost::composeUi(core::dsl::Ui& ui, float width, float height, const core::Rect& panel, bool detached) {
    // Panel state lives in the panel Runtime, so it is torn down with it and the
    // host never keeps a second copy of the same truth. Only one of the two panel
    // runtimes composes at a time, so the host tracks whichever one is active.
    DevtoolsPanelState& panelState = ui.state<DevtoolsPanelState>("devtools.panel");
    panelState_ = &panelState;

    DevtoolsUiState state;
    state.width = width;
    state.height = height;
    state.panel = panel;
    state.detached = detached;
    state.dockPosition = dockPosition_;
    state.panelState = &panelState;
    state.performance = &performanceSnapshot_;
    state.elementTree = &elementTree_;
    state.properties = &properties_;
    state.propertyOverrideCount = propertyOverrideCount_;
    composeDevtoolsUi(ui, state, buildActions(panelState));
}

// Every command the panel can raise, wired by name. The panel writes panel state or
// queues an edit for the app layer; none of these handlers touch the page, which is
// what keeps the panel's own truth in one place.
DevtoolsUiActions DevtoolsHost::buildActions(DevtoolsPanelState& state) {
    DevtoolsUiActions actions;

    actions.shell.selectTab = [this](DevtoolsTab tab) { selectTab(tab); };
    actions.shell.selectDockPosition = [this](DockPosition position) { selectDockPosition(position); };
    actions.shell.toggleMoreMenu = [this, &state] {
        state.moreMenuOpen = !state.moreMenuOpen;
        requestCompose();
    };
    actions.shell.dismissMoreMenu = [this, &state] {
        if (!state.moreMenuOpen) {
            return;
        }
        state.moreMenuOpen = false;
        requestCompose();
    };
    actions.shell.close = [this] { close(); };

    actions.performance.setScrollOffset = [this, &state](float offset) {
        state.performanceScrollOffset = offset;
        requestCompose();
    };

    actions.tree.setScrollOffset = [this, &state](float offset) {
        state.elementsScrollOffset = offset;
        requestCompose();
    };
    actions.tree.selectElement = [this, &state](const std::string& id) {
        if (state.selectedElement == id) {
            return;
        }
        state.selectedElement = id;
        state.revealedSelection.clear();
        requestCompose();
    };
    actions.tree.hoverElement = [&state](const std::string& id, bool hovered) {
        // Leaving a row only clears the preview if that row still owns it, so
        // moving between rows does not depend on callback order.
        if (hovered) {
            state.hoveredElement = id;
            return;
        }
        if (state.hoveredElement == id) {
            state.hoveredElement.clear();
        }
    };
    actions.tree.toggleElementPicker = [this] { setPickingElement(!pickingElement()); };
    actions.tree.toggleElementCollapsed = [this, &state](const std::string& id) {
        const auto expanded = std::find(state.expandedElements.begin(), state.expandedElements.end(), id);
        if (expanded == state.expandedElements.end()) {
            state.expandedElements.push_back(id);
        } else {
            state.expandedElements.erase(expanded);
        }
        requestCompose();
    };
    actions.tree.setRevealedSelection = [&state](const std::string& id) {
        // The tree has shown the selection; from now on it is the user's to move.
        if (state.revealedSelection != id) {
            state.revealedSelection = id;
        }
    };

    actions.properties.copyElementId = [](const std::string& id) {
        core::window::setClipboardText(id);
    };
    actions.properties.setScrollOffset = [this, &state](float offset) {
        state.propertiesScrollOffset = offset;
        requestCompose();
    };
    actions.properties.setHeight = [this, &state](float height) {
        if (state.propertiesHeight == height) {
            return;
        }
        state.propertiesHeight = height;
        requestCompose();
    };
    actions.properties.beginResize = [this, &state](float startHeight, float pixelScale) {
        state.propertiesResizeStartHeight = startHeight;
        state.propertiesResizeScale = pixelScale > 0.0f ? pixelScale : 1.0f;
        // The divider measures against what the press recorded, so the panel has
        // to compose again before the drag moves: its callbacks read the state
        // through the composition, not through the runtime that called them.
        requestCompose();
    };
    actions.properties.endResize = [&state] {
        // A finished drag leaves no origin behind: the next press measures from
        // the height the panel is at then, not from an earlier drag's start.
        state.propertiesResizeStartHeight = 0.0f;
        state.propertiesResizeScale = 1.0f;
    };
    actions.properties.toggleColorEditor = [this, &state](core::dsl::runtime::ElementField field, bool open) {
        if (state.colorEditorOpen && state.colorEditorField == field && open) {
            return;
        }
        state.colorEditorOpen = open;
        state.colorEditorField = field;
        requestCompose();
    };
    // One command for every edit: the control builds the value, the host queues it and
    // the app layer writes it, so neither side has to enumerate the fields.
    actions.properties.setValue = [this](const std::string& id, core::dsl::runtime::ElementField field,
                                         const core::dsl::runtime::FieldValue& value) {
        ElementPropertyEdit edit;
        edit.id = id;
        edit.field = field;
        edit.value = value;
        queueElementPropertyEdit(edit);
    };
    actions.properties.clearField = [this](const std::string& id, core::dsl::runtime::ElementField field) {
        ElementPropertyEdit edit;
        edit.id = id;
        edit.field = field;
        edit.clear = true;
        queueElementPropertyEdit(edit);
        // Putting a value back means the element has to be built from the app's
        // code again: the patch was written onto the composed element.
        core::platform::requestUiUpdate();
    };
    actions.properties.clearFields = [this] {
        // An empty id with `clear` puts every element on the page back, which is
        // what the property footer offers once a debug session changed something.
        ElementPropertyEdit edit;
        edit.clear = true;
        queueElementPropertyEdit(edit);
        core::platform::requestUiUpdate();
    };

    return actions;
}

void DevtoolsHost::composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    composeUi(ui, screen.width, screen.height, {0.0f, 0.0f, screen.width, screen.height}, true);
}

bool DevtoolsHost::updatePanel(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds) {
    if (framebufferWidth != framebufferWidth_ || framebufferHeight != framebufferHeight_ ||
        dpiScale != dpiScale_) {
        requestCompose();
    }
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;
    dpiScale_ = dpiScale;

    if (!visible_ || dockPosition_ == DockPosition::Floating || panelSize() <= 0 || dpiScale_ <= 0.0f) {
        return false;
    }

    const auto composePanel = [&] {
        const float width = static_cast<float>(framebufferWidth_) / dpiScale_;
        const float height = static_cast<float>(framebufferHeight_) / dpiScale_;
        const core::Rect pixelPanel = panelBounds();
        const core::Rect logicalPanel{pixelPanel.x / dpiScale_, pixelPanel.y / dpiScale_,
                                      pixelPanel.width / dpiScale_, pixelPanel.height / dpiScale_};
        runtime_.compose("eui.devtools", width, height, [&](core::dsl::Ui& ui, const core::dsl::Screen&) {
            composeUi(ui, width, height, logicalPanel, false);
        });
        composeRequested_ = false;
    };
    // At most one compose per frame: the panel tree is composed before the update,
    // never after it. Composing and updating twice in the same frame leaves the
    // primitive bounds of the panel unusable for that frame's draw, so the renderer
    // culls the whole panel and the panel area shows the cleared render cache for
    // one frame (a black flash on tab switches and menu opens). An interaction that
    // changes panel state therefore lands one frame later, which is imperceptible.
    const bool composed = composeRequested_;
    if (composeRequested_) {
        composePanel();
    }
    // The panel runtime is driven with the frame the page was updated with. Scroll
    // impulses and transitions only advance while the frame clock does, so a
    // zero delta would freeze every animation the panel owns while the pointer
    // is over it in docked mode (the detached window is a normal window and gets
    // a real delta from the frame loop).
    const bool repainted = runtime_.update(nullptr, deltaSeconds, 1.0f, dpiScale_) || composed;
    if (composeRequested_ || runtime_.isAnimating()) {
        // Panel state changes and panel animations both need the next frame; the
        // page runtime knows nothing about either of them.
        core::platform::requestFrame();
    }
    return repainted;
}

void DevtoolsHost::render(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect) {
    if (!visible_ || dockPosition_ == DockPosition::Floating) {
        return;
    }
    // The panel owns its rectangle only. Clipping to it keeps panel primitives
    // out of the page area of the render cache both are drawn into.
    const core::Rect panel = panelBounds();
    if (panel.width <= 0.0f || panel.height <= 0.0f) {
        return;
    }
    if (dirtyRect != nullptr) {
        core::Rect overlap;
        if (!core::dsl::intersectRect(panel, *dirtyRect, overlap)) {
            return;
        }
    }
    runtime_.renderDirectOverlay(windowWidth, windowHeight, dpiScale, &panel);
}

void DevtoolsHost::renderPageOverlay(const core::dsl::runtime::RenderPassContext& pass) {
    // The hovered element is what asks for a preview: the panel reports one only while it
    // shows the tree that hovers over it, so an inactive box means nothing to draw.
    if (!pass.hover.active) {
        return;
    }
    if (!boxPreviewPrimitiveInitialized_) {
        boxPreviewPrimitiveInitialized_ = boxPreviewPrimitive_.initialize();
        if (!boxPreviewPrimitiveInitialized_) {
            return;
        }
    }
    drawBoxPreview(pass.hover, pass, kHoverBoxPreviewPalette, boxPreviewPrimitive_);
}

void DevtoolsHost::releaseGraphicsResources() {
    runtime_.releaseGraphicsResources(false);
    // The preview primitive belongs to the device that just went away.
    if (boxPreviewPrimitiveInitialized_) {
        boxPreviewPrimitive_.destroy();
        boxPreviewPrimitiveInitialized_ = false;
    }
    panelState_ = nullptr;
    composeRequested_ = true;
}

void DevtoolsHost::shutdown() {
    runtime_.shutdown(false);
    if (boxPreviewPrimitiveInitialized_) {
        boxPreviewPrimitive_.destroy();
        boxPreviewPrimitiveInitialized_ = false;
    }
    panelState_ = nullptr;
    visible_ = false;
    dockPosition_ = DockPosition::Bottom;
    performanceSnapshot_ = {};
    resizing_ = false;
    panelEdgeActive_ = false;
    pickCommitPending_ = false;
    elementUnderPointer_.clear();
    panelHeightLogical_ = 0.0f;
    panelWidthLogical_ = 0.0f;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace modules::devtools

#endif
