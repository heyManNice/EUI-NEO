#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/dsl.h"
#include "core/runtime/runtime_inspector.h"
#include "modules/devtools/devtools_ui.h"

#include <cstddef>
#include <vector>

namespace modules::devtools {

// The element name a tree row and the property area both print.
const char* elementKindName(core::dsl::ElementKind kind);

// Every property the area can edit, in the order it shows them. The area has to
// cover what the runtime can write, once each, so tests compare this against
// `kDebugPropertyCount` instead of trusting the table.
const std::vector<core::dsl::runtime::DebugPropertyId>& elementPropertyIds();

// Everything the property area needs for one composition. Values belong to the host,
// the panel only reads them, like the element tree.
struct ElementPropertiesState {
    const core::dsl::runtime::DebugElementProperties* properties = nullptr;
    std::size_t overrideCount = 0;
};

// Composes the property area of the Elements tab: the values of the element the user
// selected, one editable row per property, and the way back to the element's own
// values. `area` is the region the areas may use, in panel coordinates.
void composeElementProperties(core::dsl::Ui& ui,
                              const std::string& id,
                              const core::Rect& area,
                              const ElementPropertiesState& properties,
                              const DevtoolsUiState& state,
                              const DevtoolsUiActions& actions);

} // namespace modules::devtools

#endif
