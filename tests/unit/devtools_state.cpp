#include "modules/devtools/state.h"
#include "modules/devtools/host.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

#if defined(EUI_TOOLING)

namespace {

void composeTestPage(core::dsl::Runtime& runtime) {
    runtime.compose("page", 400.0f, 300.0f, [](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        ui.column("root")
            .size(screen.width, screen.height)
            .content([&] {
                ui.text("title").size(120.0f, 20.0f).text("State Inspection Test").build();

                // Scroll container
                ui.column("scroll_box")
                    .size(200.0f, 100.0f)
                    .scrollState("scroll_box", 50.0f, 300.0f, 100.0f)
                    .content([&] {
                        ui.rect("item1").size(180.0f, 40.0f).build();
                        ui.rect("item2").size(180.0f, 40.0f).build();
                    })
                    .build();

                // Slider
                ui.rect("slider_box")
                    .size(150.0f, 20.0f)
                    .sliderState("slider_box", 0.65f, 150.0f, 16.0f)
                    .build();
            })
            .build();
    });
}

} // namespace

int main() {
    core::dsl::Runtime runtime;
    composeTestPage(runtime);
    runtime.update(nullptr, 0.016f, 1.0f, 1.0f);

    const modules::devtools::InstanceStateSnapshot snapshot = modules::devtools::captureInstanceState(runtime);
    assert(snapshot.totalCount > 0);
    assert(snapshot.scrollCount >= 1);
    assert(snapshot.sliderCount >= 1);
    assert(snapshot.seenCount >= 3);

    // Verify scroll state inspection
    bool foundScroll = false;
    for (const auto& entry : snapshot.entries) {
        if ((entry.id == "page.scroll_box" || entry.id == "scroll_box") &&
            entry.kind == modules::devtools::InstanceKind::ScrollState) {
            foundScroll = true;
            assert(entry.category == modules::devtools::StateCategory::Scroll);
            assert(entry.seen);
            assert(entry.summary.find("50.0") != std::string::npos);
            assert(entry.summary.find("300.0") != std::string::npos);
        }
    }
    assert(foundScroll);

    // Verify slider state inspection
    bool foundSlider = false;
    for (const auto& entry : snapshot.entries) {
        if ((entry.id == "page.slider_box" || entry.id == "slider_box") &&
            entry.kind == modules::devtools::InstanceKind::SliderState) {
            foundSlider = true;
            assert(entry.category == modules::devtools::StateCategory::Slider);
            assert(entry.seen);
            assert(entry.summary.find("0.65") != std::string::npos);
        }
    }
    assert(foundSlider);

    // Verify DevtoolsHost integration
    modules::devtools::DevtoolsHost host;
    host.attach(&runtime, {});

    // Open devtools via hotkey (F12)
    core::KeyEvent f12{};
    f12.action = core::KeyAction::Press;
    f12.key = core::InputKey::F12;
    assert(host.handleHotkey(f12));
    assert(host.visible());

    // Switch to State tab via UiActions
    assert(!host.wantsInstanceState()); // Initially on default tab

    // Drive host with a frame
    host.frame(800, 600, 1.0f, 0.016f);

    host.detach();
    host.shutdown();

    std::cout << "All devtools_state tests passed!\n";
    return 0;
}

#else

int main() {
    return 0;
}

#endif
