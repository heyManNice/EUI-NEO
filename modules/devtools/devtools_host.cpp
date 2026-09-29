#include "modules/devtools/devtools_host.h"

#if defined(EUI_TOOLING)

#include "modules/devtools/devtools_preview.h"
#include "modules/devtools/devtools_theme.h"
#include "modules/devtools/devtools_tree.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>

namespace modules::devtools {

namespace {

inline double monotonicSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

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

// The panel host lives for the whole process and is deliberately never destroyed: the hooks
// point at it, while the session that owns them is a file scope object in the application and
// is therefore destroyed *after* it. One leaked object at exit is what keeps that ordering
// from writing into freed storage. What belongs to one run — the panel runtime, its state, the
// preview primitive — is released by `shutdown()`, not by this object going away.
DevtoolsHost& devtoolsHost() {
    static DevtoolsHost* host = new DevtoolsHost();
    return *host;
}

// The panel is one slot in the app loop, so it has one owner. A second live session is an
// application mistake: the debug build stops on it, and the session is handed no panel rather
// than left to share the first one's.
int liveSessions = 0;

} // namespace

bool attachDevtoolsHost() {
    if (liveSessions > 0) {
        assert(false && "modules::devtools::Session: one live session per application");
        return false;
    }
    ++liveSessions;
    // The framework's side of the panel: the app loop asks these, and the panel answers.
    // Everything the panel reads and writes it does on the page runtime it is handed in
    // `attach`.
    static app::detail::OverlayHooks hooks = [] {
        app::detail::OverlayHooks value;
        value.attach = [](core::dsl::Runtime& page, const app::detail::OverlayWindows& windows) {
            devtoolsHost().attach(&page, windows);
        };
        // The app is shutting its page down: take the panel off it and shut the panel's own
        // runtime down with it. This is the one teardown that runs while the device is still
        // current, which is why the preview primitive is released for real here.
        value.detach = [] {
            devtoolsHost().detach();
            devtoolsHost().shutdown();
        };
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
    return true;
}

void detachDevtoolsHost() {
    // Leaving undoes both halves of attaching: the app loop stops asking the panel, and the
    // page stops calling it. The second half is the one an application cannot be asked to
    // get right, so it happens here.
    app::detail::setOverlayHooks(nullptr);
    devtoolsHost().detach();
    --liveSessions;
}

void DevtoolsHost::attach(core::dsl::Runtime* page, const app::detail::OverlayWindows& windows) {
    // A new page is attached, and the previous one is let go first, hooks and all: a page the
    // panel no longer inspects must not keep calling it. That page has to be alive — a page is
    // detached before it is destroyed, which is the order the app layer keeps.
    unhookPage();
    session_ = PageSession{};
    session_.page = page;
    session_.windows = windows;
    if (page == nullptr) {
        return;
    }
    // The panel owns what it does to the page: the pointer it takes, the frame it draws,
    // and the preview. All three are runtime hooks, so there is no framework layer in
    // between the panel and the page.
    page->setInputFilter([this](std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent) {
        filterInput(pointerEvents, scrollEvent);
    });
    page->setOverlayRenderer([this](int width, int height, float dpiScale, const core::Rect* dirtyRect) {
        render(width, height, dpiScale, dirtyRect);
    });
    page->setPassRenderer([this](const core::dsl::runtime::RenderPassContext& pass) {
        renderPageOverlay(pass);
    });
    // The page rebuilds every element on every compose, so what the panel wrote is put back
    // on the fresh tree between the compose and its layout.
    page->setAfterCompose([this] { session_.patches.apply(*session_.page); });
}

void DevtoolsHost::detach() {
    // One whole session goes away: the page pointer, the windows, everything the panel
    // wrote on it, everything it copied from it and the pick in progress. The panel's own
    // preferences are not in here and survive to be used on the next page.
    unhookPage();
    session_ = PageSession{};
    forgetPageSelection();
}

// The hooks are what let a page reach the panel, so taking them off is what keeps a page
// nobody inspects any more from calling into it.
void DevtoolsHost::unhookPage() {
    if (core::dsl::Runtime* page = session_.page) {
        page->setInputFilter(nullptr);
        page->setOverlayRenderer(nullptr);
        page->setPassRenderer(nullptr);
        page->setAfterCompose(nullptr);
        page->setHoveredElement(std::string{});
    }
}

// The selection, the hover and the expansion all name elements of the page that just went
// away, so the panel forgets which ones it was looking at; what the user chose for the panel
// itself (dock, size, tab, scroll offsets) is not page state and stays.
//
// The state is reached through the last compose, so a panel that never composed has none to
// forget, and a panel whose state is unreachable keeps a selection the next page does not
// have — which the area that shows it reads as "nothing selected". The clearing is not
// deferred to the next compose: it would then throw away what the user did in between.
void DevtoolsHost::forgetPageSelection() {
    composeRequested_ = true;
    if (panelState_ == nullptr) {
        return;
    }
    panelState_->selectedElement.clear();
    panelState_->hoveredElement.clear();
    panelState_->revealedSelection.clear();
    panelState_->expandedElements.clear();
    panelState_->pickingElement = false;
    panelState_->moreMenuOpen = false;
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
    if (hasPage()) {
        publishElementTree();
        applyElementPropertyEdits();
        publishElementProperties();
        publishPickedElement();
        session_.page->setHoveredElement(hoveredElement());
    }
    return updatePanel(framebufferWidth, framebufferHeight, dpiScale, deltaSeconds);
}

void DevtoolsHost::publishElementTree() {
    if (!hasPage() || !wantsElementTree()) {
        return;
    }
    const std::uint64_t revision = session_.page->elementStructureRevision();
    const double now = core::window::timeSeconds();
    if (revision == session_.treeRevision && now - session_.treeRefreshTime < kRefreshSeconds) {
        return;
    }
    session_.treeRevision = revision;
    session_.treeRefreshTime = now;
    setElementTree(buildElementTree(*session_.page));
}

void DevtoolsHost::publishElementProperties() {
    if (!hasPage()) {
        return;
    }
    const std::string& id = propertiesElement();
    if (id.empty()) {
        return;
    }
    const std::uint64_t revision = session_.page->elementStructureRevision();
    const double now = core::window::timeSeconds();
    const bool sameElement = id == session_.propertiesId;
    const bool throttled = revision == session_.propertiesRevision && now - session_.propertiesRefreshTime < kRefreshSeconds;
    if (sameElement && !session_.propertiesStale && throttled) {
        return;
    }
    session_.propertiesId = id;
    session_.propertiesRevision = revision;
    session_.propertiesRefreshTime = now;
    session_.propertiesStale = false;
    setElementProperties(readElementValues(*session_.page, id, session_.patches.written(id)));
    setElementPropertyOverrideCount(session_.patches.count());
}

// The panel is the only writer, and what it writes is its own business: the values live in
// its own store, go onto the live element in the same frame, and are put back on every
// freshly composed tree by the hook the host registered.
void DevtoolsHost::applyElementPropertyEdits() {
    if (!hasPage()) {
        return;
    }
    bool touched = false;
    ElementPropertyEdit edit;
    while (takeElementPropertyEdit(edit)) {
        if (edit.clear && edit.id.empty()) {
            session_.patches.clearAll();
        } else if (edit.clear) {
            session_.patches.clear(edit.id, edit.field);
        } else {
            session_.patches.set(edit.id, edit.field, edit.value);
        }
        // The value lands on the live element in the same frame; a cleared patch only shows
        // once the page composes again, because the element keeps what it was built with.
        if (core::dsl::Element* element = session_.page->findElement(edit.id)) {
            if (const ElementPatch* patch = session_.patches.find(edit.id)) {
                applyElementPatch(*element, *patch);
            }
        }
        session_.propertiesStale = true;
        touched = true;
    }
    if (touched) {
        // The page was written to outside its compose pass: the next capture reads every
        // element, and the next frame is painted.
        session_.page->requestElementRefresh();
    }
    setElementPropertyOverrideCount(session_.patches.count());
}

// A picking panel owns the pointer: the page is asked what is under it, and the answer
// comes from the page's own hit test, so it is the element the frame drew there.
void DevtoolsHost::publishPickedElement() {
    if (!hasPage()) {
        return;
    }
    if (!pickingElement()) {
        setElementUnderPointer(std::string{});
        return;
    }
    const core::PointerEvent pointer = pickedPointer();
    setElementUnderPointer(session_.page->elementIdAt(pointer.x, pointer.y, dpiScale_));
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
    if (session_.performance.revision == snapshot.revision) {
        return;
    }
    session_.performance = snapshot;
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
    session_.tree = tree;
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
        return session_.elementUnderPointer;
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
    return session_.pickedPointer;
}

void DevtoolsHost::setPickingElement(bool picking) {
    session_.pickCommitPending = false;
    session_.elementUnderPointer.clear();
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
    const bool commit = panelState_->pickingElement && session_.pickCommitPending && !id.empty();
    if (session_.elementUnderPointer == id && !commit) {
        return;
    }
    session_.elementUnderPointer = id;
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

void DevtoolsHost::setElementProperties(const modules::devtools::ElementValues& values) {
    session_.properties = values;
    if (visible_) {
        requestCompose();
    }
}

bool DevtoolsHost::takeElementPropertyEdit(ElementPropertyEdit& edit) {
    if (session_.edits.empty()) {
        return false;
    }
    edit = session_.edits.front();
    session_.edits.pop_front();
    return true;
}

void DevtoolsHost::setElementPropertyOverrideCount(std::size_t count) {
    if (session_.overrideCount == count) {
        return;
    }
    session_.overrideCount = count;
    if (visible_) {
        requestCompose();
    }
}

void DevtoolsHost::queueElementPropertyEdit(const ElementPropertyEdit& edit) {
    session_.edits.push_back(edit);
    // The app layer pulls the edits while it owns the page, so the panel only has to
    // make sure another frame happens.
    requestCompose();
}

const modules::devtools::ElementValues& DevtoolsHost::properties() const {
    return session_.properties;
}

std::size_t DevtoolsHost::propertyOverrideCount() const {
    return session_.overrideCount;
}

const ElementTreeSnapshot& DevtoolsHost::elementTree() const {
    return session_.tree;
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
    if (!session_.windows.open) {
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
    session_.windows.open(request);
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
    if (session_.windows.close) {
        session_.windows.close();
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
    session_.pickedPointer = event;
    if (event.isRelease(core::PointerButton::Left)) {
        session_.pickCommitPending = true;
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
    state.performance = &session_.performance;
    state.elementTree = &session_.tree;
    state.properties = &session_.properties;
    state.propertyOverrideCount = session_.overrideCount;
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

    actions.shell.setTabScrollOffset = [this, &state](float offset) {
        // The panel runtime animates the scroll and reports every step of it, so this is
        // called for frames that move the row by less than a pixel; only a change is a
        // reason to compose the panel again.
        if (state.tabScrollOffset == offset) {
            return;
        }
        state.tabScrollOffset = offset;
        requestCompose();
    };

    actions.performance.setScrollOffset = [this, &state](float offset) {
        state.performanceScrollOffset = offset;
        requestCompose();
    };

    actions.tree.setScrollOffset = [this, &state](float offset) {
        state.elementsScrollOffset = offset;
        requestCompose();
    };
    actions.tree.setTrimIdPrefix = [this, &state](bool value) {
        if (state.trimIdPrefix == value) {
            return;
        }
        state.trimIdPrefix = value;
        requestCompose();
    };
    actions.tree.setShowElementBounds = [this, &state](bool value) {
        if (state.showElementBounds == value) {
            return;
        }
        state.showElementBounds = value;
        requestCompose();
        // The rings are drawn inside the page's own pass, so the page has to draw again for
        // them to appear, and again to take them away.
        if (hasPage()) {
            session_.page->requestFullPaint();
        }
    };
    const auto toggleElementCollapsed = [this, &state](const std::string& id) {
        const auto expanded = std::find(state.expandedElements.begin(), state.expandedElements.end(), id);
        if (expanded == state.expandedElements.end()) {
            state.expandedElements.push_back(id);
        } else {
            state.expandedElements.erase(expanded);
        }
        requestCompose();
    };

    actions.tree.toggleElementCollapsed = [&state, toggleElementCollapsed](const std::string& id) {
        state.lastClickTime = 0.0;
        state.lastClickedElement.clear();
        toggleElementCollapsed(id);
    };
    actions.tree.selectElement = [this, &state, toggleElementCollapsed](const std::string& id) {
        const double now = monotonicSeconds();
        const bool isDoubleClick = (!state.lastClickedElement.empty() &&
                                    state.lastClickedElement == id &&
                                    state.lastClickTime > 0.0 &&
                                    (now - state.lastClickTime) < 0.35);
        state.lastClickedElement = id;
        state.lastClickTime = isDoubleClick ? 0.0 : now;

        if (state.selectedElement != id) {
            state.selectedElement = id;
            state.revealedSelection.clear();
            requestCompose();
        }
        if (isDoubleClick) {
            bool hasChildren = false;
            for (std::size_t i = 0; i < session_.tree.nodes.size(); ++i) {
                if (session_.tree.nodes[i].id == id) {
                    hasChildren = (i + 1 < session_.tree.nodes.size() &&
                                   session_.tree.nodes[i + 1].depth > session_.tree.nodes[i].depth);
                    break;
                }
            }
            if (hasChildren) {
                toggleElementCollapsed(id);
            }
        }
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
    actions.tree.setRevealedSelection = [&state](const std::string& id) {
        // The tree has shown the selection; from now on it is the user's to move.
        if (state.revealedSelection != id) {
            state.revealedSelection = id;
        }
    };

    actions.properties.selectTab = [this, &state](PropertiesTab tab) {
        if (state.activePropertiesTab == tab) {
            return;
        }
        state.activePropertiesTab = tab;
        state.propertiesScrollOffset = 0.0f;
        requestCompose();
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
    actions.properties.toggleColorEditor = [this, &state](modules::devtools::ElementField field, bool open) {
        if (state.colorEditorOpen && state.colorEditorField == field && open) {
            return;
        }
        state.colorEditorOpen = open;
        state.colorEditorField = field;
        requestCompose();
    };
    // One command for every edit: the control builds the value, the host queues it and
    // the app layer writes it, so neither side has to enumerate the fields.
    actions.properties.setValue = [this](const std::string& id, modules::devtools::ElementField field,
                                         const modules::devtools::FieldValue& value) {
        ElementPropertyEdit edit;
        edit.id = id;
        edit.field = field;
        edit.value = value;
        queueElementPropertyEdit(edit);
    };
    actions.properties.clearField = [this](const std::string& id, modules::devtools::ElementField field) {
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
    // The tab row clips what does not fit, so how far it may scroll sideways needs the room
    // it had and the width of the tabs in it. Both are read back from the tree the runtime
    // just laid out, so the toolbar never has to measure itself.
    if (panelState_ != nullptr) {
        if (const core::dsl::Element* strip = runtime_.findElement("eui.devtools.toolbar.tabs")) {
            panelState_->tabStripWidth = strip->frame.width;
        }
        if (const core::dsl::Element* track = runtime_.findElement("eui.devtools.toolbar.tabs.track")) {
            panelState_->tabTrackWidth = track->frame.width;
        }
    }
    if (repainted && hasPage() && dockPosition_ != DockPosition::Floating) {
        // A docked panel draws inside the page's render cache, so a repaint of its own only
        // becomes visible when that cached frame is rebuilt. The detached panel is a window
        // of its own and is composed by the window manager, which needs no such request.
        session_.page->requestFullPaint();
    }
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
    // Two things draw over the page: the box model of the element the panel points at, and,
    // while the view options ask for it, a ring around every element the tree lists. Both
    // belong to the panel, so a panel nobody can see leaves the page alone.
    const bool showBounds = visible_ && panelState_ != nullptr && panelState_->showElementBounds;
    if (!pass.hover.active && !showBounds) {
        return;
    }
    if (!boxPreviewPrimitiveInitialized_) {
        boxPreviewPrimitiveInitialized_ = boxPreviewPrimitive_.initialize();
        if (!boxPreviewPrimitiveInitialized_) {
            return;
        }
    }
    if (showBounds) {
        std::vector<ElementBounds> bounds;
        bounds.reserve(session_.tree.nodes.size());
        for (const ElementTreeNode& node : session_.tree.nodes) {
            bounds.push_back({node.frame, node.depth});
        }
        drawElementBounds(bounds, pass, boxPreviewPrimitive_);
    }
    if (pass.hover.active) {
        if (!boxPreviewTextPrimitiveInitialized_) {
            boxPreviewTextPrimitiveInitialized_ = boxPreviewTextPrimitive_.initialize();
        }
        drawBoxPreview(pass.hover, pass, kHoverBoxPreviewPalette, boxPreviewPrimitive_,
                       boxPreviewTextPrimitiveInitialized_ ? &boxPreviewTextPrimitive_ : nullptr);
    }
}

void DevtoolsHost::releaseGraphicsResources() {
    runtime_.releaseGraphicsResources(false);
    // The preview primitive belongs to the device that just went away.
    if (boxPreviewPrimitiveInitialized_) {
        boxPreviewPrimitive_.destroy();
        boxPreviewPrimitiveInitialized_ = false;
    }
    if (boxPreviewTextPrimitiveInitialized_) {
        boxPreviewTextPrimitive_.destroy();
        boxPreviewTextPrimitiveInitialized_ = false;
    }
    panelState_ = nullptr;
    composeRequested_ = true;
}

void DevtoolsHost::shutdown() {
    // The panel's runtime is shut down without asking it to release device resources: with a
    // device that is still current there is nothing left of them (the app's own
    // releaseGraphics hook ran first), and without a device a call into it would be worse
    // than a buffer that dies with it.
    runtime_.shutdown(false);
    if (boxPreviewPrimitiveInitialized_) {
        if (core::render::activeRenderBackend() != nullptr) {
            boxPreviewPrimitive_.destroy();
        } else {
            // Dropped, not destroyed: the device it belonged to is gone.
            boxPreviewPrimitive_ = core::RoundedRectPrimitive{};
        }
        boxPreviewPrimitiveInitialized_ = false;
    }
    if (boxPreviewTextPrimitiveInitialized_) {
        if (core::render::activeRenderBackend() != nullptr) {
            boxPreviewTextPrimitive_.destroy();
        } else {
            boxPreviewTextPrimitive_ = core::TextPrimitive{};
        }
        boxPreviewTextPrimitiveInitialized_ = false;
    }
    panelState_ = nullptr;
    visible_ = false;
    dockPosition_ = DockPosition::Bottom;
    session_.performance = {};
    resizing_ = false;
    panelEdgeActive_ = false;
    session_.pickCommitPending = false;
    session_.elementUnderPointer.clear();
    panelHeightLogical_ = 0.0f;
    panelWidthLogical_ = 0.0f;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace modules::devtools

#endif
