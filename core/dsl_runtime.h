#pragma once

#include "core/dsl.h"
#include "core/platform/platform.h"
#include "core/input/input_state.h"
#include "core/render/image.h"
#include "core/render/primitive.h"
#include "core/render/render_backend.h"
#include "core/render/text.h"
#include "core/runtime/runtime_animation.h"
#include "core/runtime/runtime_dirty.h"
#include "core/runtime/runtime_geometry.h"
#include "core/runtime/runtime_hit_test.h"
#include "core/runtime/runtime_instances.h"
#include "core/runtime/runtime_state_bindings.h"
#include "core/tooling/config.h"
#include "core/tooling/model.h"
#include "core/tooling/state.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace core::dsl {

class Runtime {
public:
    bool initialize();

    bool initialize(core::window::Handle window);

    void setKeyEventHandler(std::function<void(const KeyEvent&)> handler) {
        keyEventHandler_ = std::move(handler);
    }

    // The tool state of this runtime: created the first time a tool talks to the
    // runtime, null when none ever did. The runtime never asks whether tools are part
    // of the build; the seam it calls does (core/tooling/hooks.h).
    runtime::ToolingState* tooling() { return tooling_.get(); }
    const runtime::ToolingState* tooling() const { return tooling_.get(); }
    runtime::ToolingState& ensureTooling();

    // Sets the input filter a tool uses to keep events away from the page.
    void setInputFilter(std::function<void(std::vector<PointerEvent>&, ScrollEvent&)> filter);

    // A tool draws into the same render cache as the page, so every blit carries a
    // complete frame. The renderer is asked to repaint whenever the cache is rebuilt:
    // on a full paint and on every dirty rect.
    void setOverlayRenderer(std::function<void(int, int, float, const Rect*)> renderer);

    // A runtime driven by a host instead of a window has no input queue of its own. The
    // host pushes the pointer and scroll state the next update() should consume, and
    // update() is called without a window.
    void pushPointerEvent(const PointerEvent& event);

    void pushScrollEvent(const ScrollEvent& event);

    // Writes the values a tool replaced back onto the composed tree. The compose hook
    // calls it before layout; nothing else may, because after layout the readers would
    // disagree about the value an element has.
    void applyToolPatches();

    template <typename ComposeFn>
    void compose(const std::string& pageId, float logicalWidth, float logicalHeight, ComposeFn&& composeFn);

    template <typename ComposeFn>
    void compose(const std::string& pageId, const Rect& viewport, ComposeFn&& composeFn);

    bool update(core::window::Handle window, float deltaSeconds, float pointerScale, float dpiScale, bool inputEnabled = true);

    bool isAnimating() const;

    bool composeRequested() const;

    bool paintRequested() const;

    void requestFullPaint();

    void render(int windowWidth, int windowHeight, float dpiScale, const Color& clearColor);

    void render(int windowWidth, int windowHeight, float dpiScale);

    // Renders this runtime directly on top of the current frame, clipped to its
    // viewport. Overlay runtimes draw outside the app Runtime render pass.
    void renderDirectOverlay(int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect = nullptr);

    // Read-only copy of the current element tree in pre-order. It is built on
    // demand by the tools that display it, so the runtime keeps no copy of its own,
    // and it stops at `maximumNodes` to bound the work.
    runtime::ElementTreeSnapshot elementTree(
        std::size_t maximumNodes = runtime::kElementTreeMaximumNodes) const;

    // Bumped whenever the element structure changes, which lets a reader of the tree
    // snapshot tell "same tree, new frames" from "the tree itself changed" without
    // diffing. A counter is cheaper than any tool, so the runtime keeps it whether or
    // not a tool is attached.
    std::uint64_t elementStructureRevision() const { return elementStructureRevision_; }

    // Marks the element the pointer is over in a tree view. The renderer draws the
    // preview overlay for it using the same transform and clip the element itself
    // is drawn with, after the page content, so the overlay is never covered by
    // the page. It is a preview: a tool clears it again when the pointer leaves.
    // An empty id clears the mark.
    void setHoveredElement(const std::string& id);
    const std::string& hoveredElement() const;

    // Geometry of the preview overlay for the current frame; the renderer draws
    // exactly this, and tests read it without a renderer.
    runtime::DebugInspection debugHoverInspection(float dpiScale);

    // The element the pointer is over, for a tool that picks elements on the page
    // instead of interacting with them. Every element is a candidate, disabled and
    // non-interactive ones included, because a picker inspects what is drawn rather
    // than what takes input; the answer is the topmost element that draws at the
    // point, inside its ancestors' clips. Coordinates are in framebuffer pixels,
    // the space this runtime's own input uses. Empty when the point hits nothing.
    std::string debugElementAt(double x, double y, float dpiScale) const;

    // Properties of one element, read on demand for the element a debug tool shows.
    // Reading a single element instead of copying every node keeps the element tree
    // snapshot cheap for big pages.
    runtime::DebugElementProperties debugElementProperties(const std::string& id);

    // Writes a property on top of an element, for a debug session that edits the
    // page it is inspecting. The value survives recomposes, because the runtime
    // applies its overrides again to every freshly composed tree, and it is never
    // written back into app state: clearing the override is enough to get the
    // element's own value back.
    void setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, float value);
    void setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, const Color& value);
    void setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, bool value);
    void clearDebugElementOverride(const std::string& id, runtime::DebugPropertyId property);
    void clearDebugElementOverrides(const std::string& id);
    void clearAllDebugElementOverrides();
    std::size_t debugElementOverrideCount() const;

    void shutdown(bool releaseCachedImageTextures = true);

    void releaseGraphicsResources(bool releaseCachedImageTextures = true);

private:
    template <typename ComposeFn>
    void composeViewport(const std::string& pageId, const Rect& viewport, bool clipViewport, ComposeFn&& composeFn);

    template <typename Fn>
    void forEachElement(Fn&& fn) const;

    template <typename Fn>
    static void forEachElement(const Element& element, Fn&& fn);

    std::vector<runtime::ElementSnapshot> collectElementStructure() const;

    void applyCursor(core::window::Handle window);

    void destroyCursors();

    void addDirtyRect(const Rect& rect);

    void addDirtyUnion(const Rect& before, const Rect& after);

    void promoteBackdropBlurDirtyRegions(float dpiScale);

    void expandBackdropBlurDirtyRegions(const Element& element,
                                        float dpiScale,
                                        const RenderTransform& inheritedTransform,
                                        Rect& mergedDirty,
                                        bool& expanded);

    Transform pointerRuntimeTransform(const Element& element,
                                      const PointerEvent& event,
                                      float dpiScale,
                                      const std::string& hoverTargetId) const;

    bool updateFrameTarget(const Element& element);

    Rect visualDirtyRectForElement(const Element& element,
                                   float dpiScale,
                                   const RenderTransform& inheritedTransform) const;

    void updateExplicitDirtyKey(const Element& element,
                                float dpiScale,
                                const RenderTransform& inheritedTransform);

    void updateElementTree(const PointerEvent& event,
                           float deltaSeconds,
                           float dpiScale,
                           const std::string& hoverTargetId);

    bool canReuseStaticSubtree(const Element& element,
                               const PointerEvent& event,
                               float dpiScale,
                               const RenderTransform& inheritedTransform,
                               bool ancestorFrameChanged,
                               bool ancestorDisabled) const;

    bool elementHasActiveAnimation(const Element& element) const;

    runtime::PaintBoundsInstance updateElementTree(const Element& element,
                                                   const PointerEvent& event,
                                                   float deltaSeconds,
                                                   float dpiScale,
                                                   const std::string& hoverTargetId,
                                                   const RenderTransform& inheritedTransform,
                                                   bool ancestorFrameChanged,
                                                   bool ancestorDisabled);

    std::string capturedInteractionId() const;

    bool isElementInDisabledTree(const std::string& id) const;

    bool findElementDisabledState(const Element& element,
                                  const std::string& id,
                                  bool ancestorDisabled,
                                  bool& disabledTree) const;

    std::string hitTestInteractive(const PointerEvent& event, float dpiScale) const;

    std::string hitTestFocusable(const PointerEvent& event, float dpiScale) const;

    std::string hitTestScrollable(const PointerEvent& event, float dpiScale) const;

    std::string resolveHoverTarget(const PointerEvent& event, float dpiScale, bool inputEnabled);

    bool canReuseHoverTarget(const PointerEvent& event, float dpiScale) const;

    template <typename Predicate>
    std::string hitTest(const PointerEvent& event, float dpiScale, Predicate&& predicate,
                        bool includeDisabled = false) const;

    template <typename Predicate>
    bool hitTestElement(const Element& element,
                        const PointerEvent& event,
                        float dpiScale,
                        const RenderTransform& inheritedTransform,
                        Predicate& predicate,
                        bool includeDisabled,
                        bool hasClip,
                        const Rect& clipRect,
                        bool ancestorDisabled,
                        std::string& targetId) const;

    bool hitTestFocusableElement(const Element& element,
                                 const PointerEvent& event,
                                 float dpiScale,
                                 const RenderTransform& inheritedTransform,
                                 bool hasClip,
                                 const Rect& clipRect,
                                 bool ancestorDisabled,
                                 std::string& targetId) const;

    void setFocusedId(const std::string& id);

    void updateScroll(const ScrollEvent& event, const std::string& targetId);

    void updateTextInput(const TextInputEvent& event);

    void updateKeyInput(const std::vector<KeyEvent>& events);

    void updateImeCursorRect(core::window::Handle window, float dpiScale);

    void updateInteraction(const Element& element,
                           const PointerEvent& event,
                           float dpiScale,
                           const std::string& hoverTargetId,
                           const RenderTransform& inheritedTransform);

    Transform currentElementTransform(const Element& element) const;

    TransformMatrix hitMatrixForElement(const Element& element, float dpiScale, const Rect& bounds, const RenderTransform& renderTransform) const;

    bool hitContains(const Element& element,
                     const PointerEvent& event,
                     float dpiScale,
                     const Rect& bounds,
                     const RenderTransform& renderTransform) const;

    void updateTimer(const Element& element, float deltaSeconds);

    void updateFrameCallback(const Element& element, float deltaSeconds);

    void updateLayoutElement(const Element& element,
                             float deltaSeconds,
                             float dpiScale,
                             const RenderTransform& inheritedTransform,
                             const PointerEvent& event,
                             const std::string& hoverTargetId);

    void updateRect(const Element& element,
                    float deltaSeconds,
                    float dpiScale,
                    const RenderTransform& inheritedTransform,
                    bool snapFrame);

    void updatePolygon(const Element& element,
                       float deltaSeconds,
                       float dpiScale,
                       const RenderTransform& inheritedTransform,
                       bool snapFrame);

    void updateText(const Element& element,
                    float deltaSeconds,
                    float dpiScale,
                    const RenderTransform& inheritedTransform,
                    bool snapFrame);

    void updateImage(const Element& element,
                     float deltaSeconds,
                     float dpiScale,
                     const RenderTransform& inheritedTransform,
                     bool snapFrame);

    void updateShaderToy(const Element& element,
                         const PointerEvent& event,
                         float deltaSeconds,
                         float dpiScale,
                         const RenderTransform& inheritedTransform,
                         bool snapFrame);

    runtime::DependentVisualState dependentVisualStateForElement(const Element& element,
                                                        float dpiScale,
                                                        const RenderTransform& inheritedTransform) const;

    void updateDependentVisualDirtyRegions(float dpiScale);

    void updateDependentVisualDirtyRegions(const Element& element,
                                           float dpiScale,
                                           const RenderTransform& inheritedTransform);

    void syncScrollStateElement(const Element& element);

    void syncSliderStateElement(const Element& element);

    void syncScrollStateBindings();

    float scrollStepFor(const Element& element) const;

    void addScrollDirtyRect(const runtime::ScrollStateInstance& instance);

    void addSliderDirtyRect(const runtime::SliderStateInstance& instance);

    void setScrollOffset(const std::string& stateId, float offset);

    void applyRuntimeScroll(const Element& element, float delta);

    void updateScrollMotion(float deltaSeconds);

    void beginRuntimeScrollDrag(const Element& element);

    void updateRuntimeScrollDrag(const Element& element, double dragDeltaY, float dpiScale);

    float sliderValueFromPointer(const Element& element, double pointerX, float dpiScale) const;

    void setSliderValue(const std::string& stateId, float value, bool dragging);

    void updateRuntimeSlider(const Element& element, double pointerX, float dpiScale, bool dragging);

    Ui ui_;
    runtime::InstanceStore instances_;
    std::vector<runtime::ElementSnapshot> elementStructure_;
    std::vector<runtime::LogicalDirtyRect> dirtyRects_;
    bool paintRequested_ = true;
    bool animating_ = false;
    bool composeRequested_ = false;
    bool fullPaintRequested_ = true;
    bool wantsHandCursor_ = false;
    bool fullTreeUpdateRequested_ = true;
    bool pruneInstancesRequested_ = true;
    bool previousFrameAnimating_ = false;
    bool hoverTargetCacheValid_ = false;
    PointerEvent hoverTargetCacheEvent_;
    float hoverTargetCacheDpiScale_ = 0.0f;
    std::string hoverTargetCacheId_;
    std::string focusedId_;
    std::function<void(const KeyEvent&)> keyEventHandler_;
    std::uint64_t elementStructureRevision_ = 0;
    // The tool state, allocated on first use; see core/tooling/state.h. The pointer is
    // the same in every configuration, so a runtime has one layout for all of them.
    std::unique_ptr<runtime::ToolingState> tooling_;
    RenderTransform focusedElementRenderTransform_;
    bool focusedElementRenderTransformValid_ = false;
    Rect viewport_;
    bool clipViewport_ = false;
    std::uint64_t updateFrameToken_ = 0;
    core::window::CursorHandle arrowCursor_ = nullptr;
    core::window::CursorHandle handCursor_ = nullptr;
    core::window::CursorHandle currentCursor_ = nullptr;
    core::window::Handle imeCursorWindow_ = nullptr;
    Rect imeCursorRect_;
    bool imeCursorRectValid_ = false;
};

} // namespace core::dsl

#include "core/runtime/runtime_render.h"
// The seam the runtime calls. It is always included, and it is where the decision
// "is tooling part of this build" is made; every call site below it is one line.
#include "core/tooling/hooks.h"
#include "core/runtime/runtime_lifecycle.h"
#include "core/runtime/runtime_input.h"
#include "core/runtime/runtime_update.h"
#include "core/tooling/inspect.h"
