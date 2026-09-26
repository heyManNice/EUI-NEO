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

// Looks an element up by the id a tool holds. The walk uses `children`, not
// `orderedChildren`, so it also works between a compose and the next layout pass.
inline const Element* findDebugElement(const Ui& ui, const std::string& id) {
    std::vector<const Element*> pending;
    const std::vector<const Element*>& roots = ui.orderedRoots();
    pending.reserve(roots.size());
    for (const Element* root : roots) {
        pending.push_back(root);
    }
    while (!pending.empty()) {
        const Element* element = pending.back();
        pending.pop_back();
        if (element->id == id) {
            return element;
        }
        for (const auto& child : element->children) {
            pending.push_back(child.get());
        }
    }
    return nullptr;
}

// Puts a freshly written override on the live tree and asks for the frame that
// shows it. The per-frame capture walks the tree every frame, but it may skip a
// static subtree, so one full tree update is requested as well.
inline void commitDebugElementOverride(Ui& ui, InstanceStore& instances, const std::string& id, bool changed,
                                        bool& fullTreeUpdateRequested, bool& paintRequested,
                                        bool& fullPaintRequested) {
    if (!changed) {
        return;
    }
    const auto entry = instances.debugOverrides.find(id);
    if (entry == instances.debugOverrides.end()) {
        return;
    }
    if (Element* element = ui.debugFindElement(id)) {
        applyDebugOverride(*element, entry->second);
    }
    fullTreeUpdateRequested = true;
    paintRequested = true;
    fullPaintRequested = true;
}

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

inline runtime::DebugElementProperties Runtime::debugElementProperties(const std::string& id) {
    runtime::DebugElementProperties properties;
    if (id.empty()) {
        return properties;
    }
    const Element* element = runtime::findDebugElement(ui_, id);
    if (element == nullptr) {
        return properties;
    }

    properties.active = true;
    properties.id = element->id;
    properties.kind = element->kind;
    properties.frame = {element->frame.x, element->frame.y, element->frame.width, element->frame.height};
    properties.margin = element->margin;
    properties.padding = element->padding;
    properties.borderWidth = element->border.width;
    properties.zIndex = element->zIndex;
    properties.clip = element->clip;
    properties.interactive = element->interactive;
    properties.disabled = element->disabled;
    properties.text = runtime::truncateElementText(element->text, runtime::kElementTreeTextLimit);
    properties.color = element->color;
    properties.opacity = element->opacity;
    properties.radius = element->radius;
    properties.borderColor = element->border.color;
    properties.blur = element->blur;
    properties.shadow = element->shadow;
    properties.textColor = element->textColor;

    const auto override = instances_.debugOverrides.find(id);
    if (override != instances_.debugOverrides.end()) {
        properties.overridden = override->second.mask;
    }
    return properties;
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, float value) {
    if (id.empty()) {
        return;
    }
    runtime::DebugElementOverride& override = instances_.debugOverrides[id];
    const bool changed = runtime::setDebugOverrideFloat(override, property, value);
    runtime::commitDebugElementOverride(ui_, instances_, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property,
                                             const Color& value) {
    if (id.empty()) {
        return;
    }
    runtime::DebugElementOverride& override = instances_.debugOverrides[id];
    const bool changed = runtime::setDebugOverrideColor(override, property, value);
    runtime::commitDebugElementOverride(ui_, instances_, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, bool value) {
    if (id.empty()) {
        return;
    }
    runtime::DebugElementOverride& override = instances_.debugOverrides[id];
    const bool changed = runtime::setDebugOverrideFlag(override, property, value);
    runtime::commitDebugElementOverride(ui_, instances_, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::clearDebugElementOverride(const std::string& id, runtime::DebugPropertyId property) {
    const auto found = instances_.debugOverrides.find(id);
    if (found == instances_.debugOverrides.end()) {
        return;
    }
    found->second.mask &= ~runtime::debugPropertyBit(property);
    if (found->second.mask == 0) {
        instances_.debugOverrides.erase(found);
    }
}

inline void Runtime::clearDebugElementOverrides(const std::string& id) {
    instances_.debugOverrides.erase(id);
}

inline void Runtime::clearAllDebugElementOverrides() {
    if (instances_.debugOverrides.empty()) {
        return;
    }
    instances_.debugOverrides.clear();
    // The elements keep the values they were composed with until the app composes
    // again, which is also what clears an override: nothing else has to be undone.
    fullTreeUpdateRequested_ = true;
    paintRequested_ = true;
    fullPaintRequested_ = true;
}

#endif

} // namespace core::dsl
