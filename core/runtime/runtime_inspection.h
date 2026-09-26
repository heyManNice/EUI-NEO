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

inline bool findInspectionPath(Ui& ui, InstanceStore& instances, const Element& element) {
    instances.inspectionPath.push_back(&element);
    if (element.id == instances.inspectedElement) {
        return true;
    }
    for (const Element* child : element.orderedChildren) {
        if (findInspectionPath(ui, instances, *child)) {
            return true;
        }
    }
    instances.inspectionPath.pop_back();
    return false;
}

inline const std::vector<const Element*>& inspectionPath(Ui& ui, InstanceStore& instances) {
    if (instances.inspectionPathId == instances.inspectedElement &&
        instances.inspectionPathGeneration == instances.composeGeneration) {
        return instances.inspectionPath;
    }
    instances.inspectionPathId = instances.inspectedElement;
    instances.inspectionPathGeneration = instances.composeGeneration;
    instances.inspectionPath.clear();
    if (instances.inspectedElement.empty()) {
        return instances.inspectionPath;
    }
    // Depth first search for the path from a root to the inspected element. It only
    // runs when the inspected element changed or the page was recomposed.
    for (const Element* root : ui.orderedRoots()) {
        if (findInspectionPath(ui, instances, *root)) {
            break;
        }
    }
    return instances.inspectionPath;
}

inline DebugInspection computeInspection(Ui& ui, InstanceStore& instances, float dpiScale) {
    DebugInspection inspection;
    if (instances.inspectedElement.empty()) {
        return inspection;
    }
    const std::vector<const Element*>& path = inspectionPath(ui, instances);
    if (path.empty() || path.back()->id != instances.inspectedElement) {
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
    inspection.scissor = scissor;
    inspection.hasScissor = hasScissor;
    return inspection;
}

} // namespace runtime

inline void Runtime::setInspectedElement(const std::string& id) {
    if (instances_.inspectedElement == id) {
        return;
    }
    instances_.inspectedElement = id;
    instances_.inspectionPath.clear();
    instances_.inspectionPathId.clear();
    // The previous overlay has to disappear and the new one has to appear, and
    // overlays are drawn while the render cache is filled.
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline runtime::DebugInspection Runtime::debugInspection(float dpiScale) {
    return runtime::computeInspection(ui_, instances_, dpiScale);
}

#endif

} // namespace core::dsl
