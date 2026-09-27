#pragma once

#include "core/dsl.h"
#include "core/runtime/runtime_geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace core::dsl {
class Ui;
}

namespace core::dsl::runtime {

// The box of one element, resolved into the space the render pass draws in: the same
// transform and clip the element itself is drawn with, so a tool that draws from this
// cannot drift away from the element it describes. This is the core's Resolve half of the
// seam: geometry only. What a tool draws with it (a box model wash, an outline, nothing)
// lives with the tool.
struct ElementBox {
    bool active = false;
    RenderTransform transform;
    LayoutRect frame;               // the box itself: background and border live here
    EdgeInsets padding;             // inset from the box edge to the content
    EdgeInsets margin;              // layout spacing outside the box
    float borderWidth = 0.0f;       // painted inside the box edge, the same on every side
    Rect scissor;
    bool hasScissor = false;
};

// One element a tool marks, plus the cached path that leads to it. Element pointers only
// stay valid until the next compose, so the cache is keyed by the compose generation
// instead of being pinned for the runtime's lifetime.
struct ElementMark {
    std::string id;
    std::vector<const Element*> path;
    std::string pathId;
    std::uint64_t pathGeneration = 0;
};

class InstanceStore;

// Geometry of the box overlay for one mark, resolved for the pass that is about to draw.
// `composeGeneration` is what tells the mark whether the path it cached is still valid,
// since element pointers only live until the next compose.
ElementBox computeElementBox(Ui& ui,
                             InstanceStore& instances,
                             ElementMark& mark,
                             std::uint64_t composeGeneration,
                             float dpiScale);

} // namespace core::dsl::runtime
