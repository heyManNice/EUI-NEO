#pragma once

#include "core/dsl.h"
#include "core/app/performance_snapshot.h"

#include <functional>

namespace core::debug {

enum class DockPosition { Floating, Left, Bottom, Right };
enum class DevtoolsTab { Performance, Elements };

inline constexpr float kDevtoolsToolbarHeight = 31.0f;

struct DevtoolsUiState {
    float width = 0.0f;
    float height = 0.0f;
    Rect panel;
    bool detached = false;
    DockPosition dockPosition = DockPosition::Bottom;
    DevtoolsTab activeTab = DevtoolsTab::Performance;
    bool moreMenuOpen = false;
    float performanceScrollOffset = 0.0f;
    app::PerformanceSnapshot performance;
};

struct DevtoolsUiActions {
    std::function<void()> toggleMoreMenu;
    std::function<void()> close;
    std::function<void(DevtoolsTab)> selectTab;
    std::function<void(DockPosition)> selectDockPosition;
    std::function<void(float)> setPerformanceScrollOffset;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace core::debug
