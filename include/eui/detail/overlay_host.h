#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/app/performance_snapshot.h"
#include "core/dsl.h"
#include "core/input/input_types.h"
#include "core/render/render_types.h"
#include "core/window/window_types.h"

#include <functional>
#include <string>
#include <vector>

namespace app::detail {

// Window parameters for the window an overlay can detach into. The framework
// opens and closes that window, because it owns the window manager.
struct DetachedWindowOptions {
    std::string title = "Overlay";
    core::Color clearColor{0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
};

// An overlay host draws above the app page, may reserve part of the window for
// itself, and may consume input before the page Runtime sees it. The framework
// only owns this interface; a Debug module implements it and registers it here,
// so core code never references a specific overlay.
//
// Frame order between host and page Runtime:
//
//   filterInput -> page update -> update -> updateCursor -> render
//
// render() runs inside the page render pass, so the overlay becomes part of the
// cached frame the window blits. A host stays registered until it is replaced or
// the app shuts down.
class OverlayHost {
public:
    virtual ~OverlayHost() = default;

    // Consumes an app hotkey. Returning true keeps the key away from
    // DslAppConfig::onKeyEvent.
    virtual bool handleHotkey(const core::KeyEvent& key) = 0;

    // Window area left to the app page, in framebuffer pixels.
    virtual core::Rect contentBounds() const = 0;

    // Receives pointer and scroll state that the app page must not see.
    virtual void filterInput(std::vector<core::PointerEvent>& pointerEvents,
                             core::ScrollEvent& scrollEvent) = 0;

    // Runs once per frame after the page Runtime update and returns true when
    // the overlay repainted, asking the window to render again.
    virtual bool update(int framebufferWidth, int framebufferHeight, float dpiScale) = 0;

    virtual void updateCursor(core::window::Handle window) = 0;

    // Draws the overlay on top of the page content. It is called while the page
    // render cache is being filled: `dirtyRect` is null on a full paint and the
    // repainted region otherwise, both in framebuffer pixels.
    virtual void render(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect) = 0;

    // Diagnostics snapshot published by the app loop; overlays without a
    // performance view ignore it.
    virtual void setPerformanceSnapshot(const PerformanceSnapshot& snapshot) {}

    virtual void releaseGraphicsResources() = 0;

    virtual void shutdown() = 0;

    // Detached window support. The overlay describes the window it wants, gets
    // the open and close hooks from the framework, composes that window through
    // composeDetached() and hears about its closing through detachedWindowClosed().
    virtual void describeDetachedWindow(DetachedWindowOptions& options) const = 0;
    virtual void setDetachedWindowOpener(std::function<void()> opener) = 0;
    virtual void setDetachedWindowCloser(std::function<void()> closer) = 0;
    virtual void composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen) = 0;
    virtual void detachedWindowClosed() = 0;
};

inline OverlayHost*& overlayHostStorage() {
    static OverlayHost* host = nullptr;
    return host;
}

inline OverlayHost* overlayHost() {
    return overlayHostStorage();
}

inline void setOverlayHost(OverlayHost* host) {
    overlayHostStorage() = host;
}

} // namespace app::detail

#endif
