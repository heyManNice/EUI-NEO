#include "modules/devtools/devtools_host.h"

#if defined(EUI_DEBUG_BUILD)

#include "modules/devtools/devtools_theme.h"

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

void attachDevtoolsHost() {
    static DevtoolsHost host;
    app::detail::setOverlayHost(&host);
}

void detachDevtoolsHost() {
    app::detail::setOverlayHost(nullptr);
}

bool DevtoolsHost::handleHotkey(const core::KeyEvent& key) {
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

void DevtoolsHost::setElementTree(const core::dsl::runtime::ElementTreeSnapshot& tree) {
    elementTree_ = tree;
    if (visible_) {
        requestCompose();
    }
}

const std::string& DevtoolsHost::inspectedElement() const {
    // The mark belongs to the tab that shows the tree: hiding the panel or leaving
    // the tab drops it, so a page is never marked while nobody looks at it.
    static const std::string empty;
    if (!visible_ || panelState_ == nullptr || panelState_->activeTab != DevtoolsTab::Elements) {
        return empty;
    }
    return panelState_->selectedElement;
}

const core::dsl::runtime::ElementTreeSnapshot& DevtoolsHost::elementTree() const {
    return elementTree_;
}

core::dsl::runtime::ElementTreeSnapshot DevtoolsHost::panelElementTree() const {
    return runtime_.elementTree();
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
    resizeCursorActive_ = false;
    if (panelState_ != nullptr) {
        panelState_->moreMenuOpen = false;
    }
    if (dockPosition_ == DockPosition::Floating) {
        closeDetachedWindow();
    }
    resetCursor();
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
    resizeCursorActive_ = false;
    resetCursor();
    if (position == DockPosition::Floating) {
        openDetachedWindow();
    }
    requestCompose();
}

void DevtoolsHost::describeDetachedWindow(app::detail::DetachedWindowOptions& options) const {
    options.title = "EUI DevTools";
    options.clearColor = devtoolsTheme().panelBackground;
    options.width = 640;
    options.height = 420;
}

void DevtoolsHost::setDetachedWindowOpener(std::function<void()> opener) {
    detachedWindowOpener_ = std::move(opener);
}

void DevtoolsHost::setDetachedWindowCloser(std::function<void()> closer) {
    detachedWindowCloser_ = std::move(closer);
}

void DevtoolsHost::openDetachedWindow() {
    if (detachedWindowOpener_) {
        detachedWindowOpener_();
    }
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
    if (detachedWindowCloser_) {
        detachedWindowCloser_();
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

core::Rect DevtoolsHost::contentBounds() const {
    const float width = static_cast<float>(framebufferWidth_);
    const float height = static_cast<float>(framebufferHeight_);
    if (!visible_ || dockPosition_ == DockPosition::Floating) {
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
    return static_cast<int>(contentBounds().height);
}

float DevtoolsHost::performanceScrollOffset() const {
    return panelState_ != nullptr ? panelState_->performanceScrollOffset : 0.0f;
}

const std::string& DevtoolsHost::selectedElement() const {
    static const std::string empty;
    return panelState_ != nullptr ? panelState_->selectedElement : empty;
}

const std::vector<std::string>& DevtoolsHost::collapsedElements() const {
    static const std::vector<std::string> empty;
    return panelState_ != nullptr ? panelState_->collapsedElements : empty;
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

void DevtoolsHost::filterInput(std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent) {
    if (!visible_ || panelSize() <= 0) {
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
        resizeCursorActive_ = resizing_ || overResizeBoundary(event.x, event.y);

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
        if (!(inside && !resizeCursorActive_)) {
            panelEvent.x = kOutsidePointer;
            panelEvent.y = kOutsidePointer;
            panelEvent.deltaX = 0.0;
            panelEvent.deltaY = 0.0;
        }
        runtime_.pushPointerEvent(panelEvent);

        if (!inside && !overBoundary && !captured) {
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
    if (!resizeCursorActive_) {
        resetCursor();
    }
}

void DevtoolsHost::composeUi(core::dsl::Ui& ui, float width, float height, const core::Rect& panel, bool detached) {
    // Panel state lives in the panel Runtime, so it is torn down with it and the
    // host never keeps a second copy of the same truth. Only one of the two panel
    // runtimes composes at a time, so the host tracks whichever one is active.
    DevtoolsPanelState& state = ui.state<DevtoolsPanelState>("devtools.panel");
    panelState_ = &state;
    composeDevtoolsUi(ui, {width, height, panel, detached, dockPosition_, &state, &performanceSnapshot_, &elementTree_}, {
        [this, &state](DevtoolsTab tab) {
            if (state.activeTab == tab) {
                return;
            }
            state.activeTab = tab;
            state.moreMenuOpen = false;
            requestCompose();
        },
        [this](DockPosition position) { selectDockPosition(position); },
        [this, &state] {
            state.moreMenuOpen = !state.moreMenuOpen;
            requestCompose();
        },
        [this, &state] {
            if (!state.moreMenuOpen) {
                return;
            }
            state.moreMenuOpen = false;
            requestCompose();
        },
        [this] { close(); },
        [this, &state](float offset) {
            state.performanceScrollOffset = offset;
            requestCompose();
        },
        [this, &state](float offset) {
            state.elementsScrollOffset = offset;
            requestCompose();
        },
        [this, &state](const std::string& id) {
            if (state.selectedElement == id) {
                return;
            }
            state.selectedElement = id;
            requestCompose();
        },
        [this, &state](const std::string& id) {
            const auto collapsed = std::find(state.collapsedElements.begin(), state.collapsedElements.end(), id);
            if (collapsed == state.collapsedElements.end()) {
                state.collapsedElements.push_back(id);
            } else {
                state.collapsedElements.erase(collapsed);
            }
            requestCompose();
        },
        [this](const std::string& id) {
            core::window::setClipboardText(id);
        }
    });
}

void DevtoolsHost::composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    composeUi(ui, screen.width, screen.height, {0.0f, 0.0f, screen.width, screen.height}, true);
}

bool DevtoolsHost::update(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds) {
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

void DevtoolsHost::updateCursor(core::window::Handle window) {
    cursorWindow_ = window;
    if (!visible_ || dockPosition_ == DockPosition::Floating || !resizeCursorActive_ || window == nullptr) {
        resetCursor();
        return;
    }
    const core::window::CursorType type = dockPosition_ == DockPosition::Bottom
        ? core::window::CursorType::ResizeVertical
        : core::window::CursorType::ResizeHorizontal;
    if (resizeCursor_ != nullptr && resizeCursorType_ != type) {
        core::window::destroyCursor(resizeCursor_);
        resizeCursor_ = nullptr;
    }
    if (resizeCursor_ == nullptr) {
        resizeCursor_ = core::window::createStandardCursor(type);
        resizeCursorType_ = type;
    }
    if (resizeCursor_ != nullptr) {
        core::window::setCursor(window, resizeCursor_);
        resizeCursorApplied_ = true;
    }
}

void DevtoolsHost::resetCursor() {
    if (resizeCursorApplied_ && cursorWindow_ != nullptr) {
        core::window::setCursor(cursorWindow_, nullptr);
        resizeCursorApplied_ = false;
    }
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

void DevtoolsHost::releaseGraphicsResources() {
    runtime_.releaseGraphicsResources(false);
    panelState_ = nullptr;
    composeRequested_ = true;
}

void DevtoolsHost::shutdown() {
    resetCursor();
    if (resizeCursor_ != nullptr) {
        core::window::destroyCursor(resizeCursor_);
        resizeCursor_ = nullptr;
    }
    runtime_.shutdown(false);
    panelState_ = nullptr;
    visible_ = false;
    dockPosition_ = DockPosition::Bottom;
    performanceSnapshot_ = {};
    detachedWindowOpener_ = {};
    detachedWindowCloser_ = {};
    resizing_ = false;
    resizeCursorActive_ = false;
    panelHeightLogical_ = 0.0f;
    panelWidthLogical_ = 0.0f;
    resizeCursorApplied_ = false;
    cursorWindow_ = nullptr;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace modules::devtools

#endif
