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
//
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
    void setElementProperties(const core::dsl::runtime::ElementValues& value) override {
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

    bool pickingElement() const override { return picking; }
    core::PointerEvent pickedPointer() const override { return pointer; }
    void setElementUnderPointer(const std::string& id) override { underPointer = id; }

    std::string element;
    core::dsl::runtime::ElementValues properties;
    std::vector<ElementPropertyEdit> pending;
    int publishCount = 0;
    std::size_t overrideCount = 0;
    bool picking = false;
    core::PointerEvent pointer;
    std::string underPointer;
};

void composePage() {
    app::detail::dslRuntime().compose("page", 200.0f, 100.0f, app::compose);
    app::detail::dslRuntime().update(nullptr, 1.0f / 60.0f, 1.0f, 1.0f);
}

} // namespace

int main() {
    using core::dsl::runtime::ElementField;
    using core::dsl::runtime::fieldValueOf;
    using Edit = app::detail::OverlayHost::ElementPropertyEdit;

    core::dsl::Runtime& page = app::detail::dslRuntime();
    composePage();

    RecordingOverlay overlay;

    // An overlay that shows nothing asks for nothing, so nothing is read for it.
    app::tooling::publishElementProperties(page, overlay);
    assert(overlay.publishCount == 0);
    assert(!overlay.properties.active);

    // An overlay that shows an element gets that one element. The read is throttled
    // while the shown element and the page structure stay the same.
    overlay.element = "page.panel";
    app::tooling::publishElementProperties(page, overlay);
    assert(overlay.publishCount == 1);
    assert(overlay.properties.active);
    assert(overlay.properties.id == "page.panel");
    assert(overlay.properties.field(ElementField::Radius).number == 3.0f);
    assert(overlay.properties.field(ElementField::Color).color.r == 0.2f);
    assert(overlay.overrideCount == 0);
    app::tooling::publishElementProperties(page, overlay);
    assert(overlay.publishCount == 1);

    // An edit the overlay made lands on the page, the panel sees the result in the
    // same frame instead of waiting for the throttle, and the count it shows follows.
    Edit edit;
    edit.id = "page.panel";
    edit.field = ElementField::Radius;
    edit.value = fieldValueOf(12.0f);
    overlay.pending.push_back(edit);
    app::tooling::applyElementPropertyEdits(page, overlay);
    assert(page.elementPatchCount() == 1);
    assert(page.elementValues("page.panel").field(ElementField::Radius).number == 12.0f);
    assert(overlay.overrideCount == 1);
    // The app layer publishes again right after an edit, so the tool never shows the
    // value it just changed away from.
    app::tooling::publishElementProperties(page, overlay);
    assert(overlay.publishCount == 2);
    assert(overlay.properties.field(ElementField::Radius).number == 12.0f);

    // A colour and a flag are the same shape of edit: the value carries its own kind, so
    // the app layer writes it without knowing which field it is. The read reports which
    // fields a session wrote.
    edit.field = ElementField::Color;
    edit.value = fieldValueOf(core::Color{0.9f, 0.1f, 0.2f, 1.0f});
    overlay.pending.push_back(edit);
    edit.field = ElementField::ShadowEnabled;
    edit.value = fieldValueOf(true);
    overlay.pending.push_back(edit);
    app::tooling::applyElementPropertyEdits(page, overlay);
    app::tooling::publishElementProperties(page, overlay);
    const core::dsl::runtime::ElementValues edited = page.elementValues("page.panel");
    assert(edited.field(ElementField::Color).color.r == 0.9f);
    assert(edited.field(ElementField::Color).color.b == 0.2f);
    assert(edited.field(ElementField::ShadowEnabled).flag);
    assert(edited.wasWritten(ElementField::Color));
    assert(overlay.properties.field(ElementField::Color).color.r == 0.9f);

    // Putting one field back gives the element its own value again, which is the app's
    // value: the patch was never written into app state.
    edit = {};
    edit.id = "page.panel";
    edit.field = ElementField::Radius;
    edit.clear = true;
    overlay.pending.push_back(edit);
    app::tooling::applyElementPropertyEdits(page, overlay);
    composePage();
    assert(page.elementValues("page.panel").field(ElementField::Radius).number == 3.0f);

    // An empty id with `clear` puts every element on the page back.
    edit = {};
    edit.clear = true;
    overlay.pending.push_back(edit);
    app::tooling::applyElementPropertyEdits(page, overlay);
    assert(page.elementPatchCount() == 0);
    assert(overlay.overrideCount == 0);
    composePage();
    const core::dsl::runtime::ElementValues restored = page.elementValues("page.panel");
    assert(restored.field(ElementField::Color).color.r == 0.2f);
    assert(!restored.field(ElementField::ShadowEnabled).flag);
    assert(restored.written == 0);

    // A picking overlay owns the pointer: the page is asked what is under it, and the
    // answer comes from the page's own hit test, so it is the element the frame drew
    // there. The panel's element is not interactive, and it is still picked.
    overlay.picking = true;
    overlay.pointer.x = 40.0;
    overlay.pointer.y = 20.0;
    app::tooling::publishPickedElement(page, overlay, 1.0f);
    assert(overlay.underPointer == "page.panel");

    // A point that hits nothing answers with nothing, and a panel that stopped picking
    // clears the answer even while the pointer stays where it was.
    overlay.pointer.x = 190.0;
    overlay.pointer.y = 90.0;
    app::tooling::publishPickedElement(page, overlay, 1.0f);
    assert(overlay.underPointer.empty());
    overlay.pointer.x = 40.0;
    overlay.pointer.y = 20.0;
    overlay.picking = false;
    app::tooling::publishPickedElement(page, overlay, 1.0f);
    assert(overlay.underPointer.empty());

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
