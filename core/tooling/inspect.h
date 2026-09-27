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

// Puts a freshly written patch on the live tree and asks for the frame that shows it.
// The per-frame capture walks the tree every frame, but it may skip a static subtree, so
// one full tree update is requested as well.
inline void commitElementPatch(Ui& ui, ToolingState& state, const std::string& id, bool changed,
                               bool& fullTreeUpdateRequested, bool& paintRequested,
                               bool& fullPaintRequested) {
    if (!changed) {
        return;
    }
    const auto entry = state.patches.find(id);
    if (entry == state.patches.end()) {
        return;
    }
    if (Element* element = findElement(ui, id)) {
        applyElementPatch(*element, entry->second);
    }
    fullTreeUpdateRequested = true;
    paintRequested = true;
    fullPaintRequested = true;
}

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
    return state != nullptr ? state->hovered.id : empty;
}

inline std::size_t Runtime::elementPatchCount() const {
    const runtime::ToolingState* state = tooling();
    return state != nullptr ? state->patches.size() : 0;
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

// The values of one element, for the single element a tool shows. The field table is
// walked once, so a field added to the table is read without touching this.
inline runtime::ElementValues Runtime::elementValues(const std::string& id) const {
    runtime::ElementValues values;
    if (id.empty()) {
        return values;
    }
    const Element* element = runtime::findElement(ui_, id);
    if (element == nullptr) {
        return values;
    }

    values.active = true;
    values.id = element->id;
    values.kind = element->kind;
    values.frame = {element->frame.x, element->frame.y, element->frame.width, element->frame.height};
    values.margin = element->margin;
    values.padding = element->padding;
    values.borderWidth = element->border.width;
    values.zIndex = element->zIndex;
    values.clip = element->clip;
    values.interactive = element->interactive;
    values.disabled = element->disabled;
    values.text = runtime::truncateElementText(element->text, runtime::kElementTreeTextLimit);
    for (int index = 0; index < runtime::kElementFieldCount; ++index) {
        const runtime::ElementField field = static_cast<runtime::ElementField>(index);
        values.fields[static_cast<std::size_t>(index)] = runtime::readElementField(*element, field);
    }

    if (const runtime::ToolingState* state = tooling()) {
        const auto patch = state->patches.find(id);
        if (patch != state->patches.end()) {
            values.written = patch->second.mask;
        }
    }
    return values;
}

// The one writing entry point. A tool hands over the value it built, and the patch store
// decides whether it changed anything: same value written twice is not a new edit.
inline void Runtime::setElementField(const std::string& id, runtime::ElementField field,
                                     const runtime::FieldValue& value) {
    if (id.empty()) {
        return;
    }
    runtime::ToolingState& state = ensureTooling();
    const bool changed = state.patches[id].set(field, value);
    runtime::commitElementPatch(ui_, state, id, changed, fullTreeUpdateRequested_, paintRequested_,
                                fullPaintRequested_);
}

inline void Runtime::clearElementField(const std::string& id, runtime::ElementField field) {
    runtime::ToolingState* state = tooling();
    if (state == nullptr) {
        return;
    }
    const auto found = state->patches.find(id);
    if (found == state->patches.end()) {
        return;
    }
    found->second.clear(field);
    if (found->second.mask == 0) {
        state->patches.erase(found);
    }
}

inline void Runtime::clearElementFields(const std::string& id) {
    if (runtime::ToolingState* state = tooling()) {
        state->patches.erase(id);
    }
}

inline void Runtime::clearElementFields() {
    runtime::ToolingState* state = tooling();
    if (state == nullptr || state->patches.empty()) {
        return;
    }
    state->patches.clear();
    // The elements keep the values they were composed with until the app composes
    // again, which is also what clears a patch: nothing else has to be undone.
    fullTreeUpdateRequested_ = true;
    paintRequested_ = true;
    fullPaintRequested_ = true;
}

#endif

} // namespace core::dsl
