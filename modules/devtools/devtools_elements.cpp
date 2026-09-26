#include "modules/devtools/devtools_elements.h"

#include "components/virtuallist.h"
#include "modules/devtools/devtools_properties.h"
#include "modules/devtools/devtools_theme.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace modules::devtools {

namespace {

using core::dsl::runtime::ElementTreeNode;
using core::dsl::runtime::ElementTreeSnapshot;

// One visible row: the node plus what the row needs to draw itself. The app hands
// the tree over as a pre-order list with depth, so the rows on screen are a filter
// of that list with collapsed subtrees removed.
struct ElementRow {
    const ElementTreeNode* node = nullptr;
    bool hasChildren = false;
    bool collapsed = false;
};

unsigned int elementKindIcon(core::dsl::ElementKind kind) {
    const DevtoolsTheme& theme = devtoolsTheme();
    switch (kind) {
    case core::dsl::ElementKind::Row:
    case core::dsl::ElementKind::Column:
    case core::dsl::ElementKind::Stack:
    case core::dsl::ElementKind::Flow:
        return theme.iconElementLayout;
    case core::dsl::ElementKind::Text:
        return theme.iconElementText;
    case core::dsl::ElementKind::Image:
        return theme.iconElementImage;
    case core::dsl::ElementKind::Svg:
        return theme.iconElementSvg;
    case core::dsl::ElementKind::Shadertoy:
        return theme.iconElementShadertoy;
    case core::dsl::ElementKind::Rect:
    case core::dsl::ElementKind::Polygon:
        break;
    }
    return theme.iconElementShape;
}

// Nodes start collapsed: a row is only opened if the user expanded it, so a tree
// of a whole page does not unfold itself the moment the tab is shown.
bool isExpanded(const DevtoolsPanelState* panelState, const std::string& id) {
    if (panelState == nullptr) {
        return false;
    }
    const std::vector<std::string>& expanded = panelState->expandedElements;
    return std::find(expanded.begin(), expanded.end(), id) != expanded.end();
}

std::vector<ElementRow> visibleElementRows(const ElementTreeSnapshot& tree, const DevtoolsPanelState* panelState) {
    std::vector<ElementRow> rows;
    rows.reserve(tree.nodes.size());
    int hiddenDeeperThan = -1;
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
        const ElementTreeNode& node = tree.nodes[index];
        if (hiddenDeeperThan >= 0 && node.depth > hiddenDeeperThan) {
            continue;
        }
        hiddenDeeperThan = -1;
        const bool hasChildren = index + 1 < tree.nodes.size() && tree.nodes[index + 1].depth > node.depth;
        const bool collapsed = hasChildren && !isExpanded(panelState, node.id);
        rows.push_back({&node, hasChildren, collapsed});
        if (collapsed) {
            hiddenDeeperThan = node.depth;
        }
    }
    return rows;
}

void composeElementRow(core::dsl::Ui& ui, const std::string& id, const ElementRow& row, bool selected,
                       const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const ElementTreeNode& node = *row.node;
    const std::string nodeId = node.id;
    // The list already owns `id` (the row slot), so the row content lives in its
    // own subtree instead of reusing that element.
    const std::string base = id + ".row";
    const std::function<void()> select = actions.selectElement
        ? std::function<void()>([select = actions.selectElement, nodeId] { select(nodeId); })
        : std::function<void()>{};
    const std::function<void()> toggle = actions.toggleElementCollapsed
        ? std::function<void()>([toggle = actions.toggleElementCollapsed, nodeId] { toggle(nodeId); })
        : std::function<void()>{};
    // Hovering a row previews that element in the page; leaving it takes the
    // preview back. The row itself is still selected by a click.
    const std::function<void(bool)> hover = actions.hoverElement
        ? std::function<void(bool)>([hover = actions.hoverElement, nodeId](bool entered) {
              hover(nodeId, entered);
          })
        : std::function<void(bool)>{};

    ui.stack(base)
        .width(core::SizeValue::fill())
        .height(theme.elementRowHeight)
        .content([&] {
            ui.rect(base + ".background")
                .fill()
                .ignoreLayout()
                .states(selected ? theme.elementRowSelected : theme.transparent, theme.elementRowHover,
                        theme.elementRowHover)
                .instantStates()
                .onClick(select)
                .onHover(hover)
                .build();
            ui.row(base + ".content")
                .fill()
                .content([&] {
                    if (node.depth > 0) {
                        ui.stack(base + ".indent")
                            .width(theme.elementIndent * static_cast<float>(node.depth))
                            .height(1.0f)
                            .build();
                    }
                    ui.stack(base + ".disclosure")
                        .size(theme.elementDisclosureSize, theme.elementRowHeight)
                        .align(core::Align::CENTER, core::Align::CENTER)
                        .content([&] {
                            if (!row.hasChildren) {
                                return;
                            }
                            ui.text(base + ".disclosure.icon")
                                .size(theme.elementDisclosureSize, theme.elementDisclosureSize)
                                .icon(row.collapsed ? theme.iconElementCollapsed : theme.iconElementExpanded)
                                .fontSize(theme.elementFontSize)
                                .lineHeight(theme.elementDisclosureSize)
                                .color(theme.mutedText)
                                .horizontalAlign(core::HorizontalAlign::Center)
                                .verticalAlign(core::VerticalAlign::Center)
                                .onClick(toggle)
                                .build();
                        })
                        .build();
                    ui.text(base + ".kind")
                        .size(theme.elementKindWidth, theme.elementRowHeight)
                        .icon(elementKindIcon(node.kind))
                        .fontSize(theme.elementFontSize)
                        .lineHeight(theme.elementRowHeight)
                        .color(theme.elementKindText)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                    ui.text(base + ".label")
                        .width(core::SizeValue::fill())
                        .height(theme.elementRowHeight)
                        .text(nodeId)
                        .fontSize(theme.elementRowFontSize)
                        .color(node.disabled ? theme.elementDisabledText : theme.primaryText)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                    char sizeText[48];
                    std::snprintf(sizeText, sizeof(sizeText), "%.0f x %.0f", node.frame.width, node.frame.height);
                    ui.text(base + ".size")
                        .width(core::SizeValue::wrapContent())
                        .height(theme.elementRowHeight)
                        .text(sizeText)
                        .fontSize(theme.elementRowFontSize - 1.0f)
                        .color(theme.mutedText)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void composeElementsNotice(core::dsl::Ui& ui, const std::string& id, const std::string& text, float width,
                           float height) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id)
        .size(width, height)
        .text(text)
        .fontSize(theme.sectionFontSize)
        .color(theme.mutedText)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

} // namespace

// The kind of an element as the tree row and the property area print it.
const char* elementKindName(core::dsl::ElementKind kind) {
    switch (kind) {
    case core::dsl::ElementKind::Row: return "row";
    case core::dsl::ElementKind::Column: return "column";
    case core::dsl::ElementKind::Stack: return "stack";
    case core::dsl::ElementKind::Flow: return "flow";
    case core::dsl::ElementKind::Rect: return "rect";
    case core::dsl::ElementKind::Polygon: return "polygon";
    case core::dsl::ElementKind::Text: return "text";
    case core::dsl::ElementKind::Image: return "image";
    case core::dsl::ElementKind::Svg: return "svg";
    case core::dsl::ElementKind::Shadertoy: return "shadertoy";
    }
    return "element";
}

void composeElementsTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const ElementTreeSnapshot* tree = state.elementTree;
    const float top = state.panel.y + theme.toolbarHeight + (state.detached ? 0.0f : 1.0f);
    const float contentHeight =
        std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));
    const bool hasTree = tree != nullptr && !tree->nodes.empty();
    // The property area keeps the bottom of the tab and the tree keeps the rest, so
    // selecting a row does not move anything around.
    const float propertyHeight =
        hasTree ? std::min(theme.propertiesHeight, contentHeight * 0.65f) : 0.0f;
    const float noticeHeight = tree != nullptr && tree->truncated ? theme.elementRowHeight : 0.0f;
    const float listHeight = std::max(0.0f, contentHeight - propertyHeight - noticeHeight);

    if (!hasTree) {
        composeElementsNotice(ui, "elements.empty",
                              tree == nullptr ? "Waiting for the app page..." : "The page has no elements.",
                              state.panel.width, listHeight);
    } else {
        const std::vector<ElementRow> rows = visibleElementRows(*tree, state.panelState);
        const float scrollOffset = state.panelState != nullptr ? state.panelState->elementsScrollOffset : 0.0f;
        components::virtualList(ui, "elements.list")
            .position(state.panel.x, top)
            .size(state.panel.width, listHeight)
            .itemCount(static_cast<std::int64_t>(rows.size()))
            .rowHeight(theme.elementRowHeight)
            .offset(scrollOffset)
            .onChange(actions.setElementsScrollOffset)
            .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width, float height) {
                if (index < 0 || index >= static_cast<std::int64_t>(rows.size())) {
                    return;
                }
                const ElementRow& row = rows[static_cast<std::size_t>(index)];
                const bool isSelected = state.panelState != nullptr && row.node->id == state.panelState->selectedElement;
                (void)width;
                (void)height;
                composeElementRow(rowUi, rowId, row, isSelected, actions);
            })
            .build();
    }

    if (hasTree && propertyHeight > 0.0f) {
        ElementPropertiesState properties;
        properties.properties = state.properties;
        properties.overrideCount = state.propertyOverrideCount;
        composeElementProperties(ui, "elements.properties",
                                 {state.panel.x, top + listHeight, state.panel.width, propertyHeight}, properties,
                                 state, actions);
    }

    if (tree != nullptr && tree->truncated) {
        char notice[96];
        std::snprintf(notice, sizeof(notice), "Showing the first %zu elements.", tree->nodes.size());
        composeElementsNotice(ui, "elements.truncated", notice, state.panel.width, theme.elementRowHeight);
    }
}

} // namespace modules::devtools
