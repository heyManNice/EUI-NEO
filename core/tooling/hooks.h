#pragma once

#include "core/tooling/config.h"
#include "core/tooling/state.h"

#include <string>
#include <vector>

namespace core::dsl {

#if EUI_TOOLING_ENABLED
// The tool state of a runtime, created the first time a tool talks to the runtime.
// Everything the runtime does for tools goes through it, so a runtime nobody inspects
// pays one null pointer and nothing else.
inline runtime::ToolingState& Runtime::ensureTooling() {
    if (!tooling_) {
        tooling_ = std::make_unique<runtime::ToolingState>();
    }
    return *tooling_;
}

// The moment between composing and laying out, where a tool that replaces values on the
// page puts them back. The runtime owns the moment and the tool owns what happens in it,
// so nothing here knows what a tool writes or how it remembers it.
inline void Runtime::setAfterCompose(std::function<void()> hook) {
    ensureTooling().afterCompose = std::move(hook);
}

// The four ways a tool attaches itself or feeds a runtime it drives. They live here
// because they are the first thing that allocates the tool state: until one of them is
// called, a runtime carries the pointer and nothing behind it.
inline void Runtime::setInputFilter(std::function<void(std::vector<PointerEvent>&, ScrollEvent&)> filter) {
    ensureTooling().inputFilter = std::move(filter);
}

inline void Runtime::setOverlayRenderer(std::function<void(int, int, float, const Rect*)> renderer) {
    ensureTooling().overlayRenderer = std::move(renderer);
}

inline void Runtime::setPassRenderer(std::function<void(const runtime::RenderPassContext&)> renderer) {
    ensureTooling().passRenderer = std::move(renderer);
}

inline void Runtime::pushPointerEvent(const PointerEvent& event) {
    ensureTooling().hostPointerEvents.push_back(event);
}

inline void Runtime::pushScrollEvent(const ScrollEvent& event) {
    ensureTooling().hostScrollEvent = event;
}
#endif

namespace tooling {

// The hooks the runtime calls. This header is the only place in the framework that
// branches on whether tooling is part of the build: with it, each hook forwards to the
// real implementation, without it each hook is empty, so a build without tools carries
// no tooling code at all. Every call site in the runtime is one unconditional line and
// never asks which configuration it is in. See docs/工具协议.md.

#if EUI_TOOLING_ENABLED

// A compose builds every element again, so anything that remembered element pointers
// (a cached inspection path) has to forget them.
inline void beforeCompose(Runtime& runtime) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr) {
        return;
    }
    ++state->composeGeneration;
    state->hovered.path.clear();
    state->hovered.pathId.clear();
}

// The values a tool replaced go back on the fresh tree, before it is laid out.
inline void afterCompose(Runtime& runtime) {
    runtime::ToolingState* state = runtime.tooling();
    if (state != nullptr && state->afterCompose) {
        state->afterCompose();
    }
}

// Input is off for this frame, so the events a host pushed for it go with it.
inline void resetHostInput(Runtime& runtime) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr) {
        return;
    }
    state->hostPointerEvents.clear();
    state->hostScrollEvent = {};
}

// A runtime driven by a host instead of a window has no input queue of its own: the
// events its tool pushed are merged into the frame's input.
inline void mergeHostInput(Runtime& runtime, std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr) {
        return;
    }
    pointerEvents.insert(pointerEvents.end(), state->hostPointerEvents.begin(), state->hostPointerEvents.end());
    state->hostPointerEvents.clear();
    if (state->hostScrollEvent.active()) {
        scrollEvent = state->hostScrollEvent;
    }
    state->hostScrollEvent = {};
}

// The tool takes the events it wants before the page sees them.
inline void filterInput(Runtime& runtime, std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr || !state->inputFilter) {
        return;
    }
    state->inputFilter(pointerEvents, scrollEvent);
}

// The tool draws on top of the page, inside the page render pass, so its output becomes
// part of the cached frame the window blits. What the pass has to hand over is the
// renderer's own material: the tree the geometry is resolved from, the backend it draws
// with, and the state the tool left behind. What to draw with that is the tool's, so this
// hook knows no palette and no preview.
inline void drawPassOverlay(Ui& ui,
                            runtime::InstanceStore& instances,
                            runtime::ToolingState* state,
                            core::render::RenderBackend& renderBackend,
                            int windowWidth,
                            int windowHeight,
                            float dpiScale) {
    if (state == nullptr || !state->passRenderer) {
        return;
    }
    runtime::RenderPassContext pass;
    pass.backend = &renderBackend;
    pass.windowWidth = windowWidth;
    pass.windowHeight = windowHeight;
    pass.dpiScale = dpiScale;
    pass.hover = runtime::computeElementBox(ui, instances, state->hovered, state->composeGeneration, dpiScale);
    state->passRenderer(pass);
}

// The tool draws its own overlay (its own runtime) on top of the page, inside the page
// render pass, so its output becomes part of the cached frame the window blits.
inline void drawOverlay(Runtime& runtime, int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr || !state->overlayRenderer) {
        return;
    }
    state->overlayRenderer(windowWidth, windowHeight, dpiScale, dirtyRect);
}

// The runtime is dropping its graphics resources, so the tool's overlay primitive goes
// with them instead of outliving the device.
inline void releaseGraphics(Runtime& runtime) {
    // The tool owns whatever it draws with and hooks this through its own host, so the
    // seam has nothing to release here.
    static_cast<void>(runtime);
}

// The tool is going away with the runtime.
inline void release(Runtime& runtime) {
    runtime::ToolingState* state = runtime.tooling();
    if (state == nullptr) {
        return;
    }
    state->inputFilter = {};
    state->overlayRenderer = {};
    state->passRenderer = {};
    state->hostPointerEvents.clear();
    state->hostScrollEvent = {};
}

#else

inline void beforeCompose(Runtime&) {}
inline void afterCompose(Runtime&) {}
inline void resetHostInput(Runtime&) {}
inline void mergeHostInput(Runtime&, std::vector<PointerEvent>&, ScrollEvent&) {}
inline void filterInput(Runtime&, std::vector<PointerEvent>&, ScrollEvent&) {}
inline void drawPassOverlay(Ui&, runtime::InstanceStore&, runtime::ToolingState*, core::render::RenderBackend&, int, int, float) {}
inline void drawOverlay(Runtime&, int, int, float, const Rect*) {}
inline void releaseGraphics(Runtime&) {}
inline void release(Runtime&) {}

#endif

} // namespace tooling

#if !EUI_TOOLING_ENABLED
// The seam's own API is declared in every configuration, so a tool is written once and
// compiled against one set of headers. Without tooling every entry point is inert:
// there is no state to read, nothing to mark, nothing to replace.
inline const std::string& Runtime::hoveredElement() const {
    static const std::string empty;
    return empty;
}

inline void Runtime::setHoveredElement(const std::string&) {}

inline void Runtime::setAfterCompose(std::function<void()>) {}

inline runtime::ElementBox Runtime::hoveredBox(float) {
    return {};
}

inline std::string Runtime::elementIdAt(double, double, float) const {
    return {};
}
#endif

} // namespace core::dsl
