#include "modules/devtools/tree.h"

#include "modules/devtools/fields.h"

#if defined(EUI_TOOLING)

#include <utility>

namespace modules::devtools {

ElementTreeSnapshot buildElementTree(const core::dsl::Runtime& page, std::size_t maximumNodes) {
    ElementTreeSnapshot snapshot;
    snapshot.revision = page.elementStructureRevision();

    // Pre-order with an explicit stack: the snapshot carries depth instead of nesting, so a
    // panel that renders it maps nodes to flat rows.
    std::vector<std::pair<const core::dsl::Element*, int>> pending;
    const std::vector<const core::dsl::Element*>& roots = page.elementRoots();
    pending.reserve(roots.size());
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        pending.push_back({*root, 0});
    }

    while (!pending.empty()) {
        if (snapshot.nodes.size() >= maximumNodes) {
            snapshot.truncated = true;
            break;
        }
        const std::pair<const core::dsl::Element*, int> current = pending.back();
        pending.pop_back();
        const core::dsl::Element& element = *current.first;

        ElementTreeNode node;
        node.id = element.id;
        node.kind = element.kind;
        node.depth = current.second;
        node.zIndex = element.zIndex;
        node.clip = element.clip;
        node.interactive = element.interactive;
        node.disabled = element.disabled;
        node.frame = {element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        if (element.kind == core::dsl::ElementKind::Text) {
            // The row shows a prefix of a long label, never the whole string.
            node.text = truncateElementText(element.text, kElementTreeTextLimit);
        }
        snapshot.nodes.push_back(std::move(node));

        const std::vector<const core::dsl::Element*>& children = element.orderedChildren;
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            pending.push_back({*child, current.second + 1});
        }
    }
    return snapshot;
}

} // namespace modules::devtools

#endif
