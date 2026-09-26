#include "modules/devtools/devtools_ui.h"

#include "modules/devtools/devtools_performance.h"
#include "modules/devtools/devtools_theme.h"

#include <algorithm>

namespace modules::devtools {

namespace {

void composeToolbarIcon(core::dsl::Ui& ui, const std::string& id, unsigned int codepoint,
                        const core::Color& color, const std::function<void()>& onClick = {}) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.stack(id)
        .size(theme.iconButtonSize, theme.iconButtonSize)
        .align(core::Align::CENTER, core::Align::CENTER)
        .content([&] {
            auto background = ui.rect(id + ".background");
            background.fill()
                .ignoreLayout()
                .states(theme.transparent, theme.toolbarHover, theme.toolbarHover)
                .instantStates()
                .radius(theme.iconRadius);
            if (onClick) {
                background.onClick(onClick);
            }
            background.build();
            ui.text(id + ".icon")
                .size(theme.iconSize, theme.iconSize)
                .icon(codepoint)
                .fontSize(theme.iconSize)
                .lineHeight(theme.iconSize)
                .color(color)
                .horizontalAlign(core::HorizontalAlign::Center)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .build();
}

void composeToolbarTab(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                       bool selected, const std::function<void()>& onClick) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(theme.toolbarHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states(theme.transparent, theme.tabHover, theme.tabHover)
                .instantStates()
                .onClick(onClick)
                .build();
            ui.row(id + ".content")
                .width(core::SizeValue::wrapContent())
                .height(theme.toolbarHeight)
                .padding(theme.tabHorizontalPadding, 0.0f)
                .content([&] {
                    ui.text(id + ".label")
                        .width(core::SizeValue::wrapContent())
                        .height(theme.toolbarHeight)
                        .text(label)
                        .fontSize(theme.tabFontSize)
                        .color(selected ? theme.primaryText : theme.mutedText)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
            if (selected) {
                ui.rect(id + ".indicator")
                    .width(core::SizeValue::fill())
                    .height(theme.tabIndicatorHeight)
                    .margin(theme.indicatorInset, 0.0f, theme.indicatorInset, 0.0f)
                    .y(theme.toolbarHeight - theme.tabIndicatorHeight)
                    .ignoreLayout()
                    .color(theme.accent)
                    .build();
            }
        })
        .build();
}

void composeDockOption(core::dsl::Ui& ui, const std::string& id, unsigned int codepoint,
                       const std::string& label, DockPosition position, DockPosition selectedPosition,
                       const std::function<void(DockPosition)>& onSelect) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const bool selected = position == selectedPosition;
    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(theme.menuRowHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states(selected ? theme.menuRowSelected : theme.transparent,
                        theme.menuRowHover,
                        theme.menuRowHover)
                .radius(theme.iconRadius)
                .instantStates()
                .onClick([onSelect, position] { onSelect(position); })
                .build();
            ui.row(id + ".content")
                .fill()
                .padding(theme.menuRowIconPadding, 0.0f)
                .gap(theme.menuRowIconGap)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text(id + ".icon")
                        .size(theme.menuIconSize, theme.menuIconSize)
                        .icon(codepoint)
                        .fontSize(theme.menuIconSize)
                        .lineHeight(theme.menuIconSize)
                        .color(selected ? theme.sectionLabel : theme.icon)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                    ui.text(id + ".label")
                        .width(core::SizeValue::fill())
                        .height(theme.menuRowHeight)
                        .text(label)
                        .fontSize(theme.menuRowFontSize)
                        .color(selected ? theme.menuRowSelectedText : theme.primaryText)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void composeMoreMenu(core::dsl::Ui& ui, float x, float y, DockPosition selectedPosition,
                     const std::function<void(DockPosition)>& onSelect) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.stack("more.menu")
        .position(x, y)
        .width(theme.menuWidth)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect("more.menu.background")
                .fill()
                .ignoreLayout()
                .color(theme.menuBackground)
                .radius(theme.iconRadius + 2.0f)
                .shadow(theme.menuShadowRadius, 0.0f, theme.menuShadowOffsetY, theme.menuShadow)
                .build();
            ui.column("more.menu.rows")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(theme.menuPadding)
                .content([&] {
                    composeDockOption(ui, "more.menu.dock.floating", theme.iconDockFloating,
                                      "Separate Window", DockPosition::Floating, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.left", theme.iconDockLeft,
                                      "Dock to Left", DockPosition::Left, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.bottom", theme.iconDockDown,
                                      "Dock to Bottom", DockPosition::Bottom, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.right", theme.iconDockRight,
                                      "Dock to Right", DockPosition::Right, selectedPosition, onSelect);
                })
                .build();
        })
        .build();
}

void composeToolbar(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const bool moreMenuOpen = state.panelState != nullptr && state.panelState->moreMenuOpen;
    const bool compact = state.panel.width < theme.compactWidth;
    const std::function<void()> dismissMenu = moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{};
    ui.stack("toolbar")
        .width(core::SizeValue::fill())
        .height(theme.toolbarHeight)
        .content([&] {
            ui.rect("toolbar.background")
                .fill()
                .ignoreLayout()
                .color(theme.toolbarBackground)
                .onClick(dismissMenu)
                .build();
            ui.row("toolbar.items")
                .fill()
                .padding(compact ? theme.toolbarCompactPadding : theme.toolbarPadding, 0.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    if (!compact) {
                        ui.row("toolbar.leading")
                            .width(core::SizeValue::wrapContent())
                            .height(theme.iconButtonSize)
                            .gap(theme.metricGap)
                            .content([&] {
                                composeToolbarIcon(ui, "selectElement", theme.iconSelectElement, theme.icon, dismissMenu);
                                composeToolbarIcon(ui, "deviceViewport", theme.iconDeviceViewport, theme.icon, dismissMenu);
                            })
                            .build();
                    }
                    ui.row("toolbar.tabs")
                        .width(core::SizeValue::wrapContent())
                        .height(theme.toolbarHeight)
                        .margin(compact ? 0.0f : theme.toolbarPadding, 0.0f, 0.0f, 0.0f)
                        .content([&] {
                            const DevtoolsTab activeTab = state.panelState != nullptr
                                ? state.panelState->activeTab : DevtoolsTab::Performance;
                            composeToolbarTab(ui, "performance.tab", "Performance",
                                              activeTab == DevtoolsTab::Performance,
                                              [onSelect = actions.selectTab] { onSelect(DevtoolsTab::Performance); });
                            composeToolbarTab(ui, "elements.tab", "Elements",
                                              activeTab == DevtoolsTab::Elements,
                                              [onSelect = actions.selectTab] { onSelect(DevtoolsTab::Elements); });
                        })
                        .build();
                    ui.stack("toolbar.spacer")
                        .width(core::SizeValue::fill())
                        .height(1.0f)
                        .build();
                    ui.row("toolbar.trailing")
                        .width(core::SizeValue::wrapContent())
                        .height(theme.iconButtonSize)
                        .gap(theme.metricGap)
                        .content([&] {
                            composeToolbarIcon(ui, "settings", theme.iconSettings, theme.icon, dismissMenu);
                            composeToolbarIcon(ui, "more", theme.iconMore,
                                               moreMenuOpen ? theme.primaryText : theme.icon,
                                               actions.toggleMoreMenu);
                            composeToolbarIcon(ui, "close", theme.iconClose, theme.icon, actions.close);
                        })
                        .build();
                })
                .build();
        })
        .build();
}

void composePanelContent(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const DevtoolsTab activeTab = state.panelState != nullptr ? state.panelState->activeTab : DevtoolsTab::Performance;
    if (activeTab == DevtoolsTab::Performance) {
        composePerformanceTab(ui, state, actions);
        return;
    }
    ui.column("panel.content")
        .width(core::SizeValue::fill())
        .height(core::SizeValue::fill())
        .padding(24.0f, 28.0f, 24.0f, 0.0f)
        .content([&] {
            ui.text("empty.description")
                .width(core::SizeValue::fill())
                .height(theme.sectionHeight)
                .text("Element inspection is the next milestone.")
                .fontSize(theme.sectionFontSize)
                .color(theme.mutedText)
                .build();
        })
        .build();
}

} // namespace

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float width = state.width;
    const float height = state.height;
    const core::Rect& panel = state.panel;
    const bool detached = state.detached;
    ui.stack("root")
        .size(width, height)
        .content([&] {
            ui.column("panel")
                .position(panel.x, panel.y)
                .size(panel.width, panel.height)
                .content([&] {
                    ui.rect("panel.background")
                        .fill()
                        .ignoreLayout()
                        .color(theme.panelBackground)
                        .onClick(state.panelState != nullptr && state.panelState->moreMenuOpen
                                     ? actions.dismissMoreMenu
                                     : std::function<void()>{})
                        .build();
                    if (!detached) {
                        ui.rect("panel.border")
                            .width(core::SizeValue::fill())
                            .height(1.0f)
                            .color(theme.panelBorder)
                            .build();
                    }
                    composeToolbar(ui, state, actions);
                    composePanelContent(ui, state, actions);
                })
                .build();
            if (state.panelState != nullptr && state.panelState->moreMenuOpen) {
                ui.rect("more.dismiss")
                    .position(panel.x, panel.y + theme.toolbarHeight)
                    .size(panel.width, std::max(0.0f, panel.height - theme.toolbarHeight))
                    .color(theme.dismissSurface)
                    .onClick(actions.dismissMoreMenu)
                    .build();
                composeMoreMenu(ui,
                                std::max(panel.x + theme.toolbarPadding,
                                         panel.x + panel.width - theme.menuWidth - 36.0f),
                                panel.y + theme.toolbarHeight + 6.0f, state.dockPosition,
                                actions.selectDockPosition);
            }
        })
        .build();
}

} // namespace modules::devtools
