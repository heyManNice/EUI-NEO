#include "modules/devtools/mcp_semantic.h"

#include "modules/devtools/fields.h"
#include "modules/devtools/properties.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace modules::devtools {

namespace {

std::string escapeJson(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if (static_cast<unsigned char>(c) < 0x20) {
            ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            ss << c;
        }
    }
    return ss.str();
}

std::string findSubtreeText(const core::dsl::Element& element, int depth = 0) {
    if (depth > 8) return {};
    if (element.kind == core::dsl::ElementKind::Text && !element.text.empty()) {
        return element.text;
    }
    for (const auto* child : element.orderedChildren) {
        if (child != nullptr) {
            std::string t = findSubtreeText(*child, depth + 1);
            if (!t.empty()) return t;
        }
    }
    return {};
}

void collectInteractiveElementsRecursive(const core::dsl::Element& element,
                                         std::vector<McpInteractiveElement>& result,
                                         int& nextIndex,
                                         bool onlyVisible) {
    if (onlyVisible) {
        if (element.frame.width <= 0.0f || element.frame.height <= 0.0f) {
            return;
        }
    }

    const bool isInteractive = element.interactive ||
                               static_cast<bool>(element.onClick) ||
                               element.focusable ||
                               !element.sliderInputSourceId.empty();

    if (isInteractive) {
        McpInteractiveElement item;
        item.markIndex = nextIndex++;
        item.id = element.id;
        item.kind = elementKindName(element.kind);
        item.text = findSubtreeText(element);
        item.frame = core::Rect{element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        item.disabled = element.disabled;
        item.focusable = element.focusable;
        result.push_back(std::move(item));
    }

    for (const auto* child : element.orderedChildren) {
        if (child != nullptr) {
            collectInteractiveElementsRecursive(*child, result, nextIndex, onlyVisible);
        }
    }
}

void formatElementTreeJsonRecursive(const core::dsl::Element& element,
                                    std::ostringstream& ss,
                                    int depth,
                                    int maxDepth) {
    ss << "{\"id\":\"" << escapeJson(element.id) << "\","
       << "\"kind\":\"" << elementKindName(element.kind) << "\","
       << "\"frame\":[" << element.frame.x << "," << element.frame.y << ","
       << element.frame.width << "," << element.frame.height << "],"
       << "\"interactive\":" << (element.interactive ? "true" : "false");

    if (element.kind == core::dsl::ElementKind::Text && !element.text.empty()) {
        ss << ",\"text\":\"" << escapeJson(element.text) << "\"";
    }

    if (depth < maxDepth && !element.orderedChildren.empty()) {
        ss << ",\"children\":[";
        bool first = true;
        for (const auto* child : element.orderedChildren) {
            if (child == nullptr) continue;
            if (!first) ss << ",";
            first = false;
            formatElementTreeJsonRecursive(*child, ss, depth + 1, maxDepth);
        }
        ss << "]";
    } else {
        ss << ",\"children\":[]";
    }
    ss << "}";
}

} // namespace

std::vector<McpInteractiveElement> extractInteractiveElements(const core::dsl::Runtime& runtime, bool onlyVisible) {
    std::vector<McpInteractiveElement> result;
    int nextIndex = 1;
    const std::vector<const core::dsl::Element*>& roots = runtime.elementRoots();
    for (const auto* root : roots) {
        if (root != nullptr) {
            collectInteractiveElementsRecursive(*root, result, nextIndex, onlyVisible);
        }
    }
    return result;
}

std::string formatInteractiveElementsJson(const std::vector<McpInteractiveElement>& elements) {
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& item : elements) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"markIndex\":" << item.markIndex
           << ",\"id\":\"" << escapeJson(item.id) << "\""
           << ",\"kind\":\"" << escapeJson(item.kind) << "\""
           << ",\"text\":\"" << escapeJson(item.text) << "\""
           << ",\"bounds\":[" << item.frame.x << "," << item.frame.y << "," << item.frame.width << "," << item.frame.height << "]"
           << ",\"disabled\":" << (item.disabled ? "true" : "false")
           << ",\"focusable\":" << (item.focusable ? "true" : "false")
           << "}";
    }
    ss << "]";
    return ss.str();
}

std::string extractElementTreeJson(const core::dsl::Runtime& runtime, int maxDepth) {
    std::ostringstream ss;
    ss << "{\"roots\":[";
    bool first = true;
    const std::vector<const core::dsl::Element*>& roots = runtime.elementRoots();
    for (const auto* root : roots) {
        if (root == nullptr) continue;
        if (!first) ss << ",";
        first = false;
        formatElementTreeJsonRecursive(*root, ss, 0, maxDepth);
    }
    ss << "]}";
    return ss.str();
}

std::string extractElementDetailsJson(const core::dsl::Runtime& runtime, const std::string& elementId) {
    const core::dsl::Element* el = runtime.findElement(elementId);
    if (el == nullptr) {
        return "{\"error\":\"Element not found\"}";
    }

    const ElementValues vals = readElementValues(runtime, elementId, 0);
    std::ostringstream ss;
    ss << "{\"id\":\"" << escapeJson(el->id) << "\","
       << "\"kind\":\"" << elementKindName(el->kind) << "\","
       << "\"frame\":[" << el->frame.x << "," << el->frame.y << "," << el->frame.width << "," << el->frame.height << "],"
       << "\"interactive\":" << (el->interactive ? "true" : "false") << ","
       << "\"disabled\":" << (el->disabled ? "true" : "false") << ","
       << "\"zIndex\":" << el->zIndex << ","
       << "\"clip\":" << (el->clip ? "true" : "false") << ","
       << "\"text\":\"" << escapeJson(vals.text) << "\","
       << "\"fields\":{";

    bool first = true;
    const auto appendField = [&](const char* name, const FieldValue& fieldVal) {
        if (!first) ss << ",";
        first = false;
        ss << "\"" << escapeJson(name) << "\":";
        switch (fieldVal.kind) {
            case FieldKind::Flag:
                ss << (fieldVal.flag ? "true" : "false");
                break;
            case FieldKind::Number:
                ss << fieldVal.number;
                break;
            case FieldKind::Color:
                ss << "{\"r\":" << fieldVal.color.r << ",\"g\":" << fieldVal.color.g
                   << ",\"b\":" << fieldVal.color.b << ",\"a\":" << fieldVal.color.a << "}";
                break;
        }
    };

#define MCP_EXTRACT_FIELD(fieldName, fieldKindVal, memberName) \
    appendField(#fieldName, vals.field(ElementField::fieldName));
    DEVTOOLS_ELEMENT_FIELD_TABLE(MCP_EXTRACT_FIELD)
#undef MCP_EXTRACT_FIELD

    ss << "}}";
    return ss.str();
}

} // namespace modules::devtools
