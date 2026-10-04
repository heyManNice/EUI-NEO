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

struct TextNodeRecord {
    const core::dsl::Element* element = nullptr;
    std::string text;
    core::Rect frame{};
    float centerX = 0.0f;
    float centerY = 0.0f;
};

template <typename T1, typename T2>
float computeRectDistance(const T1& a, const T2& b) {
    float dx = 0.0f;
    if (a.x + a.width < b.x) dx = b.x - (a.x + a.width);
    else if (b.x + b.width < a.x) dx = a.x - (b.x + b.width);

    float dy = 0.0f;
    if (a.y + a.height < b.y) dy = b.y - (a.y + a.height);
    else if (b.y + b.height < a.y) dy = a.y - (b.y + b.height);

    return std::sqrt(dx * dx + dy * dy);
}

template <typename T1, typename T2>
float computeCenterDistance(const T1& a, const T2& b) {
    float cx1 = a.x + a.width * 0.5f;
    float cy1 = a.y + a.height * 0.5f;
    float cx2 = b.x + b.width * 0.5f;
    float cy2 = b.y + b.height * 0.5f;
    return std::sqrt((cx1 - cx2) * (cx1 - cx2) + (cy1 - cy2) * (cy1 - cy2));
}

template <typename T1, typename T2>
bool rectsIntersect(const T1& a, const T2& b) {
    return !(a.x + a.width <= b.x || b.x + b.width <= a.x ||
             a.y + a.height <= b.y || b.y + b.height <= a.y);
}

bool isPrintableSemanticText(const std::string& text) {
    if (text.empty()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c)) return true;
        // Check for 3-byte UTF-8 sequence
        if (c >= 0xE0 && c <= 0xEF && i + 2 < text.size()) {
            if (c == 0xEE || c == 0xEF) {
                i += 2;
                continue; // Skip Private Use Area (Font Awesome / glyph icons)
            }
            return true; // Valid CJK or other non-icon multibyte
        } else if (c >= 0x80) {
            return true;
        }
    }
    return false;
}

void collectSubtreeTextsRecursive(const core::dsl::Element& element,
                                  std::vector<std::string>& texts,
                                  int depth = 0) {
    if (depth > 12) return;
    if (element.kind == core::dsl::ElementKind::Text && !element.text.empty()) {
        if (isPrintableSemanticText(element.text)) {
            texts.push_back(element.text);
        }
    }
    for (const auto* child : element.orderedChildren) {
        if (child != nullptr) {
            collectSubtreeTextsRecursive(*child, texts, depth + 1);
        }
    }
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

void buildParentMapAndCollectTexts(const core::dsl::Element& element,
                                   std::unordered_map<const core::dsl::Element*, const core::dsl::Element*>& parentMap,
                                   std::vector<TextNodeRecord>& allTexts,
                                   bool onlyVisible) {
    if (element.kind == core::dsl::ElementKind::Text && !element.text.empty()) {
        if (!onlyVisible || (element.frame.width > 0.0f && element.frame.height > 0.0f)) {
            TextNodeRecord rec;
            rec.element = &element;
            rec.text = element.text;
            rec.frame = core::Rect{element.frame.x, element.frame.y, element.frame.width, element.frame.height};
            rec.centerX = element.frame.x + element.frame.width * 0.5f;
            rec.centerY = element.frame.y + element.frame.height * 0.5f;
            allTexts.push_back(std::move(rec));
        }
    }

    for (const auto* child : element.orderedChildren) {
        if (child != nullptr) {
            parentMap[child] = &element;
            buildParentMapAndCollectTexts(*child, parentMap, allTexts, onlyVisible);
        }
    }
}

bool isDescendantOf(const core::dsl::Element* candidate,
                    const core::dsl::Element* ancestor,
                    const std::unordered_map<const core::dsl::Element*, const core::dsl::Element*>& parentMap) {
    const core::dsl::Element* cur = candidate;
    while (cur != nullptr) {
        if (cur == ancestor) return true;
        auto it = parentMap.find(cur);
        cur = (it != parentMap.end()) ? it->second : nullptr;
    }
    return false;
}

void collectInteractiveElementsRecursive(const core::dsl::Element& element,
                                         std::vector<McpInteractiveElement>& result,
                                         int& nextIndex,
                                         bool onlyVisible,
                                         const std::unordered_map<const core::dsl::Element*, const core::dsl::Element*>& parentMap,
                                         const std::vector<TextNodeRecord>& allTexts) {
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
        item.frame = core::Rect{element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        item.disabled = element.disabled;
        item.focusable = element.focusable;

        // 1. Direct or subtree text
        item.text = findSubtreeText(element);
        if (!isPrintableSemanticText(item.text)) {
            // Icon glyph (e.g. close 'X') or empty
            if (!item.text.empty()) {
                if (element.id.find("remove") != std::string::npos || element.id.find("close") != std::string::npos || element.id.find("delete") != std::string::npos) {
                    item.text = "Remove";
                }
            }
        }

        // 2. If direct text is empty, search for overlapping sibling label in parent container
        const core::dsl::Element* parent = nullptr;
        {
            auto pit = parentMap.find(&element);
            if (pit != parentMap.end()) {
                parent = pit->second;
            }
        }

        if (item.text.empty() && parent != nullptr) {
            for (const auto* sibling : parent->orderedChildren) {
                if (sibling != nullptr && sibling != &element) {
                    if (rectsIntersect(element.frame, sibling->frame)) {
                        std::string sibText = findSubtreeText(*sibling);
                        if (!sibText.empty() && isPrintableSemanticText(sibText)) {
                            item.text = sibText;
                            break; // Pick the first semantic title in layout order
                        }
                    }
                }
            }
        }

        // 3. Extract enclosing container context text (card, list item, or form group)
        {
            const core::dsl::Element* contextContainer = parent;
            // Ascend up to 2 levels to find a meaningful grouping container
            for (int depth = 0; depth < 2 && contextContainer != nullptr; ++depth) {
                // If container is not top-level root, collect all subtree texts
                auto grandPit = parentMap.find(contextContainer);
                if (grandPit != parentMap.end() && grandPit->second != nullptr) {
                    std::vector<std::string> containerTexts;
                    collectSubtreeTextsRecursive(*contextContainer, containerTexts);
                    if (!containerTexts.empty()) {
                        std::ostringstream ssContext;
                        bool firstTxt = true;
                        for (const auto& txt : containerTexts) {
                            if (!firstTxt) ssContext << ", ";
                            firstTxt = false;
                            ssContext << txt;
                        }
                        item.contextText = ssContext.str();
                        break;
                    }
                }
                contextContainer = (grandPit != parentMap.end()) ? grandPit->second : nullptr;
            }
            if (item.contextText.empty() && !item.text.empty()) {
                item.contextText = item.text;
            }
        }

        // 4. Find spatially or tree-nearest visible text in the scene
        {
            float bestEdgeDist = 1e9f;
            float bestCenterDist = 1e9f;
            std::string bestNearestText;

            for (const auto& tRec : allTexts) {
                // Skip text that is inside this element's own subtree
                if (isDescendantOf(tRec.element, &element, parentMap)) {
                    continue;
                }
                if (!isPrintableSemanticText(tRec.text)) {
                    continue;
                }

                float edgeDist = computeRectDistance(item.frame, tRec.frame);
                float centerDist = computeCenterDistance(item.frame, tRec.frame);

                bool isBetter = false;
                if (edgeDist < bestEdgeDist - 0.5f) {
                    isBetter = true;
                } else if (std::abs(edgeDist - bestEdgeDist) <= 0.5f && centerDist < bestCenterDist) {
                    isBetter = true;
                }

                if (isBetter) {
                    bestEdgeDist = edgeDist;
                    bestCenterDist = centerDist;
                    bestNearestText = tRec.text;
                }
            }

            if (!bestNearestText.empty()) {
                item.nearestText = bestNearestText;
                item.nearestDistance = std::round(bestEdgeDist * 10.0f) / 10.0f;
            }
        }

        result.push_back(std::move(item));
    }

    for (const auto* child : element.orderedChildren) {
        if (child != nullptr) {
            collectInteractiveElementsRecursive(*child, result, nextIndex, onlyVisible, parentMap, allTexts);
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

    std::unordered_map<const core::dsl::Element*, const core::dsl::Element*> parentMap;
    std::vector<TextNodeRecord> allTexts;
    for (const auto* root : roots) {
        if (root != nullptr) {
            buildParentMapAndCollectTexts(*root, parentMap, allTexts, onlyVisible);
        }
    }

    for (const auto* root : roots) {
        if (root != nullptr) {
            collectInteractiveElementsRecursive(*root, result, nextIndex, onlyVisible, parentMap, allTexts);
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
           << ",\"contextText\":\"" << escapeJson(item.contextText) << "\""
           << ",\"nearestText\":\"" << escapeJson(item.nearestText) << "\""
           << ",\"nearestDistance\":" << item.nearestDistance
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
