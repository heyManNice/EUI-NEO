#include "eui/detail/dsl_app_impl.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

// The seam between a debug tool and the page: the tool asks for the values of the
// element it shows, and the app layer is the only writer of a property edit. Both
// directions live in the app layer, so this drives them with a recording overlay
// instead of a panel, in a tiny application of its own: what a panel would do is
// exactly what the overlay does here.
// The app layer is header-only and expects an application to provide these two entry
// points, so this test is one: a page with a single element.
namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Property Bridge")
        .pageId("page")
        .windowSize(200, 100);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen&) {
    ui.rect("panel")
        .size(80.0f, 40.0f)
        .color(eui::Color{0.2f, 0.4f, 0.6f, 1.0f})
        .radius(3.0f)
        .build();
}

} // namespace app

#if defined(EUI_DEBUG_BUILD)

namespace {

class RecordingOverlay : public app::detail::OverlayHost {
public:
    bool handleHotkey(const core::KeyEvent&) override { return false; }
    core::Rect contentBounds() const override { return {}; }
    void filterInput(std::vector<core::PointerEvent>&, core::ScrollEvent&) override {}
    bool update(int, int, float, float) override { return false; }
    void render(int, int, float, const core::Rect*) override {}
    void releaseGraphicsResources() override {}
    void shutdown() override {}
    void describeDetachedWindow(app::detail::DetachedWindowOptions&) const override {}
    void setDetachedWindowOpener(std::function<void()>) override {}
    void setDetachedWindowCloser(std::function<void()>) override {}
    void composeDetached(core::dsl::Ui&, const core::dsl::Screen&) override {}
    void detachedWindowClosed() override {}

    const std::string& propertiesElement() const override { return element; }
    void setElementProperties(const core::dsl::runtime::DebugElementProperties& value) override {
        properties = value;
        ++publishCount;
    }
    bool takeElementPropertyEdit(ElementPropertyEdit& edit) override {
        if (pending.empty()) {
            return false;
        }
        edit = pending.front();
        pending.erase(pending.begin());
        return true;
    }
    void setElementPropertyOverrideCount(std::size_t count) override { overrideCount = count; }

    std::string element;
    core::dsl::runtime::DebugElementProperties properties;
    std::vector<ElementPropertyEdit> pending;
    int publishCount = 0;
    std::size_t overrideCount = 0;
};

void composePage() {
    app::detail::dslRuntime().compose("page", 200.0f, 100.0f, app::compose);
    app::detail::dslRuntime().update(nullptr, 1.0f / 60.0f, 1.0f, 1.0f);
}

} // namespace

int main() {
    using core::dsl::runtime::DebugPropertyId;
    using Edit = app::detail::OverlayHost::ElementPropertyEdit;

    core::dsl::Runtime& page = app::detail::dslRuntime();
    composePage();

    RecordingOverlay overlay;

    // An overlay that shows nothing asks for nothing, so nothing is read for it.
    app::detail::publishElementProperties(overlay);
    assert(overlay.publishCount == 0);
    assert(!overlay.properties.active);

    // An overlay that shows an element gets that one element. The read is throttled
    // while the shown element and the page structure stay the same.
    overlay.element = "page.panel";
    app::detail::publishElementProperties(overlay);
    assert(overlay.publishCount == 1);
    assert(overlay.properties.active);
    assert(overlay.properties.id == "page.panel");
    assert(overlay.properties.radius == 3.0f);
    assert(overlay.properties.color.r == 0.2f);
    assert(overlay.overrideCount == 0);
    app::detail::publishElementProperties(overlay);
    assert(overlay.publishCount == 1);

    // An edit the overlay made lands on the page, the panel sees the result in the
    // same frame instead of waiting for the throttle, and the count it shows follows.
    Edit edit;
    edit.id = "page.panel";
    edit.property = DebugPropertyId::Radius;
    edit.number = 12.0f;
    overlay.pending.push_back(edit);
    app::detail::applyElementPropertyEdits(overlay);
    assert(page.debugElementOverrideCount() == 1);
    assert(page.debugElementProperties("page.panel").radius == 12.0f);
    assert(overlay.overrideCount == 1);
    // The app layer publishes again right after an edit, so the tool never shows the
    // value it just changed away from.
    app::detail::publishElementProperties(overlay);
    assert(overlay.publishCount == 2);
    assert(overlay.properties.radius == 12.0f);

    // Colours and flags take their own overload, and the read reports which property
    // a debug session replaced.
    edit.property = DebugPropertyId::Color;
    edit.color = core::Color{0.9f, 0.1f, 0.2f, 1.0f};
    edit.number = 0.0f;
    overlay.pending.push_back(edit);
    edit.property = DebugPropertyId::ShadowEnabled;
    edit.flag = true;
    overlay.pending.push_back(edit);
    app::detail::applyElementPropertyEdits(overlay);
    app::detail::publishElementProperties(overlay);
    const core::dsl::runtime::DebugElementProperties edited = page.debugElementProperties("page.panel");
    assert(edited.color.r == 0.9f && edited.color.b == 0.2f);
    assert(edited.shadow.enabled);
    assert((edited.overridden & core::dsl::runtime::debugPropertyBit(DebugPropertyId::Color)) != 0);
    assert(overlay.properties.color.r == 0.9f);

    // Putting one property back gives the element its own value again, which is the
    // app's value: the override was never written into app state.
    edit = {};
    edit.id = "page.panel";
    edit.property = DebugPropertyId::Radius;
    edit.clear = true;
    overlay.pending.push_back(edit);
    app::detail::applyElementPropertyEdits(overlay);
    composePage();
    assert(page.debugElementProperties("page.panel").radius == 3.0f);

    // An empty id with `clear` puts every element on the page back.
    edit = {};
    edit.clear = true;
    overlay.pending.push_back(edit);
    app::detail::applyElementPropertyEdits(overlay);
    assert(page.debugElementOverrideCount() == 0);
    assert(overlay.overrideCount == 0);
    composePage();
    const core::dsl::runtime::DebugElementProperties restored = page.debugElementProperties("page.panel");
    assert(restored.color.r == 0.2f);
    assert(!restored.shadow.enabled);
    assert(restored.overridden == 0);

    page.shutdown(false);
    return 0;
}

#else

int main() {
    // Release builds carry neither the property hooks nor the tools that drive them.
    core::dsl::Runtime runtime;
    runtime.shutdown(false);
    return 0;
}

#endif
