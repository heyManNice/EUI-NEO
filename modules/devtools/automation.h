#pragma once

#include "modules/devtools/ui.h"

namespace modules::devtools {

void composeAutomationTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

inline void composeMcpTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    composeAutomationTab(ui, state, actions);
}

} // namespace modules::devtools
