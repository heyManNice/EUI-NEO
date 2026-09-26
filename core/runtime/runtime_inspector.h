#pragma once

#include "core/dsl.h"
#include "core/runtime/runtime_geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core::dsl {
class Ui;
}

namespace core::dsl::runtime {

// Debug tools copy the element tree out of a Runtime instead of walking it while
// they draw: a snapshot owns its strings, carries the depth of every node and
// stays valid across composes, so a panel can hold it between frames.
struct ElementTreeNode {
    std::string id;
    ElementKind kind = ElementKind::Stack;
    std::string text;
    int depth = 0;
    int zIndex = 0;
    bool clip = false;
    bool interactive = false;
    bool disabled = false;
    Rect frame;
};

struct ElementTreeSnapshot {
    std::uint64_t revision = 0;
    bool truncated = false;
    std::vector<ElementTreeNode> nodes;
};

// A page can hold tens of thousands of elements and the tree view only ever shows
// a window of them, so a snapshot stops copying at this many nodes.
inline constexpr std::size_t kElementTreeMaximumNodes = 20000;

// The tree view shows a readable prefix of a text element, never the whole string.
inline constexpr std::size_t kElementTreeTextLimit = 64;

// Copies at most `limit` bytes of text without splitting a UTF-8 sequence, so a
// truncated label never turns into invalid bytes.
inline std::string truncateElementText(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

// Everything the renderer needs to draw the inspection overlay for one element.
// The runtime derives it from the element path with the same transform and clip
// rules the element itself is drawn with, so the overlay cannot drift away from
// the element it describes.
struct DebugInspection {
    bool active = false;
    RenderTransform transform;
    LayoutRect frame;               // the box itself: background and border live here
    EdgeInsets padding;             // inset from the box edge to the content
    EdgeInsets margin;              // layout spacing outside the box
    float borderWidth = 0.0f;       // painted inside the box edge, the same on every side
    Rect scissor;
    bool hasScissor = false;
};

// One element a debug tool marks, plus the cached path that leads to it. Element
// pointers only stay valid until the next compose, so the cache is keyed by the
// compose generation instead of being pinned for the runtime's lifetime.
struct InspectionMark {
    std::string id;
    std::vector<const Element*> path;
    std::string pathId;
    std::uint64_t pathGeneration = 0;
};

// One translucent fill per box model region, never stroked. This is how browser
// devtools draw an element: the regions do not overlap, so a previewed element
// reads as a single light wash across its boxes instead of a frame drawn around a
// frame. The hues are the ones the browsers use for these regions, the alphas sit
// a little lower because a wash reads heavier over the dark pages EUI ships, and
// the values are fixed instead of themed because the overlay has to read the same
// over any page.
struct InspectionPalette {
    Color margin;
    Color border;
    Color padding;
    Color content;
};

inline constexpr InspectionPalette kInspectionHoverPalette{
    {0.96f, 0.70f, 0.42f, 0.55f},   // margin
    {1.00f, 0.90f, 0.60f, 0.55f},   // border
    {0.58f, 0.77f, 0.49f, 0.45f},   // padding
    {0.44f, 0.66f, 0.86f, 0.50f}    // content
};

// The area between two nested boxes as up to four rectangles. Filling the inner box
// on top of the outer one would blend the two translucent colours into a third, so
// the renderer fills the band that is left around the inner box instead. The bands
// share their edges and the side bands never run under the top and bottom ones.
struct InspectionBand {
    Rect rects[4];
    int count = 0;
};

inline InspectionBand inspectionBand(const Rect& outer, const Rect& inner) {
    InspectionBand band;
    const float outerRight = outer.x + outer.width;
    const float outerBottom = outer.y + outer.height;
    float top = inner.y - outer.y;
    if (top < 0.0f) {
        top = 0.0f;
    } else if (top > outer.height) {
        top = outer.height;
    }
    float bottom = outerBottom - (inner.y + inner.height);
    if (bottom < 0.0f) {
        bottom = 0.0f;
    } else if (bottom > outer.height - top) {
        bottom = outer.height - top;
    }
    const float middle = outer.height - top - bottom;
    float left = inner.x - outer.x;
    if (left < 0.0f) {
        left = 0.0f;
    }
    float right = outerRight - (inner.x + inner.width);
    if (right < 0.0f) {
        right = 0.0f;
    }
    if (top > 0.0f) {
        band.rects[band.count++] = {outer.x, outer.y, outer.width, top};
    }
    if (bottom > 0.0f) {
        band.rects[band.count++] = {outer.x, outer.y + top + middle, outer.width, bottom};
    }
    if (middle > 0.0f) {
        if (left > 0.0f) {
            band.rects[band.count++] = {outer.x, outer.y + top, left, middle};
        }
        if (right > 0.0f) {
            band.rects[band.count++] = {outerRight - right, outer.y + top, right, middle};
        }
    }
    return band;
}

struct InstanceStore;

#if defined(EUI_DEBUG_BUILD)
// Geometry of the inspection overlay for one mark. Both the panel (through
// `Runtime::debugInspection`) and the renderer read this, so there is one
// implementation of "where is that element now".
DebugInspection computeInspection(Ui& ui, InstanceStore& instances, InspectionMark& mark, float dpiScale);
#endif

} // namespace core::dsl::runtime
