#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/dsl.h"
#include "core/render/primitive.h"
#include "core/tooling/pass.h"

// The box model preview a tool draws over the element it points at.
//
// Nothing here is core: the core resolves the geometry of the marked element and hands it
// over inside the page render pass (core/tooling/pass.h), and this file decides what that
// geometry looks like as pixels — one translucent fill per box model region, the hues the
// browsers use for them, and how the regions are cut so no colour blends twice. Moving
// this out of the core is what keeps a palette and a preview drawing out of the runtime.

namespace modules::devtools {

// One fill per box model region. The values are fixed instead of themed, because the
// overlay has to read the same over any page.
struct BoxPreviewPalette {
    core::Color margin;
    core::Color border;
    core::Color padding;
    core::Color content;
};

// The hues match the browsers', a little lower in alpha because a wash reads heavier over
// the dark pages EUI ships.
inline constexpr BoxPreviewPalette kHoverBoxPreviewPalette{
    {0.96f, 0.70f, 0.42f, 0.55f},   // margin
    {1.00f, 0.90f, 0.60f, 0.55f},   // border
    {0.58f, 0.77f, 0.49f, 0.45f},   // padding
    {0.44f, 0.66f, 0.86f, 0.50f}    // content
};

// The area between two nested boxes as up to four rectangles. Filling the inner box on top
// of the outer one would blend the two translucent colours into a third, so the band left
// around the inner box is filled instead. The bands share their edges and the side bands
// never run under the top and bottom ones.
struct PreviewBand {
    core::Rect rects[4];
    int count = 0;
};

PreviewBand previewBand(const core::Rect& outer, const core::Rect& inner);

// Draws the wash of one resolved box, inside the pass's clip. The caller owns `primitive`
// and is what releases it with the device.
void drawBoxPreview(const core::dsl::runtime::ElementBox& box,
                    const core::dsl::runtime::RenderPassContext& pass,
                    const BoxPreviewPalette& palette,
                    core::RoundedRectPrimitive& primitive);

} // namespace modules::devtools

#endif
