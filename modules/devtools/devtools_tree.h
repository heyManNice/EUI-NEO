#pragma once

#if defined(EUI_TOOLING)

#include "core/dsl_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The page tree, as the panel holds it.
//
// The copy owns its strings and carries every node's depth, so it stays valid across
// composes and the panel can hold it between frames — which a walk while drawing could not.

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
