#include "core/debug/devtools_ui.h"
#include "core/debug/devtools_icons.h"
#include "core/debug/devtools_performance.h"

#include <algorithm>

namespace core::debug {

namespace {

constexpr float kTabFontSize = 14.0f;
constexpr float kTabHorizontalPadding = 14.0f;
constexpr float kMoreMenuWidth = 136.0f;
constexpr float kMoreMenuRowHeight = 22.67f;
constexpr float kMoreMenuPadding = 2.0f;

void composeToolbarIcon(core::dsl::Ui& ui, const std::string& id, const char* svg,
                        const std::function<void()>& onClick = {}) {
    ui.stack(id)
        .size(24.0f, 24.0f)
        .align(core::Align::CENTER, core::Align::CENTER)
        .content([&] {
            auto background = ui.rect(id + ".background");
            background.fill()
                .ignoreLayout()
                .states({0.0f, 0.0f, 0.0f, 0.0f},
                        {0.25f, 0.31f, 0.39f, 1.0f},
                        {0.25f, 0.31f, 0.39f, 1.0f})
                .instantStates()
                .radius(5.0f);
            if (onClick) {
                background.onClick(onClick);
            }
            background.build();
            ui.svg(id + ".icon")
                .size(16.0f, 16.0f)
                .source(svg)
                .tint("#C8D5E4")
                .contain()
                .build();
        })
        .build();
}

void composeToolbarTab(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                       bool selected, const std::function<void()>& onClick) {
    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(kDevtoolsToolbarHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states({0.0f, 0.0f, 0.0f, 0.0f},
                        {0.23f, 0.28f, 0.34f, 1.0f},
                        {0.23f, 0.28f, 0.34f, 1.0f})
                .instantStates()
                .onClick(onClick)
                .build();
            ui.row(id + ".content")
                .width(core::SizeValue::wrapContent())
                .height(kDevtoolsToolbarHeight)
                .padding(kTabHorizontalPadding, 0.0f)
                .content([&] {
                    ui.text(id + ".label")
                        .width(core::SizeValue::wrapContent())
                        .height(kDevtoolsToolbarHeight)
                        .text(label)
                        .fontSize(kTabFontSize)
                        .color(selected ? "#DCE7F5" : "#9CA9B8")
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
            if (selected) {
                ui.rect(id + ".indicator")
                    .width(core::SizeValue::fill())
                    .height(2.0f)
                    .margin(4.0f, 0.0f, 4.0f, 0.0f)
                    .y(kDevtoolsToolbarHeight - 2.0f)
                    .ignoreLayout()
                    .color("#66A9F7")
                    .build();
            }
        })
        .build();
}

void composeDockOption(core::dsl::Ui& ui, const std::string& id, const char* svg,
                       const std::string& label, DockPosition position, DockPosition selectedPosition,
                       const std::function<void(DockPosition)>& onSelect) {
    const bool selected = position == selectedPosition;
    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(kMoreMenuRowHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states(selected ? core::Color{0.20f, 0.34f, 0.52f, 1.0f}
                                 : core::Color{0.0f, 0.0f, 0.0f, 0.0f},
                        {0.27f, 0.35f, 0.45f, 1.0f},
                        {0.27f, 0.35f, 0.45f, 1.0f})
                .radius(5.0f)
                .instantStates()
                .onClick([onSelect, position] { onSelect(position); })
                .build();
            ui.row(id + ".content")
                .fill()
                .padding(5.0f, 0.0f)
                .gap(3.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.svg(id + ".icon")
                        .size(17.0f, 17.0f)
                        .source(svg)
                        .tint(selected ? "#91C1FF" : "#C8D5E4")
                        .contain()
                        .build();
                    ui.text(id + ".label")
                        .width(core::SizeValue::fill())
                        .height(kMoreMenuRowHeight)
                        .text(label)
                        .fontSize(13.0f)
                        .color(selected ? "#DCEBFF" : "#DCE7F5")
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void composeMoreMenu(core::dsl::Ui& ui, float x, float y, DockPosition selectedPosition,
                     const std::function<void(DockPosition)>& onSelect) {
    ui.stack("more.menu")
        .position(x, y)
        .width(kMoreMenuWidth)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect("more.menu.background")
                .fill()
                .ignoreLayout()
                .color("#303741")
                .radius(7.0f)
                .shadow(14.0f, 0.0f, 5.0f, core::Color{0.0f, 0.0f, 0.0f, 0.30f})
                .build();
            ui.column("more.menu.rows")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(kMoreMenuPadding)
                .content([&] {
                    composeDockOption(ui, "more.menu.dock.floating", icons::kDockFloatingSvg,
                                      "Separate Window", DockPosition::Floating, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.left", icons::kDockLeftSvg,
                                      "Dock to Left", DockPosition::Left, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.bottom", icons::kDockBottomSvg,
                                      "Dock to Bottom", DockPosition::Bottom, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.right", icons::kDockRightSvg,
                                      "Dock to Right", DockPosition::Right, selectedPosition, onSelect);
                })
                .build();
        })
        .build();
}

void composeToolbar(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const bool compact = state.panel.width < 360.0f;
    ui.stack("toolbar")
        .width(core::SizeValue::fill())
        .height(kDevtoolsToolbarHeight)
        .content([&] {
            ui.rect("toolbar.background")
                .fill()
                .ignoreLayout()
                .color("#292F38")
                .onClick(state.moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{})
                .build();
            ui.row("toolbar.items")
                .fill()
                .padding(compact ? 4.0f : 8.0f, 0.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    if (!compact) {
                        ui.row("toolbar.leading")
                            .width(core::SizeValue::wrapContent())
                            .height(24.0f)
                            .gap(4.0f)
                            .content([&] {
                                composeToolbarIcon(ui, "selectElement", icons::kSelectElementSvg, state.moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{});
                                composeToolbarIcon(ui, "deviceViewport", icons::kDeviceViewportSvg, state.moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{});
                            })
                            .build();
                    }
                    ui.row("toolbar.tabs")
                        .width(core::SizeValue::wrapContent())
                        .height(kDevtoolsToolbarHeight)
                        .margin(compact ? 0.0f : 8.0f, 0.0f, 0.0f, 0.0f)
                        .content([&] {
                            composeToolbarTab(ui, "performance.tab", "Performance", state.activeTab == DevtoolsTab::Performance, [onSelect = actions.selectTab] { onSelect(DevtoolsTab::Performance); });
                            composeToolbarTab(ui, "elements.tab", "Elements", state.activeTab == DevtoolsTab::Elements, [onSelect = actions.selectTab] { onSelect(DevtoolsTab::Elements); });
                        })
                        .build();
                    ui.stack("toolbar.spacer")
                        .width(core::SizeValue::fill())
                        .height(1.0f)
                        .build();
                    ui.row("toolbar.trailing")
                        .width(core::SizeValue::wrapContent())
                        .height(24.0f)
                        .gap(4.0f)
                        .content([&] {
                            composeToolbarIcon(ui, "settings", icons::kSettingsSvg, state.moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{});
                            composeToolbarIcon(ui, "more", icons::kMoreSvg, actions.toggleMoreMenu);
                            composeToolbarIcon(ui, "close", icons::kCloseSvg, actions.close);
                        })
                        .build();
                })
                .build();
        })
        .build();
}

void composePanelContent(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    if (state.activeTab == DevtoolsTab::Performance) {
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
                .height(28.0f)
                .text("Element inspection is the next milestone.")
                .fontSize(15.0f)
                .color("#9CA9B8")
                .build();
        })
        .build();
}

} // namespace

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const float width = state.width;
    const float height = state.height;
    const Rect& panel = state.panel;
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
                        .color("#20252D")
                        .onClick(state.moreMenuOpen ? actions.dismissMoreMenu : std::function<void()>{})
                        .build();
                    if (!detached) {
                        ui.rect("panel.border")
                            .width(core::SizeValue::fill())
                            .height(1.0f)
                            .color("#596574")
                            .build();
                    }
                    composeToolbar(ui, state, actions);
                    composePanelContent(ui, state, actions);
                })
                .build();
            if (state.moreMenuOpen) {
                ui.rect("more.dismiss")
                    .position(panel.x, panel.y + kDevtoolsToolbarHeight)
                    .size(panel.width, std::max(0.0f, panel.height - kDevtoolsToolbarHeight))
                    .color({0.0f, 0.0f, 0.0f, 0.0f})
                    .onClick(actions.dismissMoreMenu)
                    .build();
                composeMoreMenu(ui,
                                std::max(panel.x + 8.0f, panel.x + panel.width - kMoreMenuWidth - 36.0f),
                                panel.y + kDevtoolsToolbarHeight + 6.0f, state.dockPosition,
                                actions.selectDockPosition);
            }
        })
        .build();
}

} // namespace core::debug
