#pragma once


#include "core/dsl.h"
#include "core/runtime/runtime_geometry.h"

#include <array>
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

// The box of one element, resolved into the space the render pass draws in: the same
// transform and clip the element itself is drawn with, so a tool that draws from this
// cannot drift away from the element it describes. This is the core's Resolve half of the
// seam: geometry only. What a tool draws with it (a box model wash, an outline, nothing)
// lives with the tool.
struct ElementBox {
    bool active = false;
    RenderTransform transform;
    LayoutRect frame;               // the box itself: background and border live here
    EdgeInsets padding;             // inset from the box edge to the content
    EdgeInsets margin;              // layout spacing outside the box
    float borderWidth = 0.0f;       // painted inside the box edge, the same on every side
    Rect scissor;
    bool hasScissor = false;
};

// One element a tool marks, plus the cached path that leads to it. Element pointers only
// stay valid until the next compose, so the cache is keyed by the compose generation
// instead of being pinned for the runtime's lifetime.
struct ElementMark {
    std::string id;
    std::vector<const Element*> path;
    std::string pathId;
    std::uint64_t pathGeneration = 0;
};

// The fields a tool may write on a live element, one line each: the name a tool uses for
// the field, the kind of value it holds, and the element member it reads and writes.
//
// This table is the whole of what the core knows about editable values. The core reads a
// field, writes a field and applies what a tool wrote; it does not know what a tool calls
// the field, in which order it shows it, what range its editor covers or which control it
// puts in the row. A new field is one line here plus one row in the tool that presents it
// (see modules/devtools), and no function in the core grows a branch.
#define EUI_ELEMENT_FIELD_TABLE(X)                   \
    X(Color,           Color,  color)                \
    X(Opacity,         Number, opacity)              \
    X(Radius,          Number, radius)               \
    X(BorderWidth,     Number, border.width)         \
    X(BorderColor,     Color,  border.color)         \
    X(Blur,            Number, blur)                 \
    X(ShadowEnabled,   Flag,   shadow.enabled)       \
    X(ShadowColor,     Color,  shadow.color)         \
    X(ShadowBlur,      Number, shadow.blur)          \
    X(ShadowOffsetX,   Number, shadow.offset.x)      \
    X(ShadowOffsetY,   Number, shadow.offset.y)      \
    X(ShadowSpread,    Number, shadow.spread)        \
    X(ShadowInset,     Flag,   shadow.inset)         \
    X(GradientEnabled, Flag,   gradient.enabled)     \
    X(GradientStart,   Color,  gradient.start)       \
    X(GradientEnd,     Color,  gradient.end)         \
    X(TextColor,       Color,  textColor)

enum class ElementField {
#define EUI_ELEMENT_FIELD_ID(name, kind, member) name,
    EUI_ELEMENT_FIELD_TABLE(EUI_ELEMENT_FIELD_ID)
#undef EUI_ELEMENT_FIELD_ID
    Count
};

inline constexpr int kElementFieldCount = static_cast<int>(ElementField::Count);

// The kind of value a field holds. It is read from the table above, which is also where an
// editor learns which control a row needs.
enum class FieldKind { Number, Color, Flag };

inline constexpr FieldKind fieldKind(ElementField field) {
    switch (field) {
#define EUI_ELEMENT_FIELD_KIND(name, kind, member) \
    case ElementField::name:                       \
        return FieldKind::kind;
        EUI_ELEMENT_FIELD_TABLE(EUI_ELEMENT_FIELD_KIND)
#undef EUI_ELEMENT_FIELD_KIND
    case ElementField::Count:
        break;
    }
    return FieldKind::Number;
}

inline std::uint32_t fieldBit(ElementField field) {
    return 1u << static_cast<std::uint32_t>(field);
}

// One value a tool wrote, carried together with the kind it holds. The value travels with
// its kind so a tool can build one from its own row and hand it over without the runtime
// knowing what the field means; a value whose kind does not match the field it is written
// to is dropped instead of landing in the wrong member.
struct FieldValue {
    FieldKind kind = FieldKind::Number;
    float number = 0.0f;
    Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool flag = false;
};

inline FieldValue fieldValueOf(float value) {
    FieldValue result;
    result.kind = FieldKind::Number;
    result.number = value;
    return result;
}

inline FieldValue fieldValueOf(const Color& value) {
    FieldValue result;
    result.kind = FieldKind::Color;
    result.color = value;
    return result;
}

inline FieldValue fieldValueOf(bool value) {
    FieldValue result;
    result.kind = FieldKind::Flag;
    result.flag = value;
    return result;
}

inline bool sameFieldValue(const FieldValue& left, const FieldValue& right) {
    if (left.kind != right.kind) {
        return false;
    }
    switch (left.kind) {
    case FieldKind::Number: return left.number == right.number;
    case FieldKind::Color: return closeEnough(left.color, right.color);
    case FieldKind::Flag: return left.flag == right.flag;
    }
    return true;
}

// Assigns a value to a member of an element or of a patch, picked by the member's own type.
// The field table names the member, so nothing here has to know which field it is.
inline void assignField(float& target, const FieldValue& value) {
    if (value.kind == FieldKind::Number) {
        target = value.number;
    }
}

inline void assignField(Color& target, const FieldValue& value) {
    if (value.kind == FieldKind::Color) {
        target = value.color;
    }
}

inline void assignField(bool& target, const FieldValue& value) {
    if (value.kind == FieldKind::Flag) {
        target = value.flag;
    }
}

inline FieldValue readField(float value) { return fieldValueOf(value); }
inline FieldValue readField(const Color& value) { return fieldValueOf(value); }
inline FieldValue readField(bool value) { return fieldValueOf(value); }

// A field that only shows while another one is on. Writing the first switches the second
// on, or a tool would write a value nothing draws; the switch then reports itself as
// written, so the tool can offer to put it back. The table has the shape of the field
// table, so a field that needs a switch brings its own row along.
#define EUI_ELEMENT_FIELD_REQUIRES(X) \
    X(ShadowColor, ShadowEnabled)     \
    X(ShadowBlur, ShadowEnabled)      \
    X(ShadowOffsetX, ShadowEnabled)   \
    X(ShadowOffsetY, ShadowEnabled)   \
    X(ShadowSpread, ShadowEnabled)    \
    X(GradientStart, GradientEnabled) \
    X(GradientEnd, GradientEnabled)

// The values a tool wrote over the elements of a page, with the mask of which fields it
// wrote. A compose rebuilds every element from the app's code, so the store is applied
// again to the freshly composed tree before layout runs: that is what makes an edit survive
// the app, and what keeps the app's own state untouched.
struct ElementPatch {
    std::uint32_t mask = 0;
    std::array<FieldValue, kElementFieldCount> values{};

    bool has(ElementField field) const { return (mask & fieldBit(field)) != 0u; }

    FieldValue get(ElementField field) const {
        return has(field) ? values[static_cast<std::size_t>(field)] : FieldValue{};
    }

    // Writes one value and says whether that changed the patch. Writing a field also
    // switches on the field it needs, if it has one.
    bool set(ElementField field, const FieldValue& value) {
        if (value.kind != fieldKind(field)) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(field);
        const bool changed = !has(field) || !sameFieldValue(values[index], value);
        values[index] = value;
        mask |= fieldBit(field);
        switch (field) {
#define EUI_ELEMENT_FIELD_NEEDS(name, other)                                    \
    case ElementField::name: {                                                  \
        const std::size_t needed = static_cast<std::size_t>(ElementField::other); \
        values[needed] = fieldValueOf(true);                                    \
        mask |= fieldBit(ElementField::other);                                  \
        break;                                                                  \
    }
            EUI_ELEMENT_FIELD_REQUIRES(EUI_ELEMENT_FIELD_NEEDS)
#undef EUI_ELEMENT_FIELD_NEEDS
        default:
            break;
        }
        return changed;
    }

    void clear(ElementField field) { mask &= ~fieldBit(field); }
};

// The value of one field of a live element.
inline FieldValue readElementField(const Element& element, ElementField field) {
    switch (field) {
#define EUI_ELEMENT_FIELD_READ(name, kind, member) \
    case ElementField::name:                       \
        return readField(element.member);
        EUI_ELEMENT_FIELD_TABLE(EUI_ELEMENT_FIELD_READ)
#undef EUI_ELEMENT_FIELD_READ
    case ElementField::Count:
        break;
    }
    return {};
}

// Writes one field of a live element, and says whether the field took the value.
inline bool writeElementField(Element& element, ElementField field, const FieldValue& value) {
    if (value.kind != fieldKind(field)) {
        return false;
    }
    switch (field) {
#define EUI_ELEMENT_FIELD_WRITE(name, kind, member) \
    case ElementField::name:                        \
        assignField(element.member, value);         \
        break;
        EUI_ELEMENT_FIELD_TABLE(EUI_ELEMENT_FIELD_WRITE)
#undef EUI_ELEMENT_FIELD_WRITE
    case ElementField::Count:
        return false;
    }
    return true;
}

// Applies a patch to a composed element. Everything downstream (layout, the element tree
// snapshot, the render instances, hit testing) then sees the written values, which is why
// the runtime applies the store right after composing instead of teaching every field how
// to read from two places. The loop walks the field table instead of a hand-written list,
// so a field added above is applied without touching this.
inline void applyElementPatch(Element& element, const ElementPatch& patch) {
    if (patch.mask == 0) {
        return;
    }
    for (int index = 0; index < kElementFieldCount; ++index) {
        const ElementField field = static_cast<ElementField>(index);
        if (patch.has(field)) {
            writeElementField(element, field, patch.values[static_cast<std::size_t>(index)]);
        }
    }
}

// What one element looks like right now, read on demand for the single element a tool
// inspects. The geometry and the facts about the element are named; the values a tool can
// write live in `fields`, indexed by field, so a tool that added a row reads it back
// without the core knowing which fields it added. `written` marks the fields a tool
// replaced, so the tool can flag them and offer to put them back.
struct ElementValues {
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
    std::array<FieldValue, kElementFieldCount> fields{};
    std::uint32_t written = 0;

    FieldValue field(ElementField which) const { return fields[static_cast<std::size_t>(which)]; }
    void setField(ElementField which, const FieldValue& value) {
        fields[static_cast<std::size_t>(which)] = value;
    }
    bool wasWritten(ElementField which) const { return (written & fieldBit(which)) != 0u; }
};

class InstanceStore;

// Looks an element up by the id a tool holds. The walk uses `children`, not
// `orderedChildren`, so it also works between a compose and the next layout pass, and
// it hands out a writable element: a tool writes the values it replaced on it.
inline Element* findElement(const Ui& ui, const std::string& id) {
    std::vector<Element*> pending;
    pending.reserve(ui.roots().size());
    for (const auto& root : ui.roots()) {
        pending.push_back(root.get());
    }
    while (!pending.empty()) {
        Element* element = pending.back();
        pending.pop_back();
        if (element->id == id) {
            return element;
        }
        for (const auto& child : element->children) {
            pending.push_back(child.get());
        }
    }
    return nullptr;
}

// Geometry of the box overlay for one mark, resolved for the pass that is about to draw.
// `composeGeneration` is what tells the
// mark whether the path it cached is still valid, since element pointers only live
// until the next compose.
ElementBox computeElementBox(Ui& ui,
                             InstanceStore& instances,
                             ElementMark& mark,
                             std::uint64_t composeGeneration,
                             float dpiScale);

} // namespace core::dsl::runtime
