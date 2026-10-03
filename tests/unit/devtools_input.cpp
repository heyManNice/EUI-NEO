#include "modules/devtools/input.h"
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
        ui.stack("root")
            .size(screen.width, screen.height)
            .content([&] {
                // Background layer
                ui.rect("bg")
                    .size(screen.width, screen.height)
                    .interactive(false)
                    .build();

                // Button (interactive)
                ui.rect("btn_submit")
                    .position(50.0f, 50.0f)
                    .size(120.0f, 40.0f)
                    .interactive(true)
                    .focusable(true)
                    .build();

                // Mask overlay on top covering (40, 40) to (190, 120)
                ui.rect("mask_overlay")
                    .position(40.0f, 40.0f)
                    .size(150.0f, 80.0f)
                    .interactive(true)
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

    // 1. Initial snapshot check
    auto snapshot = modules::devtools::captureInputSnapshot(runtime);
    assert(snapshot.totalEventsCount == 0);

    // 2. Hit-test chain test at (60, 60), where mask_overlay, btn_submit and bg overlap!
    const auto chain = runtime.hitTestChain(60.0f, 60.0f, 1.0f);
    assert(!chain.empty());
    // mask_overlay was added last, so it is topmost!
    assert(chain[0].id == "page.mask_overlay");
    assert(chain[0].interactive);

    // Next should be btn_submit
    bool foundBtn = false;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        if (chain[i].id == "page.btn_submit") {
            foundBtn = true;
            break;
        }
    }
    assert(foundBtn);

    // 3. Move pointer to (60, 60) by dispatching pointer event
    core::PointerEvent moveEvent;
    moveEvent.x = 60.0;
    moveEvent.y = 60.0;
    moveEvent.action = core::PointerAction::Move;
    runtime.pushPointerEvent(moveEvent);
    runtime.update(nullptr, 0.016f, 1.0f, 1.0f);

    snapshot = modules::devtools::captureInputSnapshot(runtime);
    assert(!snapshot.hitChain.empty());
    assert(snapshot.hitChain[0].id == "page.mask_overlay");
    assert(snapshot.hitChain[0].isTopmostTarget);

    // btn_submit underneath should be marked isBlocked!
    bool btnIsBlocked = false;
    for (const auto& item : snapshot.hitChain) {
        if (item.id == "page.btn_submit") {
            assert(item.isBlocked);
            btnIsBlocked = true;
        }
    }
    assert(btnIsBlocked);

    // 4. Test Pointer press & release event recording
    core::PointerEvent pressEvent;
    pressEvent.x = 60.0;
    pressEvent.y = 60.0;
    pressEvent.action = core::PointerAction::Press;
    pressEvent.button = core::PointerButton::Left;
    runtime.pushPointerEvent(pressEvent);

    core::PointerEvent releaseEvent;
    releaseEvent.x = 60.0;
    releaseEvent.y = 60.0;
    releaseEvent.action = core::PointerAction::Release;
    releaseEvent.button = core::PointerButton::Left;
    runtime.pushPointerEvent(releaseEvent);
    runtime.update(nullptr, 0.016f, 1.0f, 1.0f);

    snapshot = modules::devtools::captureInputSnapshot(runtime);
    assert(snapshot.totalEventsCount >= 1);
    assert(!snapshot.events.empty());

    // 5. Test Compose Input Tab UI & Button interactions
    bool categoryChanged = false;
    modules::devtools::InputCategory selectedCat = modules::devtools::InputCategory::All;
    bool clearHistoryCalled = false;

    core::dsl::Runtime devtoolsUiRuntime;
    devtoolsUiRuntime.compose("devtools_input", 800.0f, 600.0f, [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        modules::devtools::DevtoolsPanelState panelState;
        modules::devtools::DevtoolsUiState uiState;
        uiState.width = screen.width;
        uiState.height = screen.height;
        uiState.panel = core::Rect{0.0f, 0.0f, screen.width, screen.height};
        uiState.panelState = &panelState;
        uiState.input = &snapshot;

        modules::devtools::DevtoolsUiActions actions;
        actions.input.setCategory = [&](modules::devtools::InputCategory cat) {
            categoryChanged = true;
            selectedCat = cat;
        };
        actions.input.clearHistory = [&]() {
            clearHistoryCalled = true;
        };
        modules::devtools::composeInputTab(ui, uiState, actions);
    });
    devtoolsUiRuntime.update(nullptr, 0.016f, 1.0f, 1.0f);

    assert(devtoolsUiRuntime.findElement("devtools_input.input.main") != nullptr);
    assert(devtoolsUiRuntime.findElement("devtools_input.input.workbench") != nullptr);
    assert(devtoolsUiRuntime.findElement("devtools_input.input.hitchain.col") != nullptr);
    assert(devtoolsUiRuntime.findElement("devtools_input.input.stream.col") != nullptr);

    // Verify category button clicking via pointer interaction
    const auto* catPointerBg = devtoolsUiRuntime.findElement("devtools_input.input.cat.Pointer.bg");
    assert(catPointerBg != nullptr);
    assert(catPointerBg->onClick != nullptr);
    core::PointerEvent pressCat;
    pressCat.x = catPointerBg->frame.x + 5.0;
    pressCat.y = catPointerBg->frame.y + 5.0;
    pressCat.action = core::PointerAction::Press;
    pressCat.button = core::PointerButton::Left;
    devtoolsUiRuntime.pushPointerEvent(pressCat);
    core::PointerEvent releaseCat;
    releaseCat.x = pressCat.x;
    releaseCat.y = pressCat.y;
    releaseCat.action = core::PointerAction::Release;
    releaseCat.button = core::PointerButton::Left;
    devtoolsUiRuntime.pushPointerEvent(releaseCat);
    devtoolsUiRuntime.update(nullptr, 0.016f, 1.0f, 1.0f);
    assert(categoryChanged);
    assert(selectedCat == modules::devtools::InputCategory::Pointer);

    // Verify clear log button clicking via pointer interaction
    const auto* clearBtnBg = devtoolsUiRuntime.findElement("devtools_input.input.clear.btn.bg");
    assert(clearBtnBg != nullptr);
    assert(clearBtnBg->onClick != nullptr);
    core::PointerEvent pressClear;
    pressClear.x = clearBtnBg->frame.x + 5.0;
    pressClear.y = clearBtnBg->frame.y + 5.0;
    pressClear.action = core::PointerAction::Press;
    pressClear.button = core::PointerButton::Left;
    devtoolsUiRuntime.pushPointerEvent(pressClear);
    core::PointerEvent releaseClear;
    releaseClear.x = pressClear.x;
    releaseClear.y = pressClear.y;
    releaseClear.action = core::PointerAction::Release;
    releaseClear.button = core::PointerButton::Left;
    devtoolsUiRuntime.pushPointerEvent(releaseClear);
    devtoolsUiRuntime.update(nullptr, 0.016f, 1.0f, 1.0f);
    assert(clearHistoryCalled);

    std::cout << "devtools_input unit test passed!" << std::endl;
    return 0;
}

#else

int main() {
    std::cout << "Devtools tooling disabled, skipping test." << std::endl;
    return 0;
}

#endif
