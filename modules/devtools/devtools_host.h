#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/dsl_runtime.h"
#include "core/render/primitive.h"
#include "eui/detail/overlay_hooks.h"
#include "modules/devtools/devtools_tree.h"
#include "modules/devtools/devtools_ui.h"

#include <deque>
#include <functional>
#include <string>

namespace modules::devtools {

// Attaches or detaches the panel. Called by Session.
void attachDevtoolsHost();
void detachDevtoolsHost();

// Debug panel host. It owns the panel Runtime, the dock geometry, the window it detaches
// into and the input the page must not see.
//
// It is the only part of the panel that talks to the framework, and it does so by hand:
// the frame and the windows come from the function slots in `eui/detail/overlay_hooks.h`,
// and everything it reads or writes on the page it does straight on the page Runtime. The
// panel's own data flow (what it wants copied, and when) lives here too, so the framework
// keeps no state for a tool it does not know about.
class DevtoolsHost {
public:
    // One field edit the panel asked for. The value carries the kind it holds, so the host
    // hands it to the runtime without knowing which field the panel edited. `clear` puts
    // the element's own value back instead of writing one, and an empty id clears every
    // patch on the page.
    struct ElementPropertyEdit {
        std::string id;
        core::dsl::runtime::ElementField field = core::dsl::runtime::ElementField::Color;
        core::dsl::runtime::FieldValue value;
        bool clear = false;
    };

    // ---- the framework's side: wired onto the page, called by the app loop ----

    // Wires the panel onto the page it inspects: the runtime hooks it uses, and the window
    // services the app layer owns. `page` is the runtime to read and write; a test may
    // attach none and drive the setters below itself.
    void attach(core::dsl::Runtime* page, const app::detail::OverlayWindows& windows);
    void detach();

    // Window area left to the page, in framebuffer pixels. The panel takes the rest of it
    // while it is docked and visible.
    core::Rect contentBounds(int windowWidth, int windowHeight, float dpiScale) const;
    // True while the panel claims the key, which keeps it away from the app's own handler.
    bool handleHotkey(const core::KeyEvent& key);
    // One frame of the panel: everything it reads from the page, the edits it made, and the
    // panel Runtime itself. Returns true when it repainted.
    bool frame(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds);
    // Draws the panel into the page render cache. `dirtyRect` is null on a full paint.
    void render(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect);
    void setPerformanceSnapshot(const app::PerformanceSnapshot& snapshot);
    // Draws the box model preview of the element the panel points at, inside the page render
    // pass: the geometry is already resolved in `pass`.
    void renderPageOverlay(const core::dsl::runtime::RenderPassContext& pass);
    void releaseGraphicsResources();
    void shutdown();

    // ---- the panel's side: state the panel widget layer drives and reads ----

    void filterInput(std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent);
    bool wantsElementTree() const;
    void setElementTree(const ElementTreeSnapshot& tree);
    const std::string& hoveredElement() const;
    bool pickingElement() const;
    core::PointerEvent pickedPointer() const;
    void setElementUnderPointer(const std::string& id);
    const std::string& propertiesElement() const;
    void setElementProperties(const core::dsl::runtime::ElementValues& values);
    bool takeElementPropertyEdit(ElementPropertyEdit& edit);
    void setElementPropertyOverrideCount(std::size_t count);

    // The panel's own frame, without the page: composes the panel when its state changed
    // and advances it with the frame the page was updated with.
    bool updatePanel(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds);

    void composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen);
    void detachedWindowClosed();

    bool visible() const;
    DockPosition dockPosition() const;
    DevtoolsTab activeTab() const;
    int contentHeight() const;
    float performanceScrollOffset() const;
    const std::string& selectedElement() const;
    const std::vector<std::string>& expandedElements() const;
    const ElementTreeSnapshot& elementTree() const;
    const core::dsl::runtime::ElementValues& properties() const;
    std::size_t propertyOverrideCount() const;

    // The panel's own element tree. Tests use it to see what the panel composed
    // without a renderer; it is also what a future "inspect the inspector" view
    // would read.
    ElementTreeSnapshot panelElementTree() const;

private:
    // Copying the page to the panel and writing the panel's edits back, each throttled by
    // the panel's own state so the page pays only for what the panel shows.
    void publishElementTree();
    void publishElementProperties();
    void publishPickedElement();
    void applyElementPropertyEdits();

    int panelSize() const;
    int minimumPanelSize() const;
    int maximumPanelSize() const;
    core::Rect panelBounds() const;
    void selectDockPosition(DockPosition position);
    void selectTab(DevtoolsTab tab);
    void setPickingElement(bool picking);
    void capturePickPointer(core::PointerEvent& event);
    void dismissMoreMenu();
    void close();
    void openDetachedWindow();
    void closeDetachedWindow();
    void composeUi(core::dsl::Ui& ui, float width, float height, const core::Rect& panel, bool detached);
    // Wires every command the panel can raise to the host, by name.
    DevtoolsUiActions buildActions(DevtoolsPanelState& state);
    void requestCompose();
    void queueElementPropertyEdit(const ElementPropertyEdit& edit);
    bool overResizeBoundary(double x, double y) const;

    // The page the panel inspects, null until it is attached (and in tests that drive the
    // panel alone).
    core::dsl::Runtime* page_ = nullptr;
    app::detail::OverlayWindows windows_;

    // What the panel asked to be copied and when it was last copied: the tree and the
    // values are rebuilt on a structural change and refreshed at most this often in
    // between, so a hover or an animation does not cost a page-sized walk every frame.
    static constexpr double kRefreshSeconds = 0.25;
    std::uint64_t treeRevision_ = 0;
    double treeRefreshTime_ = 0.0;
    std::string propertiesId_;
    std::uint64_t propertiesRevision_ = 0;
    double propertiesRefreshTime_ = 0.0;
    bool propertiesStale_ = true;

    core::dsl::Runtime runtime_;
    DevtoolsPanelState* panelState_ = nullptr;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float dpiScale_ = 1.0f;
    float panelHeightLogical_ = 0.0f;
    float panelWidthLogical_ = 0.0f;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
    int dragStartSize_ = 0;
    bool resizing_ = false;
    // The panel edge owns the pointer: it is what takes a press that resizes the
    // panel away from the panel's own controls.
    bool panelEdgeActive_ = false;
    // Picking: the pointer the panel wants answered, the element it landed on, and
    // whether the next answer commits it as the selection.
    core::PointerEvent pickedPointer_;
    std::string elementUnderPointer_;
    bool pickCommitPending_ = false;
    bool visible_ = false;
    DockPosition dockPosition_ = DockPosition::Bottom;
    app::PerformanceSnapshot performanceSnapshot_;
    ElementTreeSnapshot elementTree_;
    core::dsl::runtime::ElementValues properties_;
    std::deque<ElementPropertyEdit> propertyEdits_;
    std::size_t propertyOverrideCount_ = 0;
    bool composeRequested_ = true;
    // The primitive the box model preview is drawn with. The overlay owns what it draws
    // with, which is what lets the framework hand over the render pass without keeping a
    // graphics object of the tool's alive for it.
    core::RoundedRectPrimitive boxPreviewPrimitive_;
    bool boxPreviewPrimitiveInitialized_ = false;
};

} // namespace modules::devtools

#endif
