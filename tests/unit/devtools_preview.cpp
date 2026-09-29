#include "modules/devtools/devtools_preview.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// The box model preview the panel draws over the element it points at belongs to the
// module, not to the framework: the core resolves the geometry and the pass hands it over
// (core/tooling/pass.h), and this is the module's half. What it has to get right on its own
// is the band arithmetic: translucent region colours must not overlap, or two of them blend
// into a colour the user cannot read as either region.
#if defined(EUI_TOOLING)

int main() {
    // The band between two boxes is split into rectangles that do not overlap, so
    // translucent region colours cannot blend into a third colour. The side bands
    // only cover the height the top and bottom bands leave.
    {
        const core::Rect outer{0.0f, 0.0f, 10.0f, 10.0f};
        const core::Rect inner{2.0f, 3.0f, 6.0f, 4.0f};
        const modules::devtools::PreviewBand band = modules::devtools::previewBand(outer, inner);
        assert(band.count == 4);
        assert(band.rects[0].x == 0.0f && band.rects[0].y == 0.0f);
        assert(band.rects[0].width == 10.0f && band.rects[0].height == 3.0f);
        assert(band.rects[1].y == 7.0f && band.rects[1].height == 3.0f);
        assert(band.rects[2].x == 0.0f && band.rects[2].y == 3.0f);
        assert(band.rects[2].width == 2.0f && band.rects[2].height == 4.0f);
        assert(band.rects[3].x == 8.0f && band.rects[3].width == 2.0f);
        // Boxes that sit exactly on top of each other have no band at all.
        assert(modules::devtools::previewBand(outer, outer).count == 0);
        // An inner box that reaches past the outer one leaves nothing to fill either.
        assert(modules::devtools::previewBand(outer, {-2.0f, -2.0f, 14.0f, 14.0f}).count == 0);
        // An inner box that sticks out on one side produces no band on that side.
        const modules::devtools::PreviewBand offset =
            modules::devtools::previewBand(outer, {-3.0f, 2.0f, 6.0f, 6.0f});
        assert(offset.count == 3);
        assert(offset.rects[2].x == 3.0f && offset.rects[2].width == 7.0f);
    }

    // The bounds option rings a frame with one pixel: the ring is the band between the frame
    // and the frame a pixel inside it, so the four sides come out disjoint like the box model
    // bands do.
    {
        const modules::devtools::PreviewBand ring = modules::devtools::outlineBand({0.0f, 0.0f, 10.0f, 6.0f});
        assert(ring.count == 4);
        assert(ring.rects[0].x == 0.0f && ring.rects[0].y == 0.0f && ring.rects[0].width == 10.0f &&
               ring.rects[0].height == 1.0f);
        assert(ring.rects[1].y == 5.0f && ring.rects[1].height == 1.0f);
        assert(ring.rects[2].x == 0.0f && ring.rects[2].y == 1.0f && ring.rects[2].width == 1.0f &&
               ring.rects[2].height == 4.0f);
        assert(ring.rects[3].x == 9.0f && ring.rects[3].width == 1.0f && ring.rects[3].height == 4.0f);

        // A frame with no room for a ring has to come back as bands that still have a size,
        // never as a rectangle turned inside out.
        for (const core::Rect& thin : {core::Rect{0.0f, 0.0f, 1.0f, 4.0f}, core::Rect{2.0f, 3.0f, 0.0f, 0.0f}}) {
            const modules::devtools::PreviewBand thinRing = modules::devtools::outlineBand(thin);
            for (int index = 0; index < thinRing.count; ++index) {
                assert(thinRing.rects[index].width > 0.0f && thinRing.rects[index].height > 0.0f);
                assert(thinRing.rects[index].x >= thin.x && thinRing.rects[index].y >= thin.y);
            }
        }
    }

    // A level gets one ring colour: two elements at the same depth always read the same, and
    // neighbouring levels are far enough apart to be told apart. Every level carries the same
    // weight, so a deep element is no harder to see than its parent.
    {
        const auto sameColor = [](const core::Color& left, const core::Color& right) {
            return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
        };
        assert(sameColor(modules::devtools::elementBoundsColor(0), modules::devtools::elementBoundsColor(0)));
        assert(!sameColor(modules::devtools::elementBoundsColor(1), modules::devtools::elementBoundsColor(0)));
        // A depth that cannot happen reads as the top level rather than reading out of bounds.
        assert(sameColor(modules::devtools::elementBoundsColor(-1), modules::devtools::elementBoundsColor(0)));
        constexpr std::size_t kLevels =
            sizeof(modules::devtools::kElementBoundsColors) / sizeof(modules::devtools::kElementBoundsColors[0]);
        for (std::size_t level = 0; level < kLevels; ++level) {
            const core::Color color = modules::devtools::elementBoundsColor(static_cast<int>(level));
            assert(color.a == modules::devtools::kElementBoundsColors[0].a);
            assert(color.a > 0.5f);
        }
        // Deeper than the table wraps instead of running off it.
        assert(sameColor(modules::devtools::elementBoundsColor(static_cast<int>(kLevels)),
                         modules::devtools::elementBoundsColor(0)));
    }

    {
        const std::string text = core::dsl::utf8(0xF5FD) + "  100 × 100";
        const float w1 = core::TextPrimitive::measureTextWidth("100 × 100", {}, 11.0f, 500);
        const float w2 = core::TextPrimitive::measureTextWidth(text, {}, 11.0f, 500);
        assert(w2 > w1);
    }

    return 0;
}

#else

int main() {
    // The whole preview belongs to the panel, and the panel is a Debug build only.
    return 0;
}

#endif
