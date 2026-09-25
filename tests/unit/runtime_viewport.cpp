#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
    core::dsl::Runtime runtime;
    core::dsl::Ui* composedUi = nullptr;
    int clicks = 0;
    const auto compose = [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        composedUi = &ui;
        assert(screen.width == 500.0f);
        assert(screen.height == 600.0f);
        ui.rect("content")
            .size(120.0f, 80.0f)
            .onClick([&] { ++clicks; })
            .build();
        ui.rect("overflow")
            .position(-20.0f, 0.0f)
            .size(30.0f, 30.0f)
            .onClick([&] { ++clicks; })
            .build();
    };

    runtime.compose("test", core::Rect{100.0f, 0.0f, 500.0f, 600.0f}, compose);
    assert(composedUi->roots().size() == 2);
    assert(composedUi->roots().front()->id == "test.content");
    assert(composedUi->roots().front()->frame.x == 100.0f);
    assert(composedUi->roots().front()->frame.width == 120.0f);
    assert(composedUi->roots().back()->frame.x == 80.0f);

    const auto click = [&](double x) {
        core::queuePointerButton(nullptr, x, 20.0, core::PointerButton::Left,
                                 core::PointerAction::Press, {});
        runtime.update(nullptr, 0.0f, 1.0f, 1.0f);
        core::queuePointerButton(nullptr, x, 20.0, core::PointerButton::Left,
                                 core::PointerAction::Release, {});
        runtime.update(nullptr, 0.0f, 1.0f, 1.0f);
    };
    click(85.0);
    assert(clicks == 0);
    click(120.0);
    assert(clicks == 1);

    runtime.compose("test", core::Rect{0.0f, 0.0f, 500.0f, 600.0f}, compose);
    assert(composedUi->roots().size() == 2);
    assert(composedUi->roots().front()->frame.x == 0.0f);
    runtime.shutdown(false);
    core::detail::pointerStates().erase(nullptr);
    return 0;
}
