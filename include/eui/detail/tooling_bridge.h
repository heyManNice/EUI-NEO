#pragma once

#include "core/tooling/config.h"

// The runtime type is needed in both configurations: the release entry points still
// have to reach the page to hand the app its own key handler.
#include "core/dsl_runtime.h"
#include "core/app/performance_snapshot.h"
#include "eui/detail/overlay_host.h"
#include "eui/dsl_app.h"

#if EUI_TOOLING_ENABLED

#include "core/platform/platform.h"
#include "eui/app.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#endif

namespace app::tooling {

// The bridge between the app loop and a tool.
//
// The app loop owns the frame: it composes the page, updates the runtime and renders.
// A tool wants to see the page, replace values on it, and draw above it; the bridge is
// the only place that knows the order those happen in, so the loop calls four things
// and stays free of tooling branches. Every entry point exists in both configurations:
// without tooling they do nothing, which is what "no tool attached" means anyway.

// The overlay host a tool registered, null when none is attached.
inline detail::OverlayHost* host();

// The window area left to the page. A tool that reserved space takes it out of the
// window; without a tool the page gets all of it.
inline core::Rect contentBounds(float windowWidth, float windowHeight);

// What one frame of the tool did, so the app loop can react to it.
struct FrameResult {
    bool repainted = false;
    core::Rect contentBounds{};
};

// One frame of the tool, in the order the two sides depend on:
//
//   1. the tree of this frame, so the tool composes against what the user sees
//   2. the edits it made, read back before the values below, so an edit shows its
//      result in the same frame instead of the next one
//   3. the values of the element it shows
//   4. the element a picker points at, before the preview, so the page marks what the
//      pointer is on in the frame it moved
//   5. the preview it asked for, and then the tool's own update
inline FrameResult driveFrame(core::dsl::Runtime& runtime,
                              int windowWidth,
                              int windowHeight,
                              float dpiScale,
                              float deltaSeconds);

// Hands a tool the hooks that need the app layer: the input filter, the overlay
// renderer, the key handler and the window a tool detaches into.
inline void wireHost(core::dsl::Runtime& runtime, const DslAppConfig& config);

// The app loop publishes the diagnostics snapshot once per frame; a tool without a
// performance view ignores it.
inline void publishPerformance(const PerformanceSnapshot& snapshot);

// The window is losing its device, or the app is going away: the tool hears about it.
inline void releaseGraphics();
inline void shutdown();

#if EUI_TOOLING_ENABLED

// Frame-only changes (animation, scrolling, hover) refresh the published tree at most
// this often. A structural change always refreshes immediately, so a viewer reacts to
// the page it inspects without waiting.
inline constexpr double kTreeRefreshSeconds = 0.25;

// The throttle state lives with the bridge, so the app loop keeps no tooling state.
struct PublishState {
    std::uint64_t treeRevision = 0;
    double treeRefreshTime = 0.0;
    std::string propertiesId;
    std::uint64_t propertiesRevision = 0;
    double propertiesRefreshTime = 0.0;
    bool propertiesStale = true;
};

inline PublishState& publishState() {
    static PublishState state;
    return state;
}

// Copies the page element tree to the tool that displays it. Walking the tree costs time
// and memory proportional to the page, and the tool only pays for it while it asks for
// the tree.
inline void publishElementTree(core::dsl::Runtime& runtime, detail::OverlayHost& overlay) {
    if (!overlay.wantsElementTree()) {
        return;
    }
    PublishState& state = publishState();
    const std::uint64_t revision = runtime.elementStructureRevision();
    const double now = core::window::timeSeconds();
    if (revision == state.treeRevision && now - state.treeRefreshTime < kTreeRefreshSeconds) {
        return;
    }
    state.treeRevision = revision;
    state.treeRefreshTime = now;
    overlay.setElementTree(runtime.elementTree());
}

// Properties are read for the single element the tool shows, and only when it asks. A
// tree walk per frame would cost as much as the page is big, so the read is throttled
// like the tree and repeats immediately after an edit, when the tool has to see what its
// own edit did.
inline void publishElementProperties(core::dsl::Runtime& runtime, detail::OverlayHost& overlay) {
    const std::string& id = overlay.propertiesElement();
    if (id.empty()) {
        return;
    }
    PublishState& state = publishState();
    const std::uint64_t revision = runtime.elementStructureRevision();
    const double now = core::window::timeSeconds();
    const bool sameElement = id == state.propertiesId;
    const bool throttled = revision == state.propertiesRevision &&
                           now - state.propertiesRefreshTime < kTreeRefreshSeconds;
    if (sameElement && !state.propertiesStale && throttled) {
        return;
    }
    state.propertiesId = id;
    state.propertiesRevision = revision;
    state.propertiesRefreshTime = now;
    state.propertiesStale = false;
    overlay.setElementProperties(runtime.elementValues(id));
    overlay.setElementPropertyOverrideCount(runtime.elementPatchCount());
}

// Applies the edits a tool made to the page. This is the only direction that writes: the
// runtime keeps them on top of the app's own values until they are cleared, and the app
// state the page is built from is never touched. The edit carries the value it built, so
// this layer never enumerates the fields a tool can edit.
inline void applyElementPropertyEdits(core::dsl::Runtime& runtime, detail::OverlayHost& overlay) {
    detail::OverlayHost::ElementPropertyEdit edit;
    while (overlay.takeElementPropertyEdit(edit)) {
        if (edit.clear && edit.id.empty()) {
            runtime.clearElementFields();
        } else if (edit.clear) {
            runtime.clearElementField(edit.id, edit.field);
        } else {
            runtime.setElementField(edit.id, edit.field, edit.value);
        }
        publishState().propertiesStale = true;
    }
    overlay.setElementPropertyOverrideCount(runtime.elementPatchCount());
}

// A tool that picks elements owns the pointer: the page is told the pointer left, and
// what is under it comes back from the page's own hit test. Asking the page keeps the
// answer in the same space as the frame the user is looking at: transforms, ancestor
// clips and paint order are the ones that drew it.
inline void publishPickedElement(core::dsl::Runtime& runtime, detail::OverlayHost& overlay, float dpiScale) {
    if (!overlay.pickingElement()) {
        overlay.setElementUnderPointer(std::string{});
        return;
    }
    const core::PointerEvent pointer = overlay.pickedPointer();
    overlay.setElementUnderPointer(runtime.elementIdAt(pointer.x, pointer.y, dpiScale));
}

inline detail::OverlayHost* host() {
    return detail::overlayHost();
}

inline core::Rect contentBounds(float windowWidth, float windowHeight) {
    detail::OverlayHost* overlay = host();
    if (overlay == nullptr) {
        return {0.0f, 0.0f, windowWidth, windowHeight};
    }
    return overlay->contentBounds();
}

inline FrameResult driveFrame(core::dsl::Runtime& runtime,
                              int windowWidth,
                              int windowHeight,
                              float dpiScale,
                              float deltaSeconds) {
    FrameResult result;
    detail::OverlayHost* overlay = host();
    if (overlay == nullptr) {
        result.contentBounds = {0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
        return result;
    }
    publishElementTree(runtime, *overlay);
    applyElementPropertyEdits(runtime, *overlay);
    publishElementProperties(runtime, *overlay);
    publishPickedElement(runtime, *overlay, dpiScale);
    runtime.setHoveredElement(overlay->hoveredElement());

    // The overlay draws inside the app render cache, so a repaint of its own has to
    // rebuild the cached frame it belongs to.
    if (overlay->update(windowWidth, windowHeight, dpiScale, deltaSeconds)) {
        runtime.requestFullPaint();
        result.repainted = true;
    }
    result.contentBounds = overlay->contentBounds();
    return result;
}

inline void wireHost(core::dsl::Runtime& runtime, const DslAppConfig& config) {
    detail::OverlayHost* overlay = host();
    const std::function<void(const eui::KeyEvent&)> appKeyHandler = config.keyEventHandler;
    if (overlay == nullptr) {
        runtime.setKeyEventHandler(appKeyHandler);
        return;
    }
    runtime.setInputFilter([overlay](std::vector<core::PointerEvent>& pointerEvents,
                                     core::ScrollEvent& scrollEvent) {
        overlay->filterInput(pointerEvents, scrollEvent);
    });
    runtime.setOverlayRenderer([overlay](int width, int height, float dpiScale, const core::Rect* dirtyRect) {
        overlay->render(width, height, dpiScale, dirtyRect);
    });
    runtime.setPassRenderer([overlay](const core::dsl::runtime::RenderPassContext& pass) {
        overlay->renderPageOverlay(pass);
    });
    runtime.setKeyEventHandler([appKeyHandler](const eui::KeyEvent& key) {
        detail::OverlayHost* active = detail::overlayHost();
        if (active != nullptr && active->handleHotkey(key)) {
            return;
        }
        if (appKeyHandler) {
            appKeyHandler(key);
        }
    });
    // The overlay describes its own window, but the app layer owns window creation and
    // closing.
    const std::shared_ptr<DslWindowHandle> detachedHandle = std::make_shared<DslWindowHandle>();
    const std::shared_ptr<unsigned int> detachedGeneration = std::make_shared<unsigned int>(0);
    overlay->setDetachedWindowOpener([overlay, detachedHandle, detachedGeneration] {
        detail::DetachedWindowOptions options;
        overlay->describeDetachedWindow(options);
        const unsigned int generation = ++*detachedGeneration;
        *detachedHandle = openWindow(DslWindowConfig{}
                .title(options.title)
                .pageId("eui.overlay.detached")
                .clearColor(options.clearColor)
                .windowSize(options.width, options.height)
                .onKeyEvent([overlay](const eui::KeyEvent& key) {
                    overlay->handleHotkey(key);
                })
                .onClosed([overlay, detachedGeneration, generation] {
                    // A window from an earlier detach must not touch the overlay.
                    if (*detachedGeneration == generation) {
                        overlay->detachedWindowClosed();
                    }
                }),
            [overlay](eui::Ui& ui, const eui::Screen& screen) {
                overlay->composeDetached(ui, screen);
            });
    });
    overlay->setDetachedWindowCloser([detachedHandle] {
        detachedHandle->requestClose();
    });
}

inline void publishPerformance(const PerformanceSnapshot& snapshot) {
    if (detail::OverlayHost* overlay = host()) {
        overlay->setPerformanceSnapshot(snapshot);
    }
}

inline void releaseGraphics() {
    if (detail::OverlayHost* overlay = host()) {
        overlay->releaseGraphicsResources();
    }
}

inline void shutdown() {
    if (detail::OverlayHost* overlay = host()) {
        overlay->shutdown();
    }
}

#else

inline detail::OverlayHost* host() {
    return nullptr;
}

inline core::Rect contentBounds(float windowWidth, float windowHeight) {
    return {0.0f, 0.0f, windowWidth, windowHeight};
}

inline FrameResult driveFrame(core::dsl::Runtime&,
                              int windowWidth,
                              int windowHeight,
                              float,
                              float) {
    FrameResult result;
    result.contentBounds = {0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
    return result;
}

inline void wireHost(core::dsl::Runtime& runtime, const DslAppConfig& config) {
    runtime.setKeyEventHandler(config.keyEventHandler);
}

inline void publishPerformance(const PerformanceSnapshot&) {}
inline void releaseGraphics() {}
inline void shutdown() {}

#endif

} // namespace app::tooling
