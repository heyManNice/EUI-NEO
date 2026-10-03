#include "modules/devtools/devtools_elements.h"

#include "components/checkbox.h"
#include "components/virtuallist.h"
#include "core/render/text.h"
#include "modules/devtools/devtools_properties.h"
#include "modules/devtools/devtools_theme.h"
#include "modules/devtools/devtools_tree.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace modules::devtools {

namespace {

using modules::devtools::ElementTreeNode;
using modules::devtools::ElementTreeSnapshot;

// One visible row: the node plus what the row needs to draw itself. The app hands
// the tree over as a pre-order list with depth, so the rows on screen are a filter
// of that list with collapsed subtrees removed.
struct ElementRow {
    const ElementTreeNode* node = nullptr;
    // The nearest row above this one with a smaller depth: a row prints its id without the
    // part this one already says.
    const ElementTreeNode* parent = nullptr;
    bool hasChildren = false;
    bool collapsed = false;
};

// Nodes start collapsed: a row is only opened if the user expanded it, so a tree
// of a whole page does not unfold itself the moment the tab is shown.
bool isExpanded(const std::vector<std::string>& expanded, const std::string& id) {
    return std::find(expanded.begin(), expanded.end(), id) != expanded.end();
}

std::vector<ElementRow> visibleElementRows(const ElementTreeSnapshot& tree, const std::vector<std::string>& expanded) {
    std::vector<ElementRow> rows;
    rows.reserve(tree.nodes.size());
    // The nodes arrive in pre-order with their depth, so the ancestor at a depth is the last
    // node seen at it. A row keeps the one directly above so it can print a trimmed id.
    std::vector<const ElementTreeNode*> ancestors;
    int hiddenDeeperThan = -1;
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
        const ElementTreeNode& node = tree.nodes[index];
        if (hiddenDeeperThan >= 0 && node.depth > hiddenDeeperThan) {
            continue;
        }
        hiddenDeeperThan = -1;
        const bool hasChildren = index + 1 < tree.nodes.size() && tree.nodes[index + 1].depth > node.depth;
        const bool collapsed = hasChildren && !isExpanded(expanded, node.id);
        const std::size_t depth = static_cast<std::size_t>(std::max(0, node.depth));
        const ElementTreeNode* parent = depth > 0 && depth <= ancestors.size() ? ancestors[depth - 1] : nullptr;
        rows.push_back({&node, parent, hasChildren, collapsed});
        ancestors.resize(depth);
        ancestors.push_back(&node);
        if (collapsed) {
            hiddenDeeperThan = node.depth;
        }
    }
    return rows;
}

// What a row prints as its id. Element ids are scoped by the page and by the scopes a
// component pushes, never by their parent element (`page.ok` hangs under `page.buttons` but
// only shares `page.`), so the part to drop is whatever the two ids have in common up to the
// last segment they agree on. Ids that share nothing, top level rows, and ids the parent
// already says in full keep what they have.
std::string elementRowLabel(const ElementRow& row, bool trimPrefix) {
    const std::string& id = row.node->id;
    if (!trimPrefix || row.parent == nullptr) {
        return id;
    }
    const std::string& parentId = row.parent->id;
    std::size_t shared = 0;
    while (shared < id.size() && shared < parentId.size() && id[shared] == parentId[shared]) {
        ++shared;
    }
    if (shared == 0) {
        return id;
    }
    // The break has to fall on a separator, so a row prints names and never the halves of two
    // ids that happen to start the same way. When the ids agree up to the parent's end, the
    // separator that follows in the child is part of what the parent already said.
    const std::size_t cut = shared < id.size() && id[shared] == '.' ? shared + 1 : id.rfind('.', shared - 1) + 1;
    if (cut == 0 || cut >= id.size()) {
        return id;
    }
    return id.substr(cut);
}

// The selection can arrive from the page (a pick) instead of from the tree, so the
// tree brings it into view once: the ancestors that hide it are opened, and the list
// scrolls to its row. Both go through actions, so they land in the next composition,
// and the marker stops the reveal from undoing what the user does by hand afterwards.
void revealSelection(const ElementTreeSnapshot& tree,
                     const DevtoolsUiState& state,
                     const DevtoolsUiActions& actions,
                     float listHeight) {
    if (state.panelState == nullptr || state.panelState->selectedElement.empty() ||
        state.panelState->revealedSelection == state.panelState->selectedElement) {
        return;
    }
    const std::string& selected = state.panelState->selectedElement;
    std::size_t index = tree.nodes.size();
    for (std::size_t candidate = 0; candidate < tree.nodes.size(); ++candidate) {
        if (tree.nodes[candidate].id == selected) {
            index = candidate;
            break;
        }
    }
    if (index == tree.nodes.size()) {
        // The element is gone, because the page recomposed without it. Nothing to show,
        // and no marker either, so a later selection is revealed again.
        return;
    }

    // The ancestors of a node are the nearest earlier nodes with a smaller depth.
    std::vector<std::string> expanded = state.panelState->expandedElements;
    for (int depth = tree.nodes[index].depth - 1; depth >= 0; --depth) {
        for (std::size_t walk = index; walk > 0; --walk) {
            const ElementTreeNode& candidate = tree.nodes[walk - 1];
            if (candidate.depth != depth) {
                continue;
            }
            if (!isExpanded(expanded, candidate.id)) {
                if (actions.tree.toggleElementCollapsed) {
                    actions.tree.toggleElementCollapsed(candidate.id);
                }
                expanded.push_back(candidate.id);
            }
            break;
        }
    }

    // The row can only be off screen once the ancestors above it are open, so the
    // offset is measured against the rows the tree will have when they are.
    if (listHeight > 0.0f && actions.tree.setScrollOffset) {
        const std::vector<ElementRow> rows = visibleElementRows(tree, expanded);
        const float rowHeight = devtoolsTheme().elementRowHeight;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].node->id != selected) {
                continue;
            }
            const float rowTop = static_cast<float>(row) * rowHeight;
            const float offset = state.panelState->elementsScrollOffset;
            if (rowTop < offset) {
                actions.tree.setScrollOffset(rowTop);
            } else if (rowTop + rowHeight > offset + listHeight) {
                actions.tree.setScrollOffset(std::max(0.0f, rowTop + rowHeight - listHeight));
            }
            break;
        }
    }
    if (actions.tree.setRevealedSelection) {
        actions.tree.setRevealedSelection(selected);
    }
}

void composeElementRow(core::dsl::Ui& ui, const std::string& id, const ElementRow& row, bool selected,
                       bool trimPrefix, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const ElementTreeNode& node = *row.node;
    // The actions address the element the row stands for, so they always take the whole id;
    // only what the row prints follows the option.
    const std::string nodeId = node.id;
    const std::string label = elementRowLabel(row, trimPrefix);
    // The list already owns `id` (the row slot), so the row content lives in its
    // own subtree instead of reusing that element.
    const std::string base = id + ".row";
    const std::function<void()> select = actions.tree.selectElement
        ? std::function<void()>([select = actions.tree.selectElement, nodeId] { select(nodeId); })
        : std::function<void()>{};
    const std::function<void()> toggle = actions.tree.toggleElementCollapsed
        ? std::function<void()>([toggle = actions.tree.toggleElementCollapsed, nodeId] { toggle(nodeId); })
        : std::function<void()>{};
    // Hovering a row previews that element in the page; leaving it takes the
    // preview back. The row itself is still selected by a click.
    const std::function<void(bool)> hover = actions.tree.hoverElement
        ? std::function<void(bool)>([hover = actions.tree.hoverElement, nodeId](bool entered) {
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
            // The highlight above spans the whole row, so the content keeps the inset
            // the numbers need: without it the size column would touch the scrollbar.
            // Left padding aligns the disclosure arrow with the toolbar margin.
            ui.row(base + ".content")
                .fill()
                .padding(theme.toolbarPadding * 0.5f, 0.0f, theme.elementDetailsPadding, 0.0f)
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
                                .icon(row.collapsed ? theme.iconDisclosureCollapsed : theme.iconDisclosureExpanded)
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
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(theme.elementRowHeight)
                        .text(label)
                        .fontSize(theme.elementRowFontSize)
                        .color(node.disabled ? theme.elementDisabledText : theme.primaryText)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                    char sizeText[48];
                    std::snprintf(sizeText, sizeof(sizeText), "%.0f x %.0f", node.frame.width, node.frame.height);
                    ui.text(base + ".size")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::wrapContent())
                        .height(theme.elementRowHeight)
                        .text(sizeText)
                        .fontSize(theme.captionFontSize)
                        .color(theme.mutedText)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

// Geometry and drag limits for the property area and its divider.
struct PropertyAreaGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float currentHeight = 0.0f;
    float dragStartHeight = 0.0f;
    float pixelScale = 1.0f;
    float minimumHeight = 0.0f;
    float maximumHeight = 0.0f;
};

// The divider on the top edge of the property area: dragging it up gives the area
// more room and dragging it down takes room away, within what the tree can spare. The
// press records the height the drag works from, so a drag measures the pointer against
// a fixed value instead of accumulating deltas.
//
// A pointer event is in framebuffer pixels while the panel composes in logical units,
// so the press also records the ratio between them and the drag divides its delta with
// it: without that the area would resize as many times faster than the pointer as the
// window is scaled.
//
// The strip that takes the pointer is the same element that shows the hover: a child
// rectangle would be the topmost interactive element and swallow the press without
// ever reaching a handler.
void composeElementPropertyDivider(core::dsl::Ui& ui,
                                   const std::string& id,
                                   const PropertyAreaGeometry& geometry,
                                   const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float x = geometry.x;
    const float y = geometry.y;
    const float width = geometry.width;
    const float height = geometry.height;
    ui.rect(id + ".body")
        .position(x, y)
        .size(width, height)
        .states(theme.transparent, theme.propertiesHandleHover, theme.propertiesHandleHover)
        .instantStates()
        .onPress([begin = actions.properties.beginResize, liveHeight = geometry.currentHeight,
                  composedWidth = geometry.width](const core::PointerEvent&, const core::Rect& bounds) {
            if (begin) {
                // The bounds come back in framebuffer pixels, so their width against
                // the width the strip was composed with is the ratio to divide by.
                begin(liveHeight, static_cast<float>(bounds.width) / std::max(0.001f, composedWidth));
            }
        })
        .onRelease([end = actions.properties.endResize](const core::PointerEvent&, const core::Rect&) {
            if (end) {
                end();
            }
        })
        .onDrag([resize = actions.properties.setHeight, dragStartHeight = geometry.dragStartHeight,
                 pixelScale = geometry.pixelScale, minimumHeight = geometry.minimumHeight,
                 maximumHeight = geometry.maximumHeight](const core::dsl::DragEvent& event) {
            if (!resize) {
                return;
            }
            const float delta = static_cast<float>(event.totalY) / std::max(0.001f, pixelScale);
            resize(std::clamp(dragStartHeight - delta, minimumHeight, maximumHeight));
        })
        .build();
    ui.rect(id + ".line")
        .position(x, y + std::floor((height - 1.0f) * 0.5f))
        .size(width, 1.0f)
        .color(theme.panelBorder)
        .build();
}

void composeElementsNotice(core::dsl::Ui& ui, const std::string& id, const std::string& text, float width,
                           float height) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id)
        .fontFamily(theme.fontFamily)
        .size(width, height)
        .text(text)
        .fontSize(theme.sectionFontSize)
        .color(theme.mutedText)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

} // namespace

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

// Width one option needs: a checkbox takes its box, the gap after it and its insets out of
// the width it is given, so its label is measured instead. A fixed width would either clip
// the text or leave the two options further apart than the gap between them.
float elementOptionWidth(const std::string& label, const DevtoolsTheme& theme,
                         const components::theme::ThemeMetricTokens& metrics) {
    const float inset = metrics.spacing.control;
    const float text = core::TextPrimitive::measureTextWidth(label, theme.fontFamily, theme.elementRowFontSize, 400);
    return inset + theme.elementOptionBoxSize + inset + text + inset;
}

// The view options of the Elements tab: one line of checkboxes above the tree, framed
// as a sub-toolbar with a background and a bottom border. The options align with the
// toolbar margin so they sit cleanly over the tree.
void composeElementsOptions(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const DevtoolsPanelState* panel = state.panelState;
    const std::string trimLabel = "Omit prefix";
    const std::string boundsLabel = "Show bounds";
    const bool compact = state.panel.width < theme.compactWidth;
    components::theme::ThemeColorTokens control = devtoolsControlTheme();
    control.metrics.spacing.control = compact ? theme.toolbarCompactPadding : theme.toolbarPadding;
    control.metrics.radius.small = 2.0f;
    const float y = state.panel.y + theme.toolbarHeight + (state.detached ? 0.0f : 1.0f);

    ui.stack("elements.options")
        .position(state.panel.x, y)
        .size(state.panel.width, theme.elementOptionsHeight)
        .content([&] {
            ui.rect("elements.options.border")
                .position(0.0f, theme.elementOptionsHeight - 1.0f)
                .size(state.panel.width, 1.0f)
                .ignoreLayout()
                .color(theme.panelBorder)
                .build();
            ui.row("elements.options.items")
                .fill()
                .alignItems(core::Align::CENTER)
                .gap(theme.elementOptionGap)
                .content([&] {
                    components::checkbox(ui, "elements.options.trimPrefix")
                        .size(elementOptionWidth(trimLabel, theme, control.metrics), theme.elementOptionsHeight)
                        .fontFamily(theme.fontFamily)
                        .text(trimLabel)
                        .fontSize(theme.elementRowFontSize)
                        .boxSize(theme.elementOptionBoxSize)
                        .theme(control)
                        .checked(panel != nullptr && panel->trimIdPrefix)
                        .onChange(actions.tree.setTrimIdPrefix)
                        .build();
                    components::checkbox(ui, "elements.options.showBounds")
                        .size(elementOptionWidth(boundsLabel, theme, control.metrics), theme.elementOptionsHeight)
                        .fontFamily(theme.fontFamily)
                        .text(boundsLabel)
                        .fontSize(theme.elementRowFontSize)
                        .boxSize(theme.elementOptionBoxSize)
                        .theme(control)
                        .checked(panel != nullptr && panel->showElementBounds)
                        .onChange(actions.tree.setShowElementBounds)
                        .build();
                })
                .build();
        })
        .build();
}

void composeElementsTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const ElementTreeSnapshot* tree = state.elementTree;
    const float tabTop = state.panel.y + theme.toolbarHeight + (state.detached ? 0.0f : 1.0f);
    const float top = tabTop + theme.elementOptionsHeight;
    const float contentHeight = std::max(
        0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f) -
                  theme.elementOptionsHeight);
    const bool hasTree = tree != nullptr && !tree->nodes.empty();
    // The property area belongs to the element the user selected, so it only takes
    // space while there is one, and the divider on its top edge decides how much.
    const bool hasProperties = hasTree && state.properties != nullptr && state.properties->active;
    const float noticeHeight = tree != nullptr && tree->truncated ? theme.elementRowHeight : 0.0f;
    const float availableHeight = std::max(0.0f, contentHeight - noticeHeight);
    const float handleHeight = hasProperties ? theme.propertiesHandleHeight : 0.0f;
    // The tree keeps a few rows whatever the divider does, so the area can always be
    // grown and shrunk instead of pinning itself to one end.
    const float minimumHeight = std::min(theme.propertiesMinimumHeight, availableHeight);
    const float maximumPropertyHeight = std::max(
        minimumHeight, availableHeight - handleHeight - theme.propertiesMinimumTreeHeight);
    const float requestedHeight = state.panelState != nullptr && state.panelState->propertiesHeight > 0.0f
        ? state.panelState->propertiesHeight
        : availableHeight * theme.propertiesInitialFraction;
    const float propertyHeight =
        hasProperties ? std::clamp(requestedHeight, minimumHeight, maximumPropertyHeight) : 0.0f;
    const float listHeight = std::max(0.0f, availableHeight - handleHeight - propertyHeight);

    composeElementsOptions(ui, state, actions);

    if (!hasTree) {
        composeElementsNotice(ui, "elements.empty",
                              tree == nullptr ? "Waiting for the app page..." : "The page has no elements.",
                              state.panel.width, listHeight);
    } else {
        // A selection that came from the page is brought into view before the rows are
        // composed, so the row the user picked is the one they see selected.
        revealSelection(*tree, state, actions, listHeight);
        static const std::vector<std::string> noExpansions;
        const std::vector<std::string>& expanded =
            state.panelState != nullptr ? state.panelState->expandedElements : noExpansions;
        const std::vector<ElementRow> rows = visibleElementRows(*tree, expanded);
        const float scrollOffset = state.panelState != nullptr ? state.panelState->elementsScrollOffset : 0.0f;
        // The list spans the whole panel, so its scrollbar sits on the panel edge and a
        // row highlight reaches it instead of stopping a gap short. That gap is what the
        // list reserves for the scrollbar, which the row content replaces with its own
        // right inset.
        components::virtualList(ui, "elements.list")
            .position(state.panel.x, top)
            .size(state.panel.width, listHeight)
            .itemCount(static_cast<std::int64_t>(rows.size()))
            .rowHeight(theme.elementRowHeight)
            .scrollbarGap(0.0f)
            .offset(scrollOffset)
            .onChange(actions.tree.setScrollOffset)
            .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width, float height) {
                if (index < 0 || index >= static_cast<std::int64_t>(rows.size())) {
                    return;
                }
                const ElementRow& row = rows[static_cast<std::size_t>(index)];
                const bool isSelected = state.panelState != nullptr && row.node->id == state.panelState->selectedElement;
                const bool trimPrefix = state.panelState != nullptr && state.panelState->trimIdPrefix;
                (void)width;
                (void)height;
                composeElementRow(rowUi, rowId, row, isSelected, trimPrefix, actions);
            })
            .build();
    }

    if (hasProperties) {
        const float dragStartHeight =
            state.panelState != nullptr && state.panelState->propertiesResizeStartHeight > 0.0f
            ? state.panelState->propertiesResizeStartHeight
            : propertyHeight;
        PropertyAreaGeometry divider;
        divider.x = state.panel.x;
        divider.y = top + listHeight;
        divider.width = state.panel.width;
        divider.height = handleHeight;
        divider.currentHeight = propertyHeight;
        divider.dragStartHeight = dragStartHeight;
        divider.pixelScale = state.panelState != nullptr ? state.panelState->propertiesResizeScale : 1.0f;
        divider.minimumHeight = minimumHeight;
        divider.maximumHeight = maximumPropertyHeight;
        composeElementPropertyDivider(ui, "elements.properties.handle", divider, actions);
        ElementPropertiesState properties;
        properties.properties = state.properties;
        properties.overrideCount = state.propertyOverrideCount;
        composeElementProperties(ui, "elements.properties",
                                 {state.panel.x, top + listHeight + handleHeight, state.panel.width,
                                  propertyHeight},
                                 properties, state, actions);
    }

    if (tree != nullptr && tree->truncated) {
        char notice[96];
        std::snprintf(notice, sizeof(notice), "Showing the first %zu elements.", tree->nodes.size());
        composeElementsNotice(ui, "elements.truncated", notice, state.panel.width, theme.elementRowHeight);
    }
}

} // namespace modules::devtools
