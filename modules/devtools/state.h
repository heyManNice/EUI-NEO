#pragma once

#include "core/dsl.h"
#include "core/dsl_runtime.h"
#include "modules/devtools/ui.h"

#include <cstddef>
#include <string>
#include <vector>

namespace modules::devtools {

enum class InstanceKind {
    Rect,
    Polygon,
    Text,
    Image,
    ShaderToy,
    Interaction,
    DirtyKey,
    Layout,
    ScrollState,
    SliderState,
    Timer,
    DependentVisualState,
    FrameTarget,
    PaintBounds,
    RetainedLayer
};

const char* stateCategoryName(StateCategory category);
const char* instanceKindName(InstanceKind kind);

struct InstanceEntry {
    std::string id;
    InstanceKind kind = InstanceKind::Rect;
    StateCategory category = StateCategory::Primitives;
    bool seen = false;
    bool initialized = false;
    std::string summary;
    std::string details;
};

struct InstanceStateSnapshot {
    std::vector<InstanceEntry> entries;
    std::size_t totalCount = 0;
    std::size_t seenCount = 0;
    std::size_t unseenCount = 0;
    std::size_t scrollCount = 0;
    std::size_t sliderCount = 0;
    std::size_t timerCount = 0;
    std::size_t interactionCount = 0;
    std::size_t primitiveCount = 0;
    std::size_t layoutCount = 0;
    std::size_t layerCount = 0;
};

InstanceStateSnapshot captureInstanceState(const core::dsl::Runtime& page);

void composeStateTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
