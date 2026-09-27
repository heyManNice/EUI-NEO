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

// Puts a freshly written override on the live tree and asks for the frame that
// shows it. The per-frame capture walks the tree every frame, but it may skip a
// static subtree, so one full tree update is requested as well.
inline void commitDebugElementOverride(Ui& ui, ToolingState& state, const std::string& id, bool changed,
                                        bool& fullTreeUpdateRequested, bool& paintRequested,
                                        bool& fullPaintRequested) {
    if (!changed) {
        return;
    }
    const auto entry = state.overrides.find(id);
    if (entry == state.overrides.end()) {
        return;
    }
    if (Element* element = findDebugElement(ui, id)) {
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

inline const std::vector<const Element*>& inspectionPath(Ui& ui, InstanceStore& instances, InspectionMark& mark,
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
        if (findInspectionPath(ui, instances, mark, *root)) {
            break;
        }
    }
    return mark.path;
}

inline DebugInspection computeInspection(Ui& ui, InstanceStore& instances, InspectionMark& mark,
                                          std::uint64_t composeGeneration, float dpiScale) {
    DebugInspection inspection;
    if (mark.id.empty()) {
        return inspection;
    }
    const std::vector<const Element*>& path = inspectionPath(ui, instances, mark, composeGeneration);
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

inline std::string Runtime::debugElementAt(double x, double y, float dpiScale) const {
    PointerEvent event;
    event.x = x;
    event.y = y;
    // Every element is a candidate, disabled ones included: a picker asks what is
    // drawn at the pointer, not what would take input there.
    return hitTest(event, dpiScale, [](const Element&) { return true; }, true);
}

// A read-only copy of the element tree in pre-order.
inline runtime::ElementTreeSnapshot Runtime::elementTree(std::size_t maximumNodes) const {
    runtime::ElementTreeSnapshot snapshot;
    snapshot.revision = elementStructureRevision();

    // Pre-order with an explicit stack: the snapshot carries depth instead of nesting,
    // so a tool that renders it can map nodes to flat rows.
    std::vector<std::pair<const Element*, int>> pending;
    const std::vector<const Element*>& roots = ui_.orderedRoots();
    pending.reserve(roots.size());
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        pending.push_back({*root, 0});
    }

    while (!pending.empty()) {
        if (snapshot.nodes.size() >= maximumNodes) {
            snapshot.truncated = true;
            break;
        }
        const std::pair<const Element*, int> current = pending.back();
        pending.pop_back();
        const Element& element = *current.first;

        runtime::ElementTreeNode node;
        node.id = element.id;
        node.kind = element.kind;
        node.depth = current.second;
        node.zIndex = element.zIndex;
        node.clip = element.clip;
        node.interactive = element.interactive;
        node.disabled = element.disabled;
        node.frame = {element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        if (element.kind == ElementKind::Text) {
            node.text = runtime::truncateElementText(element.text, runtime::kElementTreeTextLimit);
        }
        snapshot.nodes.push_back(std::move(node));

        const std::vector<const Element*>& children = element.orderedChildren;
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            pending.push_back({*child, current.second + 1});
        }
    }
    return snapshot;
}

// The two readers a tool uses to see what is marked and what it replaced. They answer
// "nothing" when no tool ever talked to the runtime.
inline const std::string& Runtime::hoveredElement() const {
    static const std::string empty;
    const runtime::ToolingState* state = tooling();
    return state != nullptr ? state->hoveredMark.id : empty;
}

inline std::size_t Runtime::debugElementOverrideCount() const {
    const runtime::ToolingState* state = tooling();
    return state != nullptr ? state->overrides.size() : 0;
}

inline void Runtime::setHoveredElement(const std::string& id) {
    runtime::ToolingState& state = ensureTooling();
    if (state.hoveredMark.id == id) {
        return;
    }
    state.hoveredMark.id = id;
    state.hoveredMark.path.clear();
    state.hoveredMark.pathId.clear();
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline runtime::DebugInspection Runtime::debugHoverInspection(float dpiScale) {
    runtime::ToolingState* state = tooling();
    if (state == nullptr) {
        return {};
    }
    return runtime::computeInspection(ui_, instances_, state->hoveredMark, state->composeGeneration, dpiScale);
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
    properties.gradient = element->gradient;
    properties.textColor = element->textColor;

    if (const runtime::ToolingState* state = tooling()) {
        const auto override = state->overrides.find(id);
        if (override != state->overrides.end()) {
            properties.overridden = override->second.mask;
        }
    }
    return properties;
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, float value) {
    if (id.empty()) {
        return;
    }
    runtime::ToolingState& state = ensureTooling();
    runtime::DebugElementOverride& override = state.overrides[id];
    const bool changed = runtime::setDebugOverrideFloat(override, property, value);
    runtime::commitDebugElementOverride(ui_, state, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property,
                                             const Color& value) {
    if (id.empty()) {
        return;
    }
    runtime::ToolingState& state = ensureTooling();
    runtime::DebugElementOverride& override = state.overrides[id];
    const bool changed = runtime::setDebugOverrideColor(override, property, value);
    runtime::commitDebugElementOverride(ui_, state, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::setDebugElementOverride(const std::string& id, runtime::DebugPropertyId property, bool value) {
    if (id.empty()) {
        return;
    }
    runtime::ToolingState& state = ensureTooling();
    runtime::DebugElementOverride& override = state.overrides[id];
    const bool changed = runtime::setDebugOverrideFlag(override, property, value);
    runtime::commitDebugElementOverride(ui_, state, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                        fullPaintRequested_);
}

inline void Runtime::clearDebugElementOverride(const std::string& id, runtime::DebugPropertyId property) {
    runtime::ToolingState* state = tooling();
    if (state == nullptr) {
        return;
    }
    const auto found = state->overrides.find(id);
    if (found == state->overrides.end()) {
        return;
    }
    found->second.mask &= ~runtime::debugPropertyBit(property);
    if (found->second.mask == 0) {
        state->overrides.erase(found);
    }
}

inline void Runtime::clearDebugElementOverrides(const std::string& id) {
    if (runtime::ToolingState* state = tooling()) {
        state->overrides.erase(id);
    }
}

inline void Runtime::clearAllDebugElementOverrides() {
    runtime::ToolingState* state = tooling();
    if (state == nullptr || state->overrides.empty()) {
        return;
    }
    state->overrides.clear();
    // The elements keep the values they were composed with until the app composes
    // again, which is also what clears an override: nothing else has to be undone.
    fullTreeUpdateRequested_ = true;
    paintRequested_ = true;
    fullPaintRequested_ = true;
}

#endif

} // namespace core::dsl
