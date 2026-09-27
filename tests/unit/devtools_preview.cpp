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
#if defined(EUI_DEBUG_BUILD)

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

    return 0;
}

#else

int main() {
    // The whole preview belongs to the panel, and the panel is a Debug build only.
    return 0;
}

#endif
