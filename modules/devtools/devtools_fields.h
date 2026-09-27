#pragma once

#include "core/dsl_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The values the panel can read and write on an element.
//
// This is the panel's own model. The framework offers a page's elements, a way to look one
// up, a moment to write it (after a compose, before layout) and a way to ask for the frame
// that shows the result; it does not know what a "field" is, which of them a row can edit,
// or what kind of value one holds. "Editable values" is an editing decision, and editing is
// what the panel does.
//
// One table below describes every field: the name the panel gives it, the kind of value it
// holds, and the element member it reads and writes. Reading, writing and applying a patch
// all walk that table, so adding a field is one row here plus one row in the property area.

#if defined(EUI_TOOLING)

namespace modules::devtools {

// A page can hold tens of thousands of elements and a tool that reads one only ever shows a
// window of the text, so the values read for the panel stop copying at these sizes. They are
// about what a copy costs, not about what a tool displays: a tool that wants another limit
// uses its own.
inline constexpr std::size_t kElementValueTextLimit = 64;

// Copies at most `limit` bytes of text without splitting a UTF-8 sequence, so a truncated
// label never turns into invalid bytes.
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


// The fields a tool may write on a live element, one line each: the name a tool uses for
// the field, the kind of value it holds, and the element member it reads and writes.
//
// This table is the whole of what the module knows about editable values. Reading a field,
// writing a field and applying what a tool wrote all walk the table; nothing here knows what
// a tool calls the field, in which order it shows it, what range its editor covers or which
// control it puts in the row. A new field is one line here plus one row in the tool that
// presents it (see modules/devtools), and no function grows a branch.
#define DEVTOOLS_ELEMENT_FIELD_TABLE(X)                   \
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
#define DEVTOOLS_ELEMENT_FIELD_ID(name, kind, member) name,
    DEVTOOLS_ELEMENT_FIELD_TABLE(DEVTOOLS_ELEMENT_FIELD_ID)
#undef DEVTOOLS_ELEMENT_FIELD_ID
    Count
};

inline constexpr int kElementFieldCount = static_cast<int>(ElementField::Count);

// `ElementField::Count` says how many fields there are; it is not a field. A value cast into
// the enum from outside — a stored index, a script, an off-by-one — is refused here instead
// of being used as an index into tables that are exactly `kElementFieldCount` long.
inline constexpr bool isElementField(ElementField field) {
    return static_cast<int>(field) >= 0 && static_cast<int>(field) < kElementFieldCount;
}

// The kind of value a field holds. It is read from the table above, which is also where an
// editor learns which control a row needs.
enum class FieldKind { Number, Color, Flag };

inline constexpr FieldKind fieldKind(ElementField field) {
    switch (field) {
#define DEVTOOLS_ELEMENT_FIELD_KIND(name, kind, member) \
    case ElementField::name:                       \
        return FieldKind::kind;
        DEVTOOLS_ELEMENT_FIELD_TABLE(DEVTOOLS_ELEMENT_FIELD_KIND)
#undef DEVTOOLS_ELEMENT_FIELD_KIND
    case ElementField::Count:
        break;
    }
    return FieldKind::Number;
}

inline std::uint32_t fieldBit(ElementField field) {
    return isElementField(field) ? (1u << static_cast<std::uint32_t>(field)) : 0u;
}

// One value a tool wrote, carried together with the kind it holds. The value travels with
// its kind so a tool can build one from its own row and hand it over without the runtime
// knowing what the field means; a value whose kind does not match the field it is written
// to is dropped instead of landing in the wrong member.
struct FieldValue {
    FieldKind kind = FieldKind::Number;
    float number = 0.0f;
    core::Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool flag = false;
};

inline FieldValue fieldValueOf(float value) {
    FieldValue result;
    result.kind = FieldKind::Number;
    result.number = value;
    return result;
}

inline FieldValue fieldValueOf(const core::Color& value) {
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
    case FieldKind::Color: return core::closeEnough(left.color, right.color);
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

inline void assignField(core::Color& target, const FieldValue& value) {
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
inline FieldValue readField(const core::Color& value) { return fieldValueOf(value); }
inline FieldValue readField(bool value) { return fieldValueOf(value); }

// A field that only shows while another one is on. Writing the first switches the second
// on, or a tool would write a value nothing draws; the switch then reports itself as
// written, so the tool can offer to put it back. The table has the shape of the field
// table, so a field that needs a switch brings its own row along.
#define DEVTOOLS_ELEMENT_FIELD_REQUIRES(X) \
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

    bool has(ElementField field) const { return isElementField(field) && (mask & fieldBit(field)) != 0u; }

    FieldValue get(ElementField field) const {
        return has(field) ? values[static_cast<std::size_t>(field)] : FieldValue{};
    }

    // Writes one value and says whether that changed the patch. Writing a field also
    // switches on the field it needs, if it has one. A field the table does not have is
    // refused: the store is exactly as wide as the table.
    bool set(ElementField field, const FieldValue& value) {
        if (!isElementField(field) || value.kind != fieldKind(field)) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(field);
        const bool changed = !has(field) || !sameFieldValue(values[index], value);
        values[index] = value;
        mask |= fieldBit(field);
        switch (field) {
#define DEVTOOLS_ELEMENT_FIELD_NEEDS(name, other)                                    \
    case ElementField::name: {                                                  \
        const std::size_t needed = static_cast<std::size_t>(ElementField::other); \
        values[needed] = fieldValueOf(true);                                    \
        mask |= fieldBit(ElementField::other);                                  \
        break;                                                                  \
    }
            DEVTOOLS_ELEMENT_FIELD_REQUIRES(DEVTOOLS_ELEMENT_FIELD_NEEDS)
#undef DEVTOOLS_ELEMENT_FIELD_NEEDS
        default:
            break;
        }
        return changed;
    }

    void clear(ElementField field) { mask &= ~fieldBit(field); }
};

// The value of one field of a live element.
inline FieldValue readElementField(const core::dsl::Element& element, ElementField field) {
    switch (field) {
#define DEVTOOLS_ELEMENT_FIELD_READ(name, kind, member) \
    case ElementField::name:                       \
        return readField(element.member);
        DEVTOOLS_ELEMENT_FIELD_TABLE(DEVTOOLS_ELEMENT_FIELD_READ)
#undef DEVTOOLS_ELEMENT_FIELD_READ
    case ElementField::Count:
        break;
    }
    return {};
}

// Writes one field of a live element, and says whether the field took the value.
inline bool writeElementField(core::dsl::Element& element, ElementField field, const FieldValue& value) {
    if (!isElementField(field) || value.kind != fieldKind(field)) {
        return false;
    }
    switch (field) {
#define DEVTOOLS_ELEMENT_FIELD_WRITE(name, kind, member) \
    case ElementField::name:                        \
        assignField(element.member, value);         \
        break;
        DEVTOOLS_ELEMENT_FIELD_TABLE(DEVTOOLS_ELEMENT_FIELD_WRITE)
#undef DEVTOOLS_ELEMENT_FIELD_WRITE
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
inline void applyElementPatch(core::dsl::Element& element, const ElementPatch& patch) {
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
// without the module knowing which fields it added. `written` marks the fields a tool
// replaced, so the tool can flag them and offer to put them back.
struct ElementValues {
    bool active = false;
    std::string id;
    core::dsl::ElementKind kind = core::dsl::ElementKind::Stack;
    core::Rect frame;
    core::EdgeInsets margin;
    core::EdgeInsets padding;
    float borderWidth = 0.0f;
    int zIndex = 0;
    bool clip = false;
    bool interactive = false;
    bool disabled = false;
    std::string text;
    std::array<FieldValue, kElementFieldCount> fields{};
    std::uint32_t written = 0;

    FieldValue field(ElementField which) const {
        return isElementField(which) ? fields[static_cast<std::size_t>(which)] : FieldValue{};
    }
    void setField(ElementField which, const FieldValue& value) {
        if (isElementField(which)) {
            fields[static_cast<std::size_t>(which)] = value;
        }
    }
    bool wasWritten(ElementField which) const { return (written & fieldBit(which)) != 0u; }
};

// The values the panel wrote over the page, keyed by element id, and the one place that puts
// them back: the page rebuilds every element from the app's code on every compose, so the
// store is re-applied to the fresh tree before layout runs (through the `afterCompose` hook
// the panel registers).
class ElementPatches {
public:
    bool empty() const { return patches_.empty(); }
    std::size_t count() const { return patches_.size(); }

    // The fields written for one element, as the mask the panel shows.
    std::uint32_t written(const std::string& id) const;

    // The patch of one element, or null when nothing was written for it.
    const ElementPatch* find(const std::string& id) const;

    // Writes one value and says whether that changed anything: the same value written twice
    // is not a new edit, so nothing has to be painted again for it. Writing a field also
    // switches on the field it needs, so a value that would otherwise be invisible shows.
    bool set(const std::string& id, ElementField field, const FieldValue& value);
    void clear(const std::string& id, ElementField field);
    void clearElement(const std::string& id);
    void clearAll();

    // Writes every patch onto the page's live tree.
    void apply(core::dsl::Runtime& page) const;

private:
    std::unordered_map<std::string, ElementPatch> patches_;
};

// The values of one element, read on demand for the single element the panel shows.
// `written` is the mask of fields the store holds for it.
ElementValues readElementValues(const core::dsl::Runtime& page, const std::string& id, std::uint32_t written);

} // namespace modules::devtools

#endif
