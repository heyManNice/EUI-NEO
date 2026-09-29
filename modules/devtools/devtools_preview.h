#pragma once

#if defined(EUI_TOOLING)

#include "core/dsl.h"
#include "core/render/primitive.h"
#include "core/tooling/pass.h"

#include <vector>

// The box model preview the panel draws over the element it points at.
//
// The core resolves the geometry of the marked element and hands it over inside the page
// render pass (core/tooling/pass.h). What that geometry looks like as pixels — one
// translucent fill per box model region, the hues the browsers use for them, and how the
// regions are cut so no colour blends twice — is a display decision and lives here.

namespace core {
class TextPrimitive;
}

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

// The colours the bounds option rings elements with, one per depth and cycled: a nested
// element is told apart from its parent by hue, and two elements at the same depth always
// carry the same colour. Picked rather than drawn at random, because a colour that changed
// while the page redraws would read as flicker rather than as a level.
inline constexpr core::Color kElementBoundsColors[] = {
    {0.42f, 0.86f, 0.98f, 0.80f},   // cyan
    {0.98f, 0.72f, 0.36f, 0.80f},   // amber
    {0.72f, 0.60f, 0.98f, 0.80f},   // violet
    {0.48f, 0.90f, 0.60f, 0.80f},   // green
    {0.98f, 0.55f, 0.72f, 0.80f},   // pink
    {0.62f, 0.82f, 0.36f, 0.80f}    // olive
};

core::Color elementBoundsColor(int depth);

// One layout box and the depth the tree gave it.
struct ElementBounds {
    core::Rect frame;
    int depth = 0;
};

// The one pixel ring just inside a frame. It is the same band arithmetic the box preview uses,
// which is what keeps a frame too small to hold two rings from turning inside out.
PreviewBand outlineBand(const core::Rect& frame);

// Rings every frame the element tree published, in the pass's pixel space, in the colour its
// depth picks. The frames are the layout boxes, so what lands on screen is where the tree says
// the rows are.
void drawElementBounds(const std::vector<ElementBounds>& bounds,
                       const core::dsl::runtime::RenderPassContext& pass,
                       core::RoundedRectPrimitive& primitive);


// Draws the wash of one resolved box, inside the pass's clip, and floats a coordinate badge
// at the element's bottom-left corner when textPrimitive is provided. The caller owns primitive
// and textPrimitive and is what releases them with the device.
void drawBoxPreview(const core::dsl::runtime::ElementBox& box,
                    const core::dsl::runtime::RenderPassContext& pass,
                    const BoxPreviewPalette& palette,
                    core::RoundedRectPrimitive& primitive,
                    core::TextPrimitive* textPrimitive = nullptr,
                    const core::Vec2* relativeOffset = nullptr);

} // namespace modules::devtools

#endif
