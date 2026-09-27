#pragma once

#include "core/app/performance_snapshot.h"
#include "core/dsl_runtime.h"
#include "core/tooling/config.h"

#include <functional>
#include <string>

// The whole framework side of "a tool is attached to this app".
//
// Two sides meet here, and they own different things: the **app layer owns the frame and
// the windows**, the **tool owns the page data**. So this header is only the few things
// the app layer can do that the tool cannot do for itself — plus the few the tool tells the
// app about. Everything else the tool wires itself, straight onto the page runtime
// (`Runtime::setInputFilter`, `setPassRenderer`, `setOverlayRenderer`, `elementTree`,
// `elementValues`, ...), which is why there is no interface to inherit and no bridge to
// keep the two sides in step: the data path is one hop.
//
// There is one tool per app; a second registration replaces the first. Every slot is
// optional: a tool that only reads the page fills in `attach` and nothing else, which is
// what a headless client looks like.

namespace app::detail {

// The window a tool wants for itself. The app layer owns the window manager, so a tool
// describes the window instead of opening one; the app composes the tool into it and
// reports when it closed.
struct OverlayWindowRequest {
    std::string title = "Overlay";
    core::Color clearColor{0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    std::function<void(core::dsl::Ui&, const core::dsl::Screen&)> compose;
    std::function<void()> closed;
};

// What the app layer can do for a tool: this side is filled in by the app and called by
// the tool. Opening twice reuses the window that is already open.
struct OverlayWindows {
    std::function<void(const OverlayWindowRequest&)> open;
    std::function<void()> close;
};

// What the app layer asks of a tool: this side is filled in by the tool and called by the
// app. `attach` is where a tool wires its own hooks, `update` is its frame, and the rest
// are the moments only the app layer can name.
struct OverlayHooks {
    // Called once, with the page runtime and the window services. The tool wires its own
    // hooks here and keeps what it wants to read: it owns its state, not the framework.
    std::function<void(core::dsl::Runtime&, const OverlayWindows&)> attach;
    // Called before the runtime goes away, so the tool can drop what points into it.
    std::function<void()> detach;
    // The window area left to the page, in framebuffer pixels. A tool that docked itself
    // takes the rest; a tool that did not says nothing and the page gets all of it.
    std::function<core::Rect(int windowWidth, int windowHeight, float dpiScale)> contentBounds;
    // True while the tool claims the key, which keeps it away from the app's own handler.
    std::function<bool(const core::KeyEvent&)> handleKey;
    // One frame of the tool, after the page was updated and before it is rendered.
    // Returns true when the tool repainted itself and the window has to draw again.
    std::function<bool(int windowWidth, int windowHeight, float dpiScale, float deltaSeconds)> update;
    // Draws the tool's own UI into the page render cache, so every blit carries a complete
    // frame. `dirtyRect` is null on a full paint.
    std::function<void(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect)> render;
    // Diagnostics the app loop measured this frame.
    std::function<void(const PerformanceSnapshot&)> performance;
    // The window is losing its graphics device: the tool drops what the device owns.
    std::function<void()> releaseGraphics;
};

inline OverlayHooks*& overlayHooksStorage() {
    static OverlayHooks* hooks = nullptr;
    return hooks;
}

// The tool attached to this app, null when none is.
inline OverlayHooks* overlayHooks() {
    return overlayHooksStorage();
}

inline void setOverlayHooks(OverlayHooks* hooks) {
    overlayHooksStorage() = hooks;
}

// The five things the app loop asks of an attached tool. Each one is a no-op — or answers
// "the page gets the whole window" — when no tool is attached, so the loop itself carries
// no branch and no tooling state.
inline core::Rect overlayContentBounds(int windowWidth, int windowHeight, float dpiScale) {
    OverlayHooks* hooks = overlayHooks();
    if (hooks != nullptr && hooks->contentBounds) {
        return hooks->contentBounds(windowWidth, windowHeight, dpiScale);
    }
    return {0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
}

inline bool overlayHandleKey(const core::KeyEvent& key) {
    OverlayHooks* hooks = overlayHooks();
    return hooks != nullptr && hooks->handleKey && hooks->handleKey(key);
}

inline bool overlayUpdate(int windowWidth, int windowHeight, float dpiScale, float deltaSeconds) {
    OverlayHooks* hooks = overlayHooks();
    return hooks != nullptr && hooks->update && hooks->update(windowWidth, windowHeight, dpiScale, deltaSeconds);
}

inline void overlayRender(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect) {
    OverlayHooks* hooks = overlayHooks();
    if (hooks != nullptr && hooks->render) {
        hooks->render(windowWidth, windowHeight, dpiScale, dirtyRect);
    }
}

inline void overlayPerformance(const PerformanceSnapshot& snapshot) {
    OverlayHooks* hooks = overlayHooks();
    if (hooks != nullptr && hooks->performance) {
        hooks->performance(snapshot);
    }
}

inline void overlayReleaseGraphics() {
    OverlayHooks* hooks = overlayHooks();
    if (hooks != nullptr && hooks->releaseGraphics) {
        hooks->releaseGraphics();
    }
}

inline void overlayDetach() {
    OverlayHooks* hooks = overlayHooks();
    if (hooks != nullptr && hooks->detach) {
        hooks->detach();
    }
}

} // namespace app::detail
