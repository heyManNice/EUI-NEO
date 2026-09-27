#pragma once

#include "core/tooling/config.h"

namespace core::dsl {

#if EUI_TOOLING_ENABLED

// Element inspection, the parts a debug tool drives.
//
// The overlay geometry is derived from the element path with the same helpers the
// render pass uses (`InstanceStore::renderTransform` for the accumulated transform
// and the same clip intersection rule), so an element inside a scrolled, scaled or
// clipped container is highlighted where it actually is. Nothing here runs unless a
// tool asked for an element, and the path is cached per compose, so a paint costs
// O(depth) instead of a tree search.

namespace runtime {

inline bool findMarkedPath(Ui& ui, InstanceStore& instances, ElementMark& mark, const Element& element) {
    mark.path.push_back(&element);
    if (element.id == mark.id) {
        return true;
    }
    for (const Element* child : element.orderedChildren) {
        if (findMarkedPath(ui, instances, mark, *child)) {
            return true;
        }
    }
    mark.path.pop_back();
    return false;
}

inline const std::vector<const Element*>& markedPath(Ui& ui, InstanceStore& instances, ElementMark& mark,
                                                         std::uint64_t composeGeneration) {
    if (mark.pathId == mark.id && mark.pathGeneration == composeGeneration) {
        return mark.path;
    }
    mark.pathId = mark.id;
    mark.pathGeneration = composeGeneration;
    mark.path.clear();
    if (mark.id.empty()) {
        return mark.path;
    }
    // Depth first search for the path from a root to the marked element. It only
    // runs when the mark changed or the page was recomposed.
    for (const Element* root : ui.orderedRoots()) {
        if (findMarkedPath(ui, instances, mark, *root)) {
            break;
        }
    }
    return mark.path;
}

inline ElementBox computeElementBox(Ui& ui, InstanceStore& instances, ElementMark& mark,
                                          std::uint64_t composeGeneration, float dpiScale) {
    ElementBox inspection;
    if (mark.id.empty()) {
        return inspection;
    }
    const std::vector<const Element*>& path = markedPath(ui, instances, mark, composeGeneration);
    if (path.empty() || path.back()->id != mark.id) {
        return inspection;
    }

    // Walk the path the way the render pass walks the tree: accumulate the render
    // transform top down and intersect the clip rectangles of the ancestors.
    RenderTransform transform;
    Rect scissor;
    bool hasScissor = false;
    for (const Element* element : path) {
        transform = instances.renderTransform(*element, dpiScale, transform);
        if (!element->clip) {
            continue;
        }
        const Rect clipFrame = applyRenderTransform(toPixelRect(element->frame, dpiScale), transform);
        if (hasScissor) {
            if (!intersectRect(scissor, clipFrame, scissor)) {
                return inspection;      // fully clipped away, the overlay would not show
            }
        } else {
            scissor = clipFrame;
            hasScissor = true;
        }
    }

    const Element& target = *path.back();
    inspection.active = true;
    inspection.transform = transform;
    inspection.frame = target.frame;
    inspection.padding = target.padding;
    inspection.margin = target.margin;
    inspection.borderWidth = target.border.width;
    inspection.scissor = scissor;
    inspection.hasScissor = hasScissor;
    return inspection;
}

} // namespace runtime

// The id of the element drawn at a point, from the page's own hit test. Asking the page
// keeps the answer in the same space as the frame the user is looking at.
inline std::string Runtime::elementIdAt(double x, double y, float dpiScale) const {
    PointerEvent event;
    event.x = x;
    event.y = y;
    // Every element is a candidate, disabled ones included: a picker asks what is
    // drawn at the pointer, not what would take input there.
    return hitTest(event, dpiScale, [](const Element&) { return true; }, true);
}

// The mark a tool set, and the box it resolves to. They answer "nothing" when no tool
// ever talked to the runtime.
inline const std::string& Runtime::hoveredElement() const {
    static const std::string empty;
    const runtime::ToolingState* state = tooling();
    return state != nullptr ? state->hovered.id : empty;
}
inline void Runtime::setHoveredElement(const std::string& id) {
    runtime::ToolingState& state = ensureTooling();
    if (state.hovered.id == id) {
        return;
    }
    state.hovered.id = id;
    state.hovered.path.clear();
    state.hovered.pathId.clear();
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline runtime::ElementBox Runtime::hoveredBox(float dpiScale) {
    runtime::ToolingState* state = tooling();
    if (state == nullptr) {
        return {};
    }
    return runtime::computeElementBox(ui_, instances_, state->hovered, state->composeGeneration, dpiScale);
}


#endif

} // namespace core::dsl
