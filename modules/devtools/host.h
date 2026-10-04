#pragma once

#if defined(EUI_TOOLING)

#include "core/dsl_runtime.h"
#include "core/render/primitive.h"
#include "core/render/text.h"
#include "eui/detail/overlay_hooks.h"
#include "modules/devtools/input.h"
#include "modules/devtools/preview.h"
#include "modules/devtools/state.h"
#include "modules/devtools/tree.h"
#include "modules/devtools/ui.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace modules::devtools {

// Attaches or detaches the panel. Called by Session. Attaching returns false when another
// session already owns the panel's place in the app loop, which keeps one owner per slot.
bool attachDevtoolsHost();
void detachDevtoolsHost();
class DevtoolsHost;
DevtoolsHost& devtoolsHostInstance();

// Debug panel host. It owns the panel Runtime, the dock geometry, the window it detaches
// into and the input the page must not see.
//
// It is the only part of the panel that talks to the framework, and it does so by hand:
// the frame and the windows come from the function slots in `eui/detail/overlay_hooks.h`,
// and everything it reads or writes on the page it does straight on the page Runtime.
class DevtoolsHost {
public:
    // One field edit the panel asked for. The value carries the kind it holds, so the host
    // hands it to the runtime without knowing which field the panel edited. `clear` puts
    // the element's own value back instead of writing one, and an empty id clears every
    // patch on the page.
    struct ElementPropertyEdit {
        std::string id;
        modules::devtools::ElementField field = modules::devtools::ElementField::Color;
        modules::devtools::FieldValue value;
        bool clear = false;
    };

    // ---- the framework's side: wired onto the page, called by the app loop ----

    // Wires the panel onto the page it inspects: the runtime hooks it uses, and the window
    // services the app layer owns. `page` is the runtime to read and write; a test may
    // attach none and drive the setters below itself.
    //
    // The page it is replacing is unhooked here, which requires that page to still be alive:
    // the contract is that a page is detached before it is destroyed, and the app layer keeps
    // it — `app::shutdown()` detaches the panel before it shuts the page runtime down. A
    // caller that destroys a page first has to detach it first as well.
    void attach(core::dsl::Runtime* page, const app::detail::OverlayWindows& windows);
    // Takes the panel off the page and throws the page's session away. It touches no device,
    // so it is safe after the window and its render backend are gone — which is when the
    // session that owns the panel is destroyed.
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
    // Shuts the panel's own runtime down and forgets the panel state, so what a run left
    // behind does not reach the next one in the same process. The app layer runs this while
    // its device is still current; a session torn down later only drops the preview handle,
    // because the device it was created with is gone.
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
    void setElementProperties(const modules::devtools::ElementValues& values);
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
    float scaleScrollOffset() const;
    float scaleOverride() const;
    float systemDpi() const;
    const std::string& selectedElement() const;
    const std::vector<std::string>& expandedElements() const;
    const ElementTreeSnapshot& elementTree() const;
    const InstanceStateSnapshot& instanceState() const;
    bool wantsInstanceState() const;
    float stateScrollOffset() const;
    const InputSnapshot& inputSnapshot() const;
    bool wantsInputState() const;
    float inputScrollOffset() const;
    const modules::devtools::ElementValues& properties() const;
    std::size_t propertyOverrideCount() const;

    // The panel's own element tree. Tests use it to see what the panel composed
    // without a renderer; it is also what a future "inspect the inspector" view
    // would read.
    ElementTreeSnapshot panelElementTree() const;

    // Direct controls for automation and MCP bridge
    void openDevtools(DevtoolsTab initialTab = DevtoolsTab::Mcp);
    void setTab(DevtoolsTab tab);
    core::dsl::Runtime* pageRuntime() const { return session_.page; }
    bool modifyElementProperty(const std::string& id, ElementField field, const FieldValue& value);
    void cacheRenderFramebuffer(int width, int height, std::vector<unsigned char> rgba);
    // What dates the cached frame. Every frame cached bumps it, so a caller that is about to
    // ask for a capture reads it first and hands it back to getCachedFramebufferSince: that is
    // what tells the frame the capture produced from the one still sitting there from the
    // previous ask, which is otherwise what a first read would be answered with.
    std::uint64_t framebufferGeneration() const;
    bool getCachedFramebufferSince(std::uint64_t generation, std::uint64_t& outFrameGeneration, int& outWidth, int& outHeight, std::vector<unsigned char>& outRgba);
    void requestFramebufferCapture();
    bool captureRequested() const;
    // Marks the screenshot tool wants drawn on the frames it is about to capture, and the call
    // that takes them down again once the frame carrying them has been read. The overlay is drawn
    // by the page's render pass, so it is requested before the capture that waits for a frame and
    // cleared after, which is what keeps every frame in between carrying it.
    void requestMarkOverlay(std::vector<MarkBounds> marks);
    void clearMarkOverlay();
    void requestCompose();
    float dpiScale() const { return dpiScale_ > 0.0f ? dpiScale_ : 1.0f; }
    // The size the last frame was rendered at, which is what a screenshot is measured in. Marks
    // report logical coordinates, so this and dpiScale() are the pair that converts between the
    // two spaces; both are zero before the first frame, when there is nothing to convert.
    int framebufferWidth() const { return framebufferWidth_; }
    int framebufferHeight() const { return framebufferHeight_; }
    std::uint64_t frameSequence() const;
    bool waitForFrame(std::uint64_t targetSequence, std::chrono::milliseconds timeout = std::chrono::milliseconds(1200));

private:
    // Everything the panel knows about the page it is attached to, and everything it wrote
    // on it. `attach` creates one and `detach` throws it away whole: a patch, a copied
    // snapshot or a picked element that names something the previous page had must not reach
    // a page that does not have it. What the user chose for the panel itself (dock, size,
    // tab, scroll offsets) lives outside this and stays.
    struct PageSession {
        core::dsl::Runtime* page = nullptr;
        app::detail::OverlayWindows windows;

        // What the panel wrote on the page, and the store that puts it back after every
        // compose. It is the panel's own state: the framework is never told what a patch is.
        ElementPatches patches;
        // Edits the panel asked for and the host has not applied to the page yet.
        std::deque<ElementPropertyEdit> edits;
        std::size_t overrideCount = 0;

        // What the panel asked to be copied and when it was last copied: the tree and the
        // values are rebuilt on a structural change and refreshed at most this often in
        // between, so a hover or an animation does not cost a page-sized walk every frame.
        std::uint64_t treeRevision = 0;
        double treeRefreshTime = 0.0;
        std::string propertiesId;
        std::uint64_t propertiesRevision = 0;
        double propertiesRefreshTime = 0.0;
        bool propertiesStale = true;
        ElementTreeSnapshot tree;
        modules::devtools::ElementValues properties;
        app::PerformanceSnapshot performance;
        std::uint64_t stateRevision = 0;
        double stateRefreshTime = 0.0;
        InstanceStateSnapshot instanceState;
        std::uint64_t inputEventsCount = 0;
        double inputRefreshTime = 0.0;
        InputSnapshot inputSnapshot;

        // Picking: the pointer the panel wants answered, the element it landed on, and
        // whether the next answer commits it as the selection.
        core::PointerEvent pickedPointer;
        std::string elementUnderPointer;
        bool pickCommitPending = false;
    };

    // Copying the page to the panel and writing the panel's edits back, each throttled by
    // the panel's own state so the page pays only for what the panel shows.
    void publishElementTree();
    void publishElementProperties();
    void publishInstanceState();
    void publishInputSnapshot();
    void publishPickedElement();
    void applyElementPropertyEdits();

    bool hasPage() const { return session_.page != nullptr; }
    // Takes the panel's hooks off the page it is wired to, so that page stops calling it.
    void unhookPage();
    // The panel's preferences (dock, size, tab, expansion) kept in its own runtime; the
    // elements they name belong to the page, so they go with it.
    void forgetPageSelection();

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
    void queueElementPropertyEdit(const ElementPropertyEdit& edit);
    bool overResizeBoundary(double x, double y) const;

    // The page the panel inspects and what it wrote there. It is empty until the panel is
    // attached, and `detach` empties it again in one move.
    PageSession session_;

    // How long a copied tree or value set is reused while the page does not change shape.
    static constexpr double kRefreshSeconds = 0.25;

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
    bool visible_ = false;
    DockPosition dockPosition_ = DockPosition::Bottom;
    DevtoolsTab requestedInitialTab_ = DevtoolsTab::Elements;
    bool hasRequestedInitialTab_ = false;
    bool composeRequested_ = true;
    // The primitive the box model preview is drawn with. The overlay owns what it draws
    // with, which is what lets the framework hand over the render pass without keeping a
    // graphics object of the tool's alive for it.
    core::RoundedRectPrimitive boxPreviewPrimitive_;
    bool boxPreviewPrimitiveInitialized_ = false;
    core::TextPrimitive boxPreviewTextPrimitive_;
    bool boxPreviewTextPrimitiveInitialized_ = false;
    // The mark overlay a screenshot asked for, and the primitives it draws with. They are the
    // panel's, like the box preview's, so a capture that never asks for marks leaves the page
    // with nothing drawn on it and no graphics object held for a feature nobody used.
    std::vector<MarkBounds> markOverlay_;
    core::RoundedRectPrimitive markOverlayPrimitive_;
    bool markOverlayPrimitiveInitialized_ = false;
    core::TextPrimitive markOverlayTextPrimitive_;
    bool markOverlayTextPrimitiveInitialized_ = false;

    // Framebuffer capture caching for MCP vision. The render thread fills it and the MCP
    // worker thread reads it, so the pixels and the generation that dates them are kept under
    // one lock: a reader that compared generations must not then copy the pixels of the other
    // frame, which is what two separate reads would let it do.
    mutable std::mutex framebufferMutex_;
    bool captureRequested_ = false;
    int cachedFbWidth_ = 0;
    int cachedFbHeight_ = 0;
    std::uint64_t cachedFbGeneration_ = 0;
    std::vector<unsigned char> cachedFbPixels_;

    // Frame synchronization for deterministic automation/MCP actions
    mutable std::mutex frameMutex_;
    std::condition_variable frameCv_;
    std::uint64_t frameSequence_ = 0;
};

} // namespace modules::devtools

#endif
