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
                                ui.rect("padded")
                                    .width(120.0f)
                                    .height(40.0f)
                                    .margin(6.0f)
                                    .padding(10.0f)
                                    .border(2.0f, core::Color{0.6f, 0.6f, 0.6f, 1.0f})
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

    core::dsl::Runtime& pageRuntime = runtime;

    // Nothing is previewed until a tool asks for an element.
    assert(!pageRuntime.debugHoverInspection(1.0f).active);
    assert(pageRuntime.hoveredElement().empty());

    pageRuntime.setHoveredElement("page.card");
    assert(pageRuntime.hoveredElement() == "page.card");
    settle(pageRuntime);

    const core::dsl::runtime::DebugInspection card = pageRuntime.debugHoverInspection(1.0f);
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

    // Margin and border come along as well, so the renderer can fill the whole box
    // model without asking the layout for it a second time.
    assert(card.margin.left == 0.0f && card.margin.top == 0.0f);
    assert(card.borderWidth == 0.0f);

    pageRuntime.setHoveredElement("page.padded");
    settle(pageRuntime);
    const core::dsl::runtime::DebugInspection padded = pageRuntime.debugHoverInspection(1.0f);
    assert(padded.active);
    assert(padded.margin.left == 6.0f && padded.margin.top == 6.0f);
    assert(padded.padding.left == 10.0f && padded.padding.bottom == 10.0f);
    assert(padded.borderWidth == 2.0f);
    pageRuntime.setHoveredElement("page.card");
    settle(pageRuntime);

    // Previewing does not touch the page: the tree and the structure revision stay.
    const std::uint64_t revision = pageRuntime.elementStructureRevision();
    pageRuntime.setHoveredElement("page.card");
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
    const core::dsl::runtime::DebugInspection scrolled = pageRuntime.debugHoverInspection(1.0f);
    assert(scrolled.active);
    assert(scrolled.frame.y == 30.0f);                              // layout is unchanged
    const float scrollShift = scrolled.transform.matrix.ty - card.transform.matrix.ty;
    // One wheel tick moves the content by at most velocity / friction pixels, and
    // the inertia decays over the frames above; half of it proves the scroll landed.
    assert(scrollShift < -10.0f);
    assert(scrolled.scissor.height == 100.0f);                      // the clip stays at the container

    // Previewing another element replaces the mark instead of stacking overlays, so
    // only one element is ever previewed at a time.
    pageRuntime.setHoveredElement("page.list");
    settle(pageRuntime);
    const core::dsl::runtime::DebugInspection hovered = pageRuntime.debugHoverInspection(1.0f);
    assert(hovered.active);
    assert(hovered.frame.x == 20.0f);
    assert(hovered.frame.width == 200.0f);
    assert(pageRuntime.hoveredElement() == "page.list");

    // Leaving the row takes the preview back.
    pageRuntime.setHoveredElement("");
    assert(pageRuntime.hoveredElement().empty());
    settle(pageRuntime);
    assert(!pageRuntime.debugHoverInspection(1.0f).active);

    // A page that no longer contains the element simply stops drawing the overlay.
    pageRuntime.setHoveredElement("page.missing");
    settle(pageRuntime);
    assert(!pageRuntime.debugHoverInspection(1.0f).active);

    // Recomposing rebuilds every element; the preview follows the new tree instead
    // of remembering pointers into the old one.
    pageRuntime.setHoveredElement("page.card");
    composePage(pageRuntime);
    settle(pageRuntime);
    const core::dsl::runtime::DebugInspection recomposed = pageRuntime.debugHoverInspection(1.0f);
    assert(recomposed.active);
    assert(recomposed.frame.width == 160.0f);

    // The band between two boxes is split into rectangles that do not overlap, so
    // translucent region colours cannot blend into a third colour. The side bands
    // only cover the height the top and bottom bands leave.
    {
        const core::Rect outer{0.0f, 0.0f, 10.0f, 10.0f};
        const core::Rect inner{2.0f, 3.0f, 6.0f, 4.0f};
        const core::dsl::runtime::InspectionBand band =
            core::dsl::runtime::inspectionBand(outer, inner);
        assert(band.count == 4);
        assert(band.rects[0].x == 0.0f && band.rects[0].y == 0.0f);
        assert(band.rects[0].width == 10.0f && band.rects[0].height == 3.0f);
        assert(band.rects[1].y == 7.0f && band.rects[1].height == 3.0f);
        assert(band.rects[2].x == 0.0f && band.rects[2].y == 3.0f);
        assert(band.rects[2].width == 2.0f && band.rects[2].height == 4.0f);
        assert(band.rects[3].x == 8.0f && band.rects[3].width == 2.0f);
        // Boxes that sit exactly on top of each other have no band at all.
        assert(core::dsl::runtime::inspectionBand(outer, outer).count == 0);
        // An inner box that reaches past the outer one leaves nothing to fill either.
        assert(core::dsl::runtime::inspectionBand(outer, {-2.0f, -2.0f, 14.0f, 14.0f}).count == 0);
        // An inner box that sticks out on one side produces no band on that side.
        const core::dsl::runtime::InspectionBand offset =
            core::dsl::runtime::inspectionBand(outer, {-3.0f, 2.0f, 6.0f, 6.0f});
        assert(offset.count == 3);
        assert(offset.rects[2].x == 3.0f && offset.rects[2].width == 7.0f);
    }

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
