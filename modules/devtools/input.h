#pragma once

#include "core/dsl.h"
#include "core/dsl_runtime.h"
#include "modules/devtools/ui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace modules::devtools {

const char* inputCategoryName(InputCategory cat);

struct InputEventItem {
    double timestamp = 0.0;
    core::dsl::runtime::ToolingInputKind kind = core::dsl::runtime::ToolingInputKind::PointerPress;
    std::string targetId;
    std::string detail;
    float x = 0.0f;
    float y = 0.0f;
    core::PointerButton button = core::PointerButton::None;
    core::InputKey key = core::InputKey::Unknown;
    std::string text;
};

struct HitChainItem {
    std::string id;
    core::dsl::ElementKind kind = core::dsl::ElementKind::Row;
    bool interactive = false;
    bool disabled = false;
    bool focusable = false;
    core::dsl::HitTestMode hitTestMode = core::dsl::HitTestMode::Layout;
    core::Rect frame;
    bool isTopmostTarget = false;
    bool isBlocked = false;
};

struct InputSnapshot {
    std::string focusedId;
    std::string hoverTargetId;
    std::string capturedId;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    std::uint64_t totalEventsCount = 0;
    std::vector<InputEventItem> events;
    std::vector<HitChainItem> hitChain;
};

InputSnapshot captureInputSnapshot(const core::dsl::Runtime& page);

void composeInputTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
