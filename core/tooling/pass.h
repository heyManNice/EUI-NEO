#pragma once

#include "core/render/render_backend.h"
#include "core/tooling/model.h"

// What a tool gets while the page render pass is running.
//
// A tool that draws on top of the page has to draw *inside* the pass: the renderer is
// what gives the output its clip, and the frame the window blits is the one this pass
// wrote. Drawing outside it means the overlay is missing from the next cache blit, which
// is why the pass hands the backend over instead of the tool opening a pass of its own.
//
// The core keeps the Resolve half of this: it resolves the geometry of the element a tool
// marked (`ElementBox`) and hands it over with the backend. What to draw with it — which
// boxes, which colours, in which order — is the tool's, so nothing here mentions a
// palette, a band or a preview.

namespace core::dsl::runtime {

struct RenderPassContext {
    // The backend the page is being drawn with, at the point in the pass where page
    // content is finished.
    core::render::RenderBackend* backend = nullptr;
    int windowWidth = 0;
    int windowHeight = 0;
    float dpiScale = 1.0f;

    // The geometry of the element the tool marked, resolved for this pass. It is the same
    // value `Runtime::hoveredBox` returns, taken at the moment the pass draws it, so the
    // overlay cannot drift away from the element by a frame. `active` is false when
    // nothing is marked.
    ElementBox hover;

    // Clips the next draws to a rectangle in framebuffer pixels, the space page content is
    // drawn in; `clipToNothing` turns clipping off again, which is what the pass does for
    // an element nothing clips.
    void clipTo(const Rect& rect) const {
        if (backend != nullptr) {
            backend->setScissor(true, rect, windowHeight);
        }
    }

    void clipToNothing() const {
        if (backend != nullptr) {
            backend->setScissor(false, {}, windowHeight);
        }
    }
};

} // namespace core::dsl::runtime
