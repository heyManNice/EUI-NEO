#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// Property overrides are what the panel's property area writes. They live outside the
// app's state, so this test is about the promises the runtime makes: a value shows up
// in the next read, survives a compose, and clearing it gives the element's own value
// back without the app ever knowing.
#if defined(EUI_DEBUG_BUILD)

namespace {

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

} // namespace

int main() {
    using core::dsl::runtime::DebugPropertyId;
    using core::dsl::runtime::DebugPropertyType;
    using core::dsl::runtime::debugPropertyBit;
    using core::dsl::runtime::debugPropertyType;
    using core::dsl::runtime::DebugElementProperties;

    core::dsl::Runtime runtime;
    composePage(runtime);
    settle(runtime);

    const DebugElementProperties panel = runtime.debugElementProperties("page.panel");
    assert(panel.active);
    assert(panel.id == "page.panel");
    assert(panel.kind == core::dsl::ElementKind::Rect);
    assert(panel.frame.width == 120.0f && panel.frame.height == 60.0f);
    assert(panel.color.r == 0.2f && panel.color.g == 0.4f && panel.color.b == 0.6f);
    assert(panel.radius == 4.0f);
    assert(!panel.shadow.enabled);
    assert(panel.overridden == 0);

    // Nothing is overridden until a tool writes, and an element the page does not have
    // simply has no properties to show.
    assert(runtime.debugElementOverrideCount() == 0);
    assert(!runtime.debugElementProperties("page.missing").active);

    // Writing shows up in the next read, and it is not a structural change: the app
    // composed the same tree as before.
    const std::uint64_t revision = runtime.elementStructureRevision();
    runtime.setDebugElementOverride("page.panel", DebugPropertyId::Radius, 12.0f);
    runtime.setDebugElementOverride("page.panel", DebugPropertyId::Color, core::Color{1.0f, 0.0f, 0.0f, 1.0f});
    settle(runtime);
    const DebugElementProperties overridden = runtime.debugElementProperties("page.panel");
    assert(overridden.radius == 12.0f);
    assert(overridden.color.r == 1.0f && overridden.color.g == 0.0f);
    assert((overridden.overridden & debugPropertyBit(DebugPropertyId::Radius)) != 0);
    assert((overridden.overridden & debugPropertyBit(DebugPropertyId::Color)) != 0);
    assert((overridden.overridden & debugPropertyBit(DebugPropertyId::Opacity)) == 0);
    assert(runtime.debugElementOverrideCount() == 1);
    assert(runtime.elementStructureRevision() == revision);

    // A compose rebuilds every element from the app's code, so the runtime applies its
    // store again to the fresh tree: that is what makes an edit survive the app.
    composePage(runtime);
    settle(runtime);
    const DebugElementProperties recomposed = runtime.debugElementProperties("page.panel");
    assert(recomposed.radius == 12.0f);
    assert(recomposed.color.r == 1.0f);
    assert(recomposed.color.b == 0.0f);

    // Editing one shadow field switches the shadow on as well, so the value can be seen
    // at all, and the switch reports itself as overridden.
    runtime.setDebugElementOverride("page.panel", DebugPropertyId::ShadowBlur, 9.0f);
    settle(runtime);
    const DebugElementProperties shadowed = runtime.debugElementProperties("page.panel");
    assert(shadowed.shadow.enabled);
    assert(shadowed.shadow.blur == 9.0f);
    assert((shadowed.overridden & debugPropertyBit(DebugPropertyId::ShadowEnabled)) != 0);

    runtime.setDebugElementOverride("page.panel", DebugPropertyId::ShadowEnabled, false);
    settle(runtime);
    assert(!runtime.debugElementProperties("page.panel").shadow.enabled);

    // Overrides are per element: a second element keeps its own values.
    runtime.setDebugElementOverride("page.label", DebugPropertyId::TextColor, core::Color{0.1f, 1.0f, 0.2f, 1.0f});
    settle(runtime);
    assert(runtime.debugElementProperties("page.label").textColor.g == 1.0f);
    assert(runtime.debugElementProperties("page.panel").textColor.g == panel.textColor.g);
    assert(runtime.debugElementOverrideCount() == 2);

    // The property type tells an editor which control a property needs.
    assert(debugPropertyType(DebugPropertyId::Color) == DebugPropertyType::Color);
    assert(debugPropertyType(DebugPropertyId::TextColor) == DebugPropertyType::Color);
    assert(debugPropertyType(DebugPropertyId::ShadowColor) == DebugPropertyType::Color);
    assert(debugPropertyType(DebugPropertyId::Blur) == DebugPropertyType::Number);
    assert(debugPropertyType(DebugPropertyId::ShadowOffsetY) == DebugPropertyType::Number);
    assert(debugPropertyType(DebugPropertyId::ShadowEnabled) == DebugPropertyType::Flag);

    // Putting one property back leaves the other overrides alone, and the element has
    // to be composed again for its own value to come back.
    runtime.clearDebugElementOverride("page.panel", DebugPropertyId::Radius);
    composePage(runtime);
    settle(runtime);
    const DebugElementProperties restored = runtime.debugElementProperties("page.panel");
    assert(restored.radius == 4.0f);
    assert(restored.color.r == 1.0f);

    // An id the page does not have cannot be written to, and it does not disturb the
    // elements that are there.
    runtime.setDebugElementOverride("page.missing", DebugPropertyId::Radius, 3.0f);
    settle(runtime);
    assert(runtime.debugElementProperties("page.panel").radius == 4.0f);
    runtime.clearDebugElementOverrides("page.missing");
    runtime.clearDebugElementOverrides("page.panel");
    composePage(runtime);
    settle(runtime);
    assert(runtime.debugElementProperties("page.panel").color.r == 0.2f);
    assert(runtime.debugElementOverrideCount() == 1);   // the label is still overridden

    // The footer's reset drops every override at once.
    runtime.setDebugElementOverride("page.label", DebugPropertyId::Opacity, 0.5f);
    settle(runtime);
    assert(runtime.debugElementOverrideCount() == 1);
    runtime.clearAllDebugElementOverrides();
    assert(runtime.debugElementOverrideCount() == 0);
    composePage(runtime);
    settle(runtime);
    const DebugElementProperties afterReset = runtime.debugElementProperties("page.label");
    assert(afterReset.textColor.g == 0.9f);
    assert(afterReset.overridden == 0);

    runtime.shutdown(false);
    return 0;
}

#else

int main() {
    // Release builds carry neither the property overrides nor the panel that writes
    // them, so there is nothing to check here.
    core::dsl::Runtime runtime;
    runtime.shutdown(false);
    return 0;
}

#endif
