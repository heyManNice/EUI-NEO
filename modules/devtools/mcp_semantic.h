#pragma once

#include "core/dsl.h"
#include "core/dsl_runtime.h"

#include <string>
#include <vector>

namespace modules::devtools {

struct McpInteractiveElement {
    int markIndex = 0;
    std::string id;
    std::string kind;
    std::string text;
    core::Rect frame{};
    bool disabled = false;
    bool focusable = false;
};

// Extracts all interactive elements (buttons, inputs, links, clickable items) with Set-of-Mark numbering
std::vector<McpInteractiveElement> extractInteractiveElements(const core::dsl::Runtime& runtime, bool onlyVisible = true);

// Formats the interactive elements as clean JSON array
std::string formatInteractiveElementsJson(const std::vector<McpInteractiveElement>& elements);

// Returns element tree JSON hierarchy
std::string extractElementTreeJson(const core::dsl::Runtime& runtime, int maxDepth = 32);

// Returns detailed property JSON for a specific element
std::string extractElementDetailsJson(const core::dsl::Runtime& runtime, const std::string& elementId);

} // namespace modules::devtools
