#pragma once

#include "core/dsl.h"
#include "core/dsl_runtime.h"
#include "modules/devtools/mcp_semantic.h"

#include <string>
#include <vector>

namespace modules::devtools {

struct McpActionResult {
    bool success = false;
    std::string message;
    core::Rect targetBounds{};
};

// Clicks on an element by its ID (dispatches PointerMove, PointerPress, and PointerRelease at element center)
McpActionResult clickElement(core::dsl::Runtime& runtime, const std::string& elementId);

// Clicks on an element by its Set-of-Mark index from a list of interactive elements
McpActionResult clickMark(core::dsl::Runtime& runtime, int markIndex, const std::vector<McpInteractiveElement>& marks);

// Injects text input into an element (or active focused element)
McpActionResult inputText(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& text);

// Injects a scroll delta at a specific point or on the active element
McpActionResult scrollElement(core::dsl::Runtime& runtime, const std::string& elementId, float deltaX, float deltaY);

} // namespace modules::devtools
