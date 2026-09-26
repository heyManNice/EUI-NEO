#pragma once

namespace core::dsl {

#if defined(EUI_DEBUG_BUILD)

// Element inspection, the parts a debug tool drives.
//
// The overlay geometry is derived from the element path with the same helpers the
// render pass uses (`InstanceStore::renderTransform` for the accumulated transform
// and the same clip intersection rule), so an element inside a scrolled, scaled or
// clipped container is highlighted where it actually is. Nothing here runs unless a
// tool asked for an element, and the path is cached per compose, so a paint costs
// O(depth) instead of a tree search.

namespace runtime {

inline bool findInspectionPath(Ui& ui, InstanceStore& instances, InspectionMark& mark, const Element& element) {
    mark.path.push_back(&element);
    if (element.id == mark.id) {
        return true;
    }
    for (const Element* child : element.orderedChildren) {
        if (findInspectionPath(ui, instances, mark, *child)) {
            return true;
        }
    }
    mark.path.pop_back();
    return false;
}

inline const std::vector<const Element*>& inspectionPath(Ui& ui, InstanceStore& instances, InspectionMark& mark) {
    if (mark.pathId == mark.id && mark.pathGeneration == instances.composeGeneration) {
        return mark.path;
    }
    mark.pathId = mark.id;
    mark.pathGeneration = instances.composeGeneration;
    mark.path.clear();
    if (mark.id.empty()) {
        return mark.path;
    }
    // Depth first search for the path from a root to the marked element. It only
    // runs when the mark changed or the page was recomposed.
    for (const Element* root : ui.orderedRoots()) {
        if (findInspectionPath(ui, instances, mark, *root)) {
            break;
        }
    }
    return mark.path;
}

inline DebugInspection computeInspection(Ui& ui, InstanceStore& instances, InspectionMark& mark, float dpiScale) {
    DebugInspection inspection;
    if (mark.id.empty()) {
        return inspection;
    }
    const std::vector<const Element*>& path = inspectionPath(ui, instances, mark);
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

inline void Runtime::setHoveredElement(const std::string& id) {
    if (instances_.hoveredMark.id == id) {
        return;
    }
    instances_.hoveredMark.id = id;
    instances_.hoveredMark.path.clear();
    instances_.hoveredMark.pathId.clear();
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline runtime::DebugInspection Runtime::debugHoverInspection(float dpiScale) {
    return runtime::computeInspection(ui_, instances_, instances_.hoveredMark, dpiScale);
}

#endif

} // namespace core::dsl
