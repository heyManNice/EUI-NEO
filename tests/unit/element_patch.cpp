#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// The fields a tool may write are one table in core/tooling/model.h, and this test is
// about the promises the runtime makes around them: the table reads and writes a live
// element, a value shows up in the next read, it survives a compose, and clearing it
// gives the element's own value back without the app ever knowing. Nothing here names a
// field the runtime has a branch for, because the runtime has none.
#if defined(EUI_DEBUG_BUILD)

namespace {

using core::dsl::runtime::ElementField;
using core::dsl::runtime::ElementValues;
using core::dsl::runtime::FieldKind;
using core::dsl::runtime::fieldBit;
using core::dsl::runtime::fieldKind;
using core::dsl::runtime::fieldValueOf;
using core::dsl::runtime::kElementFieldCount;

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

void settle(core::dsl::Runtime& runtime) {
    runtime.update(nullptr, kFrameSeconds, 1.0f, 1.0f);
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
    // The table is the single description of what a tool may write: every field has one
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
    }

    core::dsl::Runtime runtime;
    composePage(runtime);
    settle(runtime);

    const ElementValues panel = runtime.elementValues("page.panel");
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

    // Nothing is written until a tool writes, and an element the page does not have
    // simply has no values to show.
    assert(runtime.elementPatchCount() == 0);
    assert(!runtime.elementValues("page.missing").active);

    // Writing shows up in the next read, and it is not a structural change: the app
    // composed the same tree as before.
    const std::uint64_t revision = runtime.elementStructureRevision();
    runtime.setElementField("page.panel", ElementField::Radius, fieldValueOf(12.0f));
    runtime.setElementField("page.panel", ElementField::Color, fieldValueOf(core::Color{1.0f, 0.0f, 0.0f, 1.0f}));
    settle(runtime);
    const ElementValues written = runtime.elementValues("page.panel");
    assert(number(written, ElementField::Radius) == 12.0f);
    assert(color(written, ElementField::Color).r == 1.0f);
    assert(color(written, ElementField::Color).g == 0.0f);
    assert(written.wasWritten(ElementField::Radius));
    assert(written.wasWritten(ElementField::Color));
    assert(!written.wasWritten(ElementField::Opacity));
    assert(runtime.elementPatchCount() == 1);
    assert(runtime.elementStructureRevision() == revision);

    // A compose rebuilds every element from the app's code, so the runtime applies its
    // store again to the fresh tree: that is what makes an edit survive the app.
    composePage(runtime);
    settle(runtime);
    const ElementValues recomposed = runtime.elementValues("page.panel");
    assert(number(recomposed, ElementField::Radius) == 12.0f);
    assert(color(recomposed, ElementField::Color).r == 1.0f);
    assert(color(recomposed, ElementField::Color).b == 0.0f);

    // A value whose kind does not match the field is dropped instead of landing in the
    // wrong member: the field table, not the caller, says which kind a field holds.
    runtime.setElementField("page.panel", ElementField::Radius, fieldValueOf(core::Color{0.0f, 1.0f, 0.0f, 1.0f}));
    runtime.setElementField("page.panel", ElementField::Blur, fieldValueOf(true));
    settle(runtime);
    assert(number(runtime.elementValues("page.panel"), ElementField::Radius) == 12.0f);
    assert(number(runtime.elementValues("page.panel"), ElementField::Blur) == 0.0f);

    // A field that only shows while another one is on switches it on by itself, so the
    // value can be seen at all, and the switch reports itself as written. A field with no
    // such requirement leaves the others alone.
    runtime.setElementField("page.panel", ElementField::ShadowBlur, fieldValueOf(9.0f));
    settle(runtime);
    const ElementValues shadowed = runtime.elementValues("page.panel");
    assert(flag(shadowed, ElementField::ShadowEnabled));
    assert(number(shadowed, ElementField::ShadowBlur) == 9.0f);
    assert(shadowed.wasWritten(ElementField::ShadowEnabled));
    assert(!shadowed.wasWritten(ElementField::GradientEnabled));

    runtime.setElementField("page.panel", ElementField::ShadowSpread, fieldValueOf(5.0f));
    runtime.setElementField("page.panel", ElementField::ShadowOffsetX, fieldValueOf(-6.0f));
    runtime.setElementField("page.panel", ElementField::ShadowInset, fieldValueOf(true));
    settle(runtime);
    const ElementValues spread = runtime.elementValues("page.panel");
    assert(number(spread, ElementField::ShadowSpread) == 5.0f);
    assert(number(spread, ElementField::ShadowOffsetX) == -6.0f);
    assert(flag(spread, ElementField::ShadowInset));
    assert(flag(spread, ElementField::ShadowEnabled));

    runtime.setElementField("page.panel", ElementField::GradientStart,
                            fieldValueOf(core::Color{1.0f, 0.0f, 0.0f, 1.0f}));
    settle(runtime);
    const ElementValues gradient = runtime.elementValues("page.panel");
    assert(flag(gradient, ElementField::GradientEnabled));
    assert(color(gradient, ElementField::GradientStart).r == 1.0f);

    runtime.setElementField("page.panel", ElementField::GradientEnd,
                            fieldValueOf(core::Color{0.0f, 0.0f, 1.0f, 1.0f}));
    settle(runtime);
    assert(color(runtime.elementValues("page.panel"), ElementField::GradientEnd).b == 1.0f);

    // A switch a tool wrote can be written off again, and the fields that needed it keep
    // their own values: nothing here is coupled except the one line the table states.
    runtime.setElementField("page.panel", ElementField::GradientEnabled, fieldValueOf(false));
    runtime.setElementField("page.panel", ElementField::ShadowEnabled, fieldValueOf(false));
    settle(runtime);
    const ElementValues off = runtime.elementValues("page.panel");
    assert(!flag(off, ElementField::GradientEnabled));
    assert(!flag(off, ElementField::ShadowEnabled));
    assert(number(off, ElementField::ShadowBlur) == 9.0f);

    // Patches are per element: a second element keeps its own values.
    runtime.setElementField("page.label", ElementField::TextColor,
                            fieldValueOf(core::Color{0.1f, 1.0f, 0.2f, 1.0f}));
    settle(runtime);
    assert(color(runtime.elementValues("page.label"), ElementField::TextColor).g == 1.0f);
    assert(color(runtime.elementValues("page.panel"), ElementField::TextColor).g ==
           color(panel, ElementField::TextColor).g);
    assert(runtime.elementPatchCount() == 2);

    // Putting one field back leaves the other patches alone, and the element has to be
    // composed again for its own value to come back.
    runtime.clearElementField("page.panel", ElementField::Radius);
    composePage(runtime);
    settle(runtime);
    const ElementValues restored = runtime.elementValues("page.panel");
    assert(number(restored, ElementField::Radius) == 4.0f);
    assert(color(restored, ElementField::Color).r == 1.0f);

    // An id the page does not have cannot be written to, and clearing one does not
    // disturb the elements that are there.
    runtime.setElementField("page.missing", ElementField::Radius, fieldValueOf(3.0f));
    settle(runtime);
    assert(number(runtime.elementValues("page.panel"), ElementField::Radius) == 4.0f);
    runtime.clearElementFields("page.missing");
    runtime.clearElementFields("page.panel");
    composePage(runtime);
    settle(runtime);
    assert(color(runtime.elementValues("page.panel"), ElementField::Color).r == 0.2f);
    assert(runtime.elementPatchCount() == 1);   // the label still carries a patch

    // The footer's reset drops every patch at once.
    runtime.setElementField("page.label", ElementField::Opacity, fieldValueOf(0.5f));
    settle(runtime);
    assert(runtime.elementPatchCount() == 1);
    runtime.clearElementFields();
    assert(runtime.elementPatchCount() == 0);
    composePage(runtime);
    settle(runtime);
    const ElementValues afterReset = runtime.elementValues("page.label");
    assert(color(afterReset, ElementField::TextColor).g == 0.9f);
    assert(afterReset.written == 0);

    runtime.shutdown(false);
    return 0;
}

#else

int main() {
    // A build without tooling carries the field table but no place to keep what a tool
    // wrote: every entry point is inert and the patch count stays zero.
    core::dsl::Runtime runtime;
    assert(core::dsl::runtime::kElementFieldCount > 0);
    assert(runtime.elementPatchCount() == 0);
    assert(runtime.elementValues("page.panel").written == 0);
    runtime.setElementField("page.panel", core::dsl::runtime::ElementField::Radius,
                            core::dsl::runtime::fieldValueOf(2.0f));
    assert(runtime.elementPatchCount() == 0);
    runtime.shutdown(false);
    return 0;
}

#endif
