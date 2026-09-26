#pragma once

#include "core/dsl.h"
#include "core/runtime/runtime_geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core::dsl {
class Ui;
}

namespace core::dsl::runtime {

// Debug tools copy the element tree out of a Runtime instead of walking it while
// they draw: a snapshot owns its strings, carries the depth of every node and
// stays valid across composes, so a panel can hold it between frames.
struct ElementTreeNode {
    std::string id;
    ElementKind kind = ElementKind::Stack;
    std::string text;
    int depth = 0;
    int zIndex = 0;
    bool clip = false;
    bool interactive = false;
    bool disabled = false;
    Rect frame;
};

struct ElementTreeSnapshot {
    std::uint64_t revision = 0;
    bool truncated = false;
    std::vector<ElementTreeNode> nodes;
};

// A page can hold tens of thousands of elements and the tree view only ever shows
// a window of them, so a snapshot stops copying at this many nodes.
inline constexpr std::size_t kElementTreeMaximumNodes = 20000;

// The tree view shows a readable prefix of a text element, never the whole string.
inline constexpr std::size_t kElementTreeTextLimit = 64;

// Copies at most `limit` bytes of text without splitting a UTF-8 sequence, so a
// truncated label never turns into invalid bytes.
inline std::string truncateElementText(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

// Everything the renderer needs to draw the inspection overlay for one element.
// The runtime derives it from the element path with the same transform and clip
// rules the element itself is drawn with, so the overlay cannot drift away from
// the element it describes.
struct DebugInspection {
    bool active = false;
    RenderTransform transform;
    LayoutRect frame;
    EdgeInsets padding;
    Rect scissor;
    bool hasScissor = false;
};

// The overlay palette follows components::LayoutDebugStyle (frame red, content
// blue, low-alpha fills), kept here as values because core never depends on
// components.
inline constexpr Color kInspectionFrameColor{0.96f, 0.32f, 0.38f, 0.16f};
inline constexpr Color kInspectionFrameStroke{0.96f, 0.32f, 0.38f, 0.95f};
inline constexpr Color kInspectionContentColor{0.28f, 0.58f, 0.98f, 0.14f};
inline constexpr Color kInspectionContentStroke{0.28f, 0.58f, 0.98f, 0.95f};

struct InstanceStore;

#if defined(EUI_DEBUG_BUILD)
// Geometry of the inspection overlay for the element the store marks as
// inspected. Both the panel (through `Runtime::debugInspection`) and the renderer
// read this, so there is one implementation of "where is that element now".
DebugInspection computeInspection(Ui& ui, InstanceStore& instances, float dpiScale);
#endif

} // namespace core::dsl::runtime
