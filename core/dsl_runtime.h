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
#include "core/window/window_backend.h"

#if defined(EUI_DEBUG_BUILD)
#include "core/runtime/runtime_inspector.h"
#endif

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

#if defined(EUI_DEBUG_BUILD)
    void setInputFilter(std::function<void(std::vector<PointerEvent>&, ScrollEvent&)> filter) {
        inputFilter_ = std::move(filter);
    }

    // Debug overlays draw into the same render cache as the page, so every blit
    // carries a complete frame. The overlay is asked to repaint whenever the
    // cache is rebuilt: on a full paint and on every dirty rect.
    void setOverlayRenderer(std::function<void(int, int, float, const Rect*)> renderer) {
        overlayRenderer_ = std::move(renderer);
    }

    // An overlay runtime is driven by its host instead of a window: it has no
    // window input queue. The host pushes the pointer and scroll state that the
    // next update() should consume, and update() is called without a window.
    void pushPointerEvent(const PointerEvent& event) {
        overlayPointerEvents_.push_back(event);
    }

    void pushScrollEvent(const ScrollEvent& event) {
        overlayScrollEvent_ = event;
    }
#endif

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

#if defined(EUI_DEBUG_BUILD)
    // Renders this runtime directly on top of the current frame, clipped to its
    // viewport. Overlay runtimes draw outside the app Runtime render pass.
    void renderDirectOverlay(int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect = nullptr);

    // Read-only copy of the current element tree in pre-order. It is built on
    // demand by the debug tools that display it, so the runtime keeps no extra
    // state for them, and it stops at `maximumNodes` to bound the copy.
    runtime::ElementTreeSnapshot elementTree(
        std::size_t maximumNodes = runtime::kElementTreeMaximumNodes) const;

    // Bumped whenever the element structure changes, which lets a tree view tell
    // "same tree, new frames" from "the tree itself changed" without diffing.
    std::uint64_t elementStructureRevision() const { return elementStructureRevision_; }
#endif

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
    std::string hitTest(const PointerEvent& event, float dpiScale, Predicate&& predicate) const;

    template <typename Predicate>
    bool hitTestElement(const Element& element,
                        const PointerEvent& event,
                        float dpiScale,
                        const RenderTransform& inheritedTransform,
                        Predicate& predicate,
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
#if defined(EUI_DEBUG_BUILD)
    std::function<void(std::vector<PointerEvent>&, ScrollEvent&)> inputFilter_;
    std::function<void(int, int, float, const Rect*)> overlayRenderer_;
    std::vector<PointerEvent> overlayPointerEvents_;
    ScrollEvent overlayScrollEvent_;
    std::uint64_t elementStructureRevision_ = 0;
#endif
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
#include "core/runtime/runtime_lifecycle.h"
#include "core/runtime/runtime_input.h"
#include "core/runtime/runtime_update.h"
