#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "modules/devtools/devtools_ui.h"

namespace modules::devtools {

// Elements tab: the app page tree as the app layer published it, plus the
// details of the selected element.
void composeElementsTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools

#endif
