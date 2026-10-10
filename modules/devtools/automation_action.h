#pragma once

#include "core/dsl.h"
#include "core/dsl_runtime.h"
#include "modules/devtools/automation_snapshot.h"

#include <string>
#include <vector>

namespace modules::devtools {

struct McpActionResult {
    bool success = false;
    std::string message;
    core::Rect targetBounds{};
    std::string occludedBy;
};

// Clicks on an element by its ID (dispatches PointerMove, PointerPress, and PointerRelease at element center)
McpActionResult clickElement(core::dsl::Runtime& runtime, const std::string& elementId, bool force = false);

// Clicks on an element by its Set-of-Mark index from a list of interactive elements
McpActionResult clickMark(core::dsl::Runtime& runtime, int markIndex, const std::vector<McpInteractiveElement>& marks, bool force = false);

// Injects text input into an element (or active focused element).
// mode can be "replace" (default clears existing text before inputting) or "append".
McpActionResult inputText(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& text, const std::string& mode = "replace", bool clearFirst = false);

// Dispatches a key press/release event to the target element (or focused element)
// key: "Enter", "Backspace", "Escape", "Tab", "Space", "Delete", "Up", "Down", "Left", "Right", etc.
McpActionResult pressKey(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& keyName, bool ctrl = false, bool shift = false, bool alt = false);

// Sets focus on target element
McpActionResult focusElement(core::dsl::Runtime& runtime, const std::string& elementId);

// Injects a scroll delta at a specific point or on the active element
McpActionResult scrollElement(core::dsl::Runtime& runtime, const std::string& elementId, float deltaX, float deltaY);

} // namespace modules::devtools
