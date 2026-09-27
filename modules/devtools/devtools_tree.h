#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/dsl_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The page tree, as the panel holds it.
//
// The panel copies the tree out of the page instead of walking it while it draws: a
// snapshot owns its strings, carries the depth of every node and stays valid across
// composes, so the panel can hold it between frames. Building it is the panel's business —
// the framework offers the page's elements in paint order plus a revision counter, and
// nothing else knows what a "tree view" needs.

namespace modules::devtools {

struct ElementTreeNode {
    std::string id;
    core::dsl::ElementKind kind = core::dsl::ElementKind::Stack;
    std::string text;
    int depth = 0;
    int zIndex = 0;
    bool clip = false;
    bool interactive = false;
    bool disabled = false;
    core::Rect frame;
};

struct ElementTreeSnapshot {
    std::uint64_t revision = 0;
    bool truncated = false;
    std::vector<ElementTreeNode> nodes;
};

// A page can hold tens of thousands of elements and the panel only ever shows a window of
// them, so a snapshot stops copying at this many nodes.
inline constexpr std::size_t kElementTreeMaximumNodes = 20000;

// A row shows a readable prefix of a text element, never the whole string.
inline constexpr std::size_t kElementTreeTextLimit = 64;

// Copies the page's elements in pre-order with their depth, so the panel can map nodes to
// flat rows. `revision` is the page's structure counter at the moment of the copy.
ElementTreeSnapshot buildElementTree(const core::dsl::Runtime& page,
                                    std::size_t maximumNodes = kElementTreeMaximumNodes);

} // namespace modules::devtools

#endif
