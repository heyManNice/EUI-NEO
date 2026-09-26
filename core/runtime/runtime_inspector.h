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

// A property a debug tool may edit on a live element. The runtime knows how to read
// and write every one of them; which of them a tool shows, in what order and with
// which editor is the tool's business (see modules/devtools).
enum class DebugPropertyId {
    Color,
    Opacity,
    Radius,
    BorderWidth,
    BorderColor,
    Blur,
    ShadowEnabled,
    ShadowColor,
    ShadowBlur,
    ShadowOffsetY,
    TextColor
};

// How a property is written, which also tells an editor what to put in the row for
// it: a number field, a colour or a switch.
enum class DebugPropertyType { Number, Color, Flag };

inline constexpr int kDebugPropertyCount = static_cast<int>(DebugPropertyId::TextColor) + 1;

inline std::uint32_t debugPropertyBit(DebugPropertyId property) {
    return 1u << static_cast<std::uint32_t>(property);
}

inline constexpr DebugPropertyType debugPropertyType(DebugPropertyId property) {
    switch (property) {
    case DebugPropertyId::Color:
    case DebugPropertyId::BorderColor:
    case DebugPropertyId::ShadowColor:
    case DebugPropertyId::TextColor:
        return DebugPropertyType::Color;
    case DebugPropertyId::ShadowEnabled:
        return DebugPropertyType::Flag;
    default:
        return DebugPropertyType::Number;
    }
}

// What one element looks like right now, read on demand for the single element a
// tool inspects. `overridden` marks the properties a debug session replaced, so the
// tool can flag them and offer to put them back.
struct DebugElementProperties {
    bool active = false;
    std::string id;
    ElementKind kind = ElementKind::Stack;
    Rect frame;
    EdgeInsets margin;
    EdgeInsets padding;
    float borderWidth = 0.0f;
    int zIndex = 0;
    bool clip = false;
    bool interactive = false;
    bool disabled = false;
    std::string text;
    Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float radius = 0.0f;
    Color borderColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float blur = 0.0f;
    Shadow shadow;
    Color textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t overridden = 0;
};

// The values a debug session wrote on top of an element. A compose rebuilds every
// element from the app's own code, so the store is applied again to the freshly
// composed tree before layout runs; the mask is what a tool reads back.
struct DebugElementOverride {
    std::uint32_t mask = 0;
    Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float radius = 0.0f;
    float borderWidth = 0.0f;
    Color borderColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float blur = 0.0f;
    bool shadowEnabled = false;
    Color shadowColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float shadowBlur = 0.0f;
    float shadowOffsetY = 0.0f;
    Color textColor = {1.0f, 1.0f, 1.0f, 1.0f};
};

// Editing one shadow field of an element whose shadow is switched off would show
// nothing, so any shadow override also switches the shadow on and records that: the
// switch in the tool then tells the truth and can be put back.
inline void markDebugShadowEnabled(DebugElementOverride& override) {
    override.mask |= debugPropertyBit(DebugPropertyId::ShadowEnabled);
    override.shadowEnabled = true;
}

inline bool setDebugOverrideFloat(DebugElementOverride& override, DebugPropertyId property, float value) {
    float* target = nullptr;
    switch (property) {
    case DebugPropertyId::Opacity: target = &override.opacity; break;
    case DebugPropertyId::Radius: target = &override.radius; break;
    case DebugPropertyId::BorderWidth: target = &override.borderWidth; break;
    case DebugPropertyId::Blur: target = &override.blur; break;
    case DebugPropertyId::ShadowBlur: target = &override.shadowBlur; break;
    case DebugPropertyId::ShadowOffsetY: target = &override.shadowOffsetY; break;
    default: return false;
    }
    const bool changed = (override.mask & debugPropertyBit(property)) == 0 || *target != value;
    *target = value;
    override.mask |= debugPropertyBit(property);
    if (property == DebugPropertyId::ShadowBlur || property == DebugPropertyId::ShadowOffsetY) {
        markDebugShadowEnabled(override);
    }
    return changed;
}

inline bool setDebugOverrideColor(DebugElementOverride& override, DebugPropertyId property, const Color& value) {
    Color* target = nullptr;
    switch (property) {
    case DebugPropertyId::Color: target = &override.color; break;
    case DebugPropertyId::BorderColor: target = &override.borderColor; break;
    case DebugPropertyId::ShadowColor: target = &override.shadowColor; break;
    case DebugPropertyId::TextColor: target = &override.textColor; break;
    default: return false;
    }
    const bool changed = (override.mask & debugPropertyBit(property)) == 0 || !closeEnough(*target, value);
    *target = value;
    override.mask |= debugPropertyBit(property);
    if (property == DebugPropertyId::ShadowColor) {
        markDebugShadowEnabled(override);
    }
    return changed;
}

inline bool setDebugOverrideFlag(DebugElementOverride& override, DebugPropertyId property, bool value) {
    if (property != DebugPropertyId::ShadowEnabled) {
        return false;
    }
    const bool changed = (override.mask & debugPropertyBit(property)) == 0 || override.shadowEnabled != value;
    override.shadowEnabled = value;
    override.mask |= debugPropertyBit(property);
    return changed;
}

// Writes an override onto a composed element. Everything downstream (layout, the
// element tree snapshot, the render instances, hit testing) then sees it, which is
// why the runtime applies the store right after composing instead of teaching every
// property how to read from two places.
inline void applyDebugOverride(Element& element, const DebugElementOverride& override) {
    const std::uint32_t mask = override.mask;
    if (mask == 0) {
        return;
    }
    if (mask & debugPropertyBit(DebugPropertyId::Color)) {
        element.color = override.color;
    }
    if (mask & debugPropertyBit(DebugPropertyId::Opacity)) {
        element.opacity = override.opacity;
    }
    if (mask & debugPropertyBit(DebugPropertyId::Radius)) {
        element.radius = override.radius;
    }
    if (mask & debugPropertyBit(DebugPropertyId::BorderWidth)) {
        element.border.width = override.borderWidth;
    }
    if (mask & debugPropertyBit(DebugPropertyId::BorderColor)) {
        element.border.color = override.borderColor;
    }
    if (mask & debugPropertyBit(DebugPropertyId::Blur)) {
        element.blur = override.blur;
    }
    if (mask & debugPropertyBit(DebugPropertyId::ShadowEnabled)) {
        element.shadow.enabled = override.shadowEnabled;
    }
    if (mask & debugPropertyBit(DebugPropertyId::ShadowColor)) {
        element.shadow.color = override.shadowColor;
    }
    if (mask & debugPropertyBit(DebugPropertyId::ShadowBlur)) {
        element.shadow.blur = override.shadowBlur;
    }
    if (mask & debugPropertyBit(DebugPropertyId::ShadowOffsetY)) {
        element.shadow.offset.y = override.shadowOffsetY;
    }
    if (mask & debugPropertyBit(DebugPropertyId::TextColor)) {
        element.textColor = override.textColor;
    }
}

struct InstanceStore;

#if defined(EUI_DEBUG_BUILD)
// Geometry of the inspection overlay for one mark. Both the panel (through
// `Runtime::debugInspection`) and the renderer read this, so there is one
// implementation of "where is that element now".
DebugInspection computeInspection(Ui& ui, InstanceStore& instances, InspectionMark& mark, float dpiScale);
#endif

} // namespace core::dsl::runtime
