#pragma once

#include "modules/devtools/devtools_ui.h"

namespace modules::devtools {

void composePerformanceTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
