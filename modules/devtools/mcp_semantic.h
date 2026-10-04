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
    std::string text;             // Direct element text or overlapping label text
    std::string contextText;      // Enclosing card/container context text (e.g. card title/tags)
    std::string nearestText;      // Spatially or hierarchically nearest visible text
    float nearestDistance = 0.0f; // Pixel distance to the nearest text
    core::Rect frame{};
    bool disabled = false;
    bool focusable = false;
    bool clickable = false;
    bool textInput = false;
    bool selected = false;
};

// Extracts all interactive elements (buttons, inputs, links, clickable items) with Set-of-Mark numbering
std::vector<McpInteractiveElement> extractInteractiveElements(const core::dsl::Runtime& runtime, bool onlyVisible = true);

// Formats the interactive elements as clean JSON array
std::string formatInteractiveElementsJson(const std::vector<McpInteractiveElement>& elements);

// Returns concise text-based screen description for LLM (few hundred tokens, partitions + interactive controls + labels)
std::string describeScreen(const core::dsl::Runtime& runtime);

// Returns indentation-based text accessibility snapshot with short handles (#eN) and semantic roles
// If interactiveOnly is true, non-interactive purely layout elements are skipped
std::string takeSnapshot(const core::dsl::Runtime& runtime, bool interactiveOnly = true, int maxDepth = 16);

// Returns element tree JSON hierarchy with filtering support
std::string extractElementTreeJson(const core::dsl::Runtime& runtime, int maxDepth = 32, bool compact = false, bool interactiveOnly = false);

// Returns detailed property JSON for a specific element
std::string extractElementDetailsJson(const core::dsl::Runtime& runtime, const std::string& elementId);

} // namespace modules::devtools
