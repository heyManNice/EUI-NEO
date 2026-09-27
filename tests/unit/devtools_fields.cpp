#include "modules/devtools/devtools_fields.h"

#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// The fields a panel may write are one table inside this module, and this test is about the
// promises the module makes around them: the store writes the element the page drew, a
// value shows up in the next read, it survives a compose, and clearing it gives the
// element's own value back. The core is not asked about a single field, because it holds
// none: what the panel wrote travels to the page as one patch, applied through the hook the
// store registers.
#if defined(EUI_TOOLING)

namespace {

using modules::devtools::applyElementPatch;
using modules::devtools::ElementField;
using modules::devtools::ElementPatch;
using modules::devtools::ElementPatches;
using modules::devtools::ElementValues;
using modules::devtools::FieldKind;
using modules::devtools::fieldBit;
using modules::devtools::fieldKind;
using modules::devtools::fieldValueOf;
using modules::devtools::isElementField;
using modules::devtools::kElementFieldCount;
using modules::devtools::readElementValues;

constexpr float kFrameSeconds = 1.0f / 60.0f;

void composePage(core::dsl::Runtime& runtime) {
    runtime.compose("page", 300.0f, 200.0f, [](core::dsl::Ui& ui, const core::dsl::Screen&) {
        ui.stack("root")
            .size(300.0f, 200.0f)
            .content([&] {
                ui.rect("panel")
                    .position(10.0f, 20.0f)
                    .size(120.0f, 60.0f)
                    .color(core::Color{0.2f, 0.4f, 0.6f, 1.0f})
                    .radius(4.0f)
                    .build();
                ui.text("label")
                    .position(10.0f, 90.0f)
                    .size(100.0f, 20.0f)
                    .text("hello")
                    .color(core::Color{0.9f, 0.9f, 0.9f, 1.0f})
                    .build();
            })
            .build();
    });
}

// One frame the way the app runs it: the app composes again, and the store the panel
// registered puts its values back on the fresh tree before layout.
void frame(core::dsl::Runtime& runtime) {
    composePage(runtime);
    runtime.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
}

ElementValues read(core::dsl::Runtime& runtime, const ElementPatches& patches,
                   const std::string& id) {
    return readElementValues(runtime, id, patches.written(id));
}

// The host writes the element it is looking at in the same frame it records the edit, so
// the panel and the page never disagree about what is on screen.
void writeLiveElement(core::dsl::Runtime& runtime, const ElementPatches& patches,
                      const std::string& id) {
    const ElementPatch* patch = patches.find(id);
    core::dsl::Element* element = runtime.findElement(id);
    if (patch != nullptr && element != nullptr) {
        applyElementPatch(*element, *patch);
    }
}

float number(const ElementValues& values, ElementField field) {
    return values.field(field).number;
}

core::Color color(const ElementValues& values, ElementField field) {
    return values.field(field).color;
}

bool flag(const ElementValues& values, ElementField field) {
    return values.field(field).flag;
}

} // namespace

int main() {
    // The table is the single description of what a panel may write: every field has one
    // kind, and a bit no other field has.
    {
        assert(kElementFieldCount > 0);
        assert(fieldKind(ElementField::Color) == FieldKind::Color);
        assert(fieldKind(ElementField::BorderColor) == FieldKind::Color);
        assert(fieldKind(ElementField::Blur) == FieldKind::Number);
        assert(fieldKind(ElementField::ShadowEnabled) == FieldKind::Flag);
        assert(fieldKind(ElementField::ShadowInset) == FieldKind::Flag);
        std::uint32_t seen = 0;
        for (int index = 0; index < kElementFieldCount; ++index) {
            const std::uint32_t bit = fieldBit(static_cast<ElementField>(index));
            assert(bit != 0 && (seen & bit) == 0);
            seen |= bit;
        }

        // The sentinel that says how many fields there are is not a field, and neither is
        // anything cast into the enum from outside. Both are refused before they can be used
        // as an index: the stores below are exactly `kElementFieldCount` wide.
        assert(!isElementField(ElementField::Count));
        assert(!isElementField(static_cast<ElementField>(-1)));
        assert(!isElementField(static_cast<ElementField>(kElementFieldCount)));
        assert(fieldBit(ElementField::Count) == 0u);
        assert(fieldBit(static_cast<ElementField>(kElementFieldCount + 7)) == 0u);

        ElementPatch refused;
        assert(!refused.set(ElementField::Count, fieldValueOf(1.0f)));
        assert(!refused.set(static_cast<ElementField>(kElementFieldCount + 7), fieldValueOf(1.0f)));
        refused.clear(static_cast<ElementField>(kElementFieldCount + 7));
        assert(refused.mask == 0u);
        assert(!refused.has(ElementField::Count));
        assert(refused.get(ElementField::Count).kind == FieldKind::Number);
        assert(refused.get(static_cast<ElementField>(kElementFieldCount + 7)).number == 0.0f);

        ElementValues none;
        none.setField(ElementField::Count, fieldValueOf(1.0f));
        none.setField(static_cast<ElementField>(kElementFieldCount + 7), fieldValueOf(1.0f));
        assert(none.field(ElementField::Count).number == 0.0f);
        assert(!none.wasWritten(ElementField::Count));
    }

    core::dsl::Runtime runtime;
    ElementPatches patches;
    // The one line that ties the store to the page: the page composes, the store puts the
    // values it holds back on the tree it just built.
    runtime.setAfterCompose([&patches, &runtime] { patches.apply(runtime); });
    frame(runtime);

    const ElementValues panel = read(runtime, patches, "page.panel");
    assert(panel.active);
    assert(panel.id == "page.panel");
    assert(panel.kind == core::dsl::ElementKind::Rect);
    assert(panel.frame.width == 120.0f && panel.frame.height == 60.0f);
    assert(color(panel, ElementField::Color).r == 0.2f);
    assert(color(panel, ElementField::Color).g == 0.4f);
    assert(color(panel, ElementField::Color).b == 0.6f);
    assert(number(panel, ElementField::Radius) == 4.0f);
    assert(!flag(panel, ElementField::ShadowEnabled));
    assert(panel.written == 0);

    // Nothing is written until a panel writes, and an element the page does not have
    // simply has no values to show.
    assert(patches.empty());
    assert(!read(runtime, patches, "page.missing").active);

    // Writing shows up in the next read, and it is not a structural change: the app
    // composed the same tree as before.
    const std::uint64_t revision = runtime.elementStructureRevision();
    assert(patches.set("page.panel", ElementField::Radius, fieldValueOf(12.0f)));
    assert(patches.set("page.panel", ElementField::Color, fieldValueOf(core::Color{1.0f, 0.0f, 0.0f, 1.0f})));
    // The panel writes the element the page already drew, so the frame it is looking at
    // changes without waiting for the app's next compose.
    writeLiveElement(runtime, patches, "page.panel");
    if (const core::dsl::Element* live = runtime.findElement("page.panel")) {
        assert(live->radius == 12.0f && live->color.r == 1.0f);
    }
    frame(runtime);
    const ElementValues written = read(runtime, patches, "page.panel");
    assert(number(written, ElementField::Radius) == 12.0f);
    assert(color(written, ElementField::Color).r == 1.0f);
    assert(color(written, ElementField::Color).g == 0.0f);
    assert(written.wasWritten(ElementField::Radius));
    assert(written.wasWritten(ElementField::Color));
    assert(!written.wasWritten(ElementField::Opacity));
    assert(patches.count() == 1);
    assert(runtime.elementStructureRevision() == revision);

    // A compose rebuilds every element from the app's code, so the store is applied again
    // to the fresh tree: that is what makes an edit survive the app.
    composePage(runtime);
    runtime.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
    const ElementValues recomposed = read(runtime, patches, "page.panel");
    assert(number(recomposed, ElementField::Radius) == 12.0f);
    assert(color(recomposed, ElementField::Color).r == 1.0f);
    assert(color(recomposed, ElementField::Color).b == 0.0f);

    // A value whose kind does not match the field is dropped instead of landing in the
    // wrong member: the field table, not the caller, says which kind a field holds.
    assert(!patches.set("page.panel", ElementField::Radius, fieldValueOf(core::Color{0.0f, 1.0f, 0.0f, 1.0f})));
    assert(!patches.set("page.panel", ElementField::Blur, fieldValueOf(true)));
    assert(patches.count() == 1);
    frame(runtime);
    assert(number(read(runtime, patches, "page.panel"), ElementField::Radius) == 12.0f);
    assert(number(read(runtime, patches, "page.panel"), ElementField::Blur) == 0.0f);

    // A field that only shows while another one is on switches it on by itself, so the
    // value can be seen at all, and the switch reports itself as written. A field with no
    // such requirement leaves the others alone.
    assert(patches.set("page.panel", ElementField::ShadowBlur, fieldValueOf(9.0f)));
    frame(runtime);
    const ElementValues shadowed = read(runtime, patches, "page.panel");
    assert(flag(shadowed, ElementField::ShadowEnabled));
    assert(number(shadowed, ElementField::ShadowBlur) == 9.0f);
    assert(shadowed.wasWritten(ElementField::ShadowEnabled));
    assert(!shadowed.wasWritten(ElementField::GradientEnabled));

    assert(patches.set("page.panel", ElementField::ShadowSpread, fieldValueOf(5.0f)));
    assert(patches.set("page.panel", ElementField::ShadowOffsetX, fieldValueOf(-6.0f)));
    assert(patches.set("page.panel", ElementField::ShadowInset, fieldValueOf(true)));
    frame(runtime);
    const ElementValues spread = read(runtime, patches, "page.panel");
    assert(number(spread, ElementField::ShadowSpread) == 5.0f);
    assert(number(spread, ElementField::ShadowOffsetX) == -6.0f);
    assert(flag(spread, ElementField::ShadowInset));
    assert(flag(spread, ElementField::ShadowEnabled));

    assert(patches.set("page.panel", ElementField::GradientStart,
                       fieldValueOf(core::Color{1.0f, 0.0f, 0.0f, 1.0f})));
    frame(runtime);
    const ElementValues gradient = read(runtime, patches, "page.panel");
    assert(flag(gradient, ElementField::GradientEnabled));
    assert(color(gradient, ElementField::GradientStart).r == 1.0f);

    assert(patches.set("page.panel", ElementField::GradientEnd,
                       fieldValueOf(core::Color{0.0f, 0.0f, 1.0f, 1.0f})));
    frame(runtime);
    assert(color(read(runtime, patches, "page.panel"), ElementField::GradientEnd).b == 1.0f);

    // A switch a panel wrote can be written off again, and the fields that needed it keep
    // their own values: nothing here is coupled except the one line the table states.
    assert(patches.set("page.panel", ElementField::GradientEnabled, fieldValueOf(false)));
    assert(patches.set("page.panel", ElementField::ShadowEnabled, fieldValueOf(false)));
    frame(runtime);
    const ElementValues off = read(runtime, patches, "page.panel");
    assert(!flag(off, ElementField::GradientEnabled));
    assert(!flag(off, ElementField::ShadowEnabled));
    assert(number(off, ElementField::ShadowBlur) == 9.0f);

    // Patches are per element: a second element keeps its own values.
    assert(patches.set("page.label", ElementField::TextColor,
                       fieldValueOf(core::Color{0.1f, 1.0f, 0.2f, 1.0f})));
    frame(runtime);
    assert(color(read(runtime, patches, "page.label"), ElementField::TextColor).g == 1.0f);
    assert(color(read(runtime, patches, "page.panel"), ElementField::TextColor).g ==
           color(panel, ElementField::TextColor).g);
    assert(patches.count() == 2);

    // Putting one field back leaves the other patches alone, and the element has to be
    // composed again for its own value to come back.
    patches.clear("page.panel", ElementField::Radius);
    frame(runtime);
    const ElementValues restored = read(runtime, patches, "page.panel");
    assert(number(restored, ElementField::Radius) == 4.0f);
    assert(color(restored, ElementField::Color).r == 1.0f);

    // An id the page does not have cannot be written to a live element, and clearing one
    // does not disturb the elements that are there.
    assert(patches.set("page.missing", ElementField::Radius, fieldValueOf(3.0f)));
    writeLiveElement(runtime, patches, "page.missing");
    frame(runtime);
    assert(number(read(runtime, patches, "page.panel"), ElementField::Radius) == 4.0f);
    patches.clearElement("page.missing");
    patches.clearElement("page.panel");
    frame(runtime);
    assert(color(read(runtime, patches, "page.panel"), ElementField::Color).r == 0.2f);
    assert(patches.count() == 1);   // the label still carries a patch

    // The panel's reset drops every patch at once, and the store lets go of the ids it
    // held so it cannot grow back on its own.
    assert(patches.set("page.label", ElementField::Opacity, fieldValueOf(0.5f)));
    frame(runtime);
    assert(patches.count() == 1);
    patches.clearAll();
    assert(patches.empty());
    frame(runtime);
    const ElementValues afterReset = read(runtime, patches, "page.label");
    assert(color(afterReset, ElementField::TextColor).g == 0.9f);
    assert(afterReset.written == 0);

    // A field put back on its own value leaves the store empty for that element instead of
    // keeping a patch that writes nothing.
    assert(patches.set("page.panel", ElementField::Opacity, fieldValueOf(0.25f)));
    patches.clear("page.panel", ElementField::Opacity);
    assert(patches.empty());

    // Detaching is the app's side of the seam: with the hook gone the store still holds
    // what it was given, but nothing puts it on the page any more.
    assert(patches.set("page.panel", ElementField::Radius, fieldValueOf(21.0f)));
    assert(patches.count() == 1);
    runtime.setAfterCompose(nullptr);
    frame(runtime);
    assert(number(read(runtime, patches, "page.panel"), ElementField::Radius) == 4.0f);

    runtime.shutdown(false);
    return 0;
}

#else

int main() {
    // The panel is not part of this configuration, so neither is the store it keeps.
    return 0;
}

#endif
