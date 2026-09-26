#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

// Element inspection is a debug tool: the geometry it reports is what the renderer
// draws, so it is tested without a renderer.
#if defined(EUI_DEBUG_BUILD)

namespace {

constexpr float kFrameSeconds = 1.0f / 60.0f;

// A page with a clipped, scrollable list holding a padded card. The interesting
// cases are all here: an explicit position, an ancestor clip and a scroll
// transform. Absolute positions need a stack, like the panel composes its own
// overlays.
void composePage(core::dsl::Runtime& runtime) {
    runtime.compose("page", 400.0f, 300.0f, [](core::dsl::Ui& ui, const core::dsl::Screen&) {
        ui.stack("root")
            .size(400.0f, 300.0f)
            .content([&] {
                ui.stack("list")
                    .position(20.0f, 30.0f)
                    .size(200.0f, 100.0f)
                    .clip()
                    .scrollState("list", 0.0f, 120.0f, 20.0f)
                    .content([&] {
                        ui.column("content")
                            .width(200.0f)
                            .height(core::SizeValue::wrapContent())
                            .scrollContentFrom("list")
                            .content([&] {
                                ui.column("card")
                                    .width(160.0f)
                                    .height(60.0f)
                                    .padding(8.0f)
                                    .build();
                            })
                            .build();
                    })
                    .build();
            })
            .build();
    });
}

void settle(core::dsl::Runtime& runtime) {
    runtime.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
}

} // namespace

int main() {
    core::dsl::Runtime runtime;
    composePage(runtime);
    settle(runtime);

    // Nothing is inspected until a tool asks for an element.
    assert(!runtime.debugInspection(1.0f).active);
    assert(runtime.inspectedElement().empty());

    core::dsl::Runtime& pageRuntime = runtime;
    pageRuntime.setInspectedElement("page.card");
    assert(pageRuntime.inspectedElement() == "page.card");
    settle(pageRuntime);

    const core::dsl::runtime::DebugInspection card = pageRuntime.debugInspection(1.0f);
    assert(card.active);
    // Layout frame: the card sits inside the list at its own layout position.
    assert(card.frame.x == 20.0f);
    assert(card.frame.y == 30.0f);
    assert(card.frame.width == 160.0f);
    assert(card.frame.height == 60.0f);
    assert(card.padding.left == 8.0f);
    assert(card.padding.top == 8.0f);
    // The clip comes from the scrolling ancestor, in pixels.
    assert(card.hasScissor);
    assert(card.scissor.x == 20.0f);
    assert(card.scissor.y == 30.0f);
    assert(card.scissor.width == 200.0f);
    assert(card.scissor.height == 100.0f);

    // Inspecting does not touch the page: the tree and the structure revision stay.
    const std::uint64_t revision = pageRuntime.elementStructureRevision();
    pageRuntime.setInspectedElement("page.card");
    settle(pageRuntime);
    assert(pageRuntime.elementStructureRevision() == revision);

    // A scroll offset moves the element through its ancestor's transform, which is
    // the property that makes the overlay follow scrolling containers. The wheel is
    // the public way in: a pointer over the list plus a scroll event.
    {
        const core::window::Handle window = nullptr;   // a runtime without its own window
        core::queuePointerMotion(window, 40.0, 80.0, {}, {});
        core::queueScrollInput(window, 0.0, -1.0);
        pageRuntime.update(window, kFrameSeconds, 1.0f, 1.0f);
        for (int frame = 0; frame < 10; ++frame) {
            settle(pageRuntime);
        }
    }
    const core::dsl::runtime::DebugInspection scrolled = pageRuntime.debugInspection(1.0f);
    assert(scrolled.active);
    assert(scrolled.frame.y == 30.0f);                              // layout is unchanged
    const float scrollShift = scrolled.transform.matrix.ty - card.transform.matrix.ty;
    // One wheel tick moves the content by at most velocity / friction pixels, and
    // the inertia decays over the frames above; half of it proves the scroll landed.
    assert(scrollShift < -10.0f);
    assert(scrolled.scissor.height == 100.0f);                      // the clip stays at the container

    // A hover preview is a second mark: both can be active at once and neither
    // disturbs the other.
    pageRuntime.setHoveredElement("page.list");
    settle(pageRuntime);
    const core::dsl::runtime::DebugInspection hovered = pageRuntime.debugHoverInspection(1.0f);
    assert(hovered.active);
    assert(hovered.frame.x == 20.0f);
    assert(hovered.frame.width == 200.0f);
    assert(pageRuntime.hoveredElement() == "page.list");
    assert(pageRuntime.inspectedElement() == "page.card");
    assert(pageRuntime.debugInspection(1.0f).active);
    pageRuntime.setHoveredElement("");
    settle(pageRuntime);
    assert(!pageRuntime.debugHoverInspection(1.0f).active);
    assert(pageRuntime.debugInspection(1.0f).active);   // the selection survives the preview

    // A page that no longer contains the element simply stops drawing the overlay.
    pageRuntime.setInspectedElement("page.missing");
    settle(pageRuntime);
    assert(!pageRuntime.debugInspection(1.0f).active);

    // Recomposing rebuilds every element; the inspection follows the new tree
    // instead of remembering pointers into the old one.
    pageRuntime.setInspectedElement("page.card");
    composePage(pageRuntime);
    settle(pageRuntime);
    const core::dsl::runtime::DebugInspection recomposed = pageRuntime.debugInspection(1.0f);
    assert(recomposed.active);
    assert(recomposed.frame.width == 160.0f);

    pageRuntime.setInspectedElement("");
    assert(pageRuntime.inspectedElement().empty());
    settle(pageRuntime);
    assert(!pageRuntime.debugInspection(1.0f).active);

    pageRuntime.shutdown(false);
    return 0;
}

#else

int main() {
    core::dsl::Runtime runtime;
    runtime.shutdown(false);
    return 0;
}

#endif
