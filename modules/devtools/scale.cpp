#include "modules/devtools/scale.h"

#include "components/scrollview.h"
#include "modules/devtools/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace modules::devtools {

namespace {

std::string formatFloat(float value, int decimals = 2) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

void composePresetButton(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                         bool selected, const std::function<void()>& onClick) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color bg = selected
        ? theme.accent
        : core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.50f};
    const core::Color border = selected
        ? theme.accent
        : core::Color{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.50f};
    const core::Color text = selected ? core::Color{1.0f, 1.0f, 1.0f, 1.0f} : theme.primaryText;

    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(22.0f)
        .content([&] {
            ui.rect(id + ".bg")
                .fill()
                .ignoreLayout()
                .color(bg)
                .border(1.0f, border)
                .radius(2.0f)
                .build();
            ui.text(id + ".lbl")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::wrapContent())
                .height(22.0f)
                .padding(9.0f, 0.0f, 9.0f, 0.0f)
                .text(label)
                .fontSize(theme.captionFontSize)
                .fontWeight(selected ? 600 : 400)
                .color(text)
                .horizontalAlign(core::HorizontalAlign::Center)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .onClick(onClick)
        .build();
}

void composeTableRow(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                     const std::string& value, const core::Color& valueColor) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.18f};

    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(theme.elementRowHeight)
        .content([&] {
            ui.rect(id + ".border")
                .position(0.0f, theme.elementRowHeight - 1.0f)
                .width(core::SizeValue::fill())
                .height(1.0f)
                .ignoreLayout()
                .color(dividerColor)
                .build();

            ui.row(id + ".row")
                .fill()
                .padding(14.0f, 0.0f, 14.0f, 0.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text(id + ".lbl")
                        .fontFamily(theme.fontFamily)
                        .width(200.0f)
                        .height(theme.elementRowHeight)
                        .text(label)
                        .fontSize(theme.elementRowFontSize)
                        .color(theme.metricLabel)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();

                    ui.text(id + ".val")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(theme.elementRowHeight)
                        .text(value)
                        .fontSize(theme.elementRowFontSize)
                        .fontWeight(500)
                        .color(valueColor)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void composeSectionHeader(core::dsl::Ui& ui, const std::string& id, const std::string& title) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color headerBg{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.60f};
    const float headerHeight = 24.0f;

    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(headerHeight)
        .content([&] {
            ui.rect(id + ".bg")
                .fill()
                .ignoreLayout()
                .color(headerBg)
                .build();

            ui.rect(id + ".border")
                .position(0.0f, headerHeight - 1.0f)
                .width(core::SizeValue::fill())
                .height(1.0f)
                .ignoreLayout()
                .color(theme.panelBorder)
                .build();

            ui.row(id + ".row")
                .fill()
                .padding(14.0f, 0.0f, 14.0f, 0.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text(id + ".title")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::wrapContent())
                        .height(headerHeight)
                        .text(title)
                        .fontSize(theme.captionFontSize)
                        .fontWeight(600)
                        .color(theme.sectionLabel)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace

void composeScaleTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));
    const float toolbarHeight = theme.elementOptionsHeight;
    const float scrollHeight = std::max(0.0f, contentHeight - toolbarHeight);
    const float scrollOffset = state.panelState != nullptr ? state.panelState->scaleScrollOffset : 0.0f;

    const float activeScale = state.scaleOverride > 0.0f ? state.scaleOverride : state.dpiScale;
    const bool isOverridden = state.scaleOverride > 0.0f;
    const int percent = static_cast<int>(std::round(activeScale * 100.0f));

    ui.column("scale.tab.container")
        .width(state.panel.width)
        .height(contentHeight)
        .content([&] {
            // ---- Top Sub-toolbar (Docked switch strip) ----
            ui.stack("scale.toolbar")
                .width(state.panel.width)
                .height(toolbarHeight)
                .content([&] {
                    ui.rect("scale.toolbar.bg")
                        .fill()
                        .ignoreLayout()
                        .color(theme.toolbarBackground)
                        .build();

                    ui.rect("scale.toolbar.border")
                        .position(0.0f, toolbarHeight - 1.0f)
                        .size(state.panel.width, 1.0f)
                        .ignoreLayout()
                        .color(theme.panelBorder)
                        .build();

                    ui.row("scale.toolbar.items")
                        .fill()
                        .padding(theme.toolbarPadding, 0.0f, theme.toolbarPadding, 0.0f)
                        .alignItems(core::Align::CENTER)
                        .gap(6.0f)
                        .content([&] {
                            ui.text("scale.tb.label")
                                .fontFamily(theme.fontFamily)
                                .width(core::SizeValue::wrapContent())
                                .height(toolbarHeight)
                                .text("SCALE:")
                                .fontSize(theme.captionFontSize)
                                .fontWeight(600)
                                .color(theme.sectionLabel)
                                .verticalAlign(core::VerticalAlign::Center)
                                .build();

                            struct Preset {
                                const char* label;
                                float scale;
                            };
                            const Preset kPresets[] = {
                                {"100%", 1.00f},
                                {"125%", 1.25f},
                                {"150%", 1.50f},
                                {"175%", 1.75f},
                                {"200%", 2.00f},
                                {"250%", 2.50f}
                            };

                            for (const auto& p : kPresets) {
                                const bool selected = std::fabs(activeScale - p.scale) < 0.02f;
                                composePresetButton(ui, "scale.btn." + std::string(p.label),
                                                    p.label, selected, [set = actions.scale.setScaleOverride, target = p.scale] {
                                    if (set) set(target);
                                });
                            }

                            ui.stack("scale.tb.spacer")
                                .width(core::SizeValue::fill())
                                .height(1.0f)
                                .build();

                            // Reset Button
                            composePresetButton(ui, "scale.btn.reset", "Reset", !isOverridden, [set = actions.scale.setScaleOverride] {
                                if (set) set(0.0f);
                            });
                        })
                        .build();
                })
                .build();

            // ---- Main Data Table Area (Flush, borderless, industrial grid) ----
            components::scrollView(ui, "scale.scroll")
                .size(state.panel.width, scrollHeight)
                .offset(scrollOffset)
                .scrollbarGap(0.0f)
                .onChange(actions.scale.setScrollOffset)
                .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
                    contentUi.column("scale.table.col")
                        .width(contentWidth)
                        .height(core::SizeValue::wrapContent())
                        .content([&] {
                            // Section 1: Display & Resolution
                            composeSectionHeader(contentUi, "scale.sec.display", "DISPLAY & RESOLUTION");

                            composeTableRow(contentUi, "scale.row.scale", "Scale Factor",
                                            std::to_string(percent) + "% (" + formatFloat(activeScale, 2) + "x)",
                                            theme.accent);

                            composeTableRow(contentUi, "scale.row.logical", "Logical Resolution",
                                            formatFloat(state.width, 0) + " x " + formatFloat(state.height, 0) + " pt",
                                            theme.metricValue);

                            composeTableRow(contentUi, "scale.row.physical", "Physical Framebuffer",
                                            std::to_string(state.framebufferWidth) + " x " + std::to_string(state.framebufferHeight) + " px",
                                            core::Color{0.45f, 0.85f, 0.58f, 1.0f});

                            composeTableRow(contentUi, "scale.row.ratio", "Effective Pixel Ratio",
                                            formatFloat(activeScale, 4),
                                            theme.primaryText);

                            composeTableRow(contentUi, "scale.row.system", "System Baseline DPI",
                                            formatFloat(state.systemDpi, 2) + "x (" + std::to_string(static_cast<int>(std::round(state.systemDpi * 100.0f))) + "%)",
                                            theme.mutedText);

                            // Section 2: Engine Specifications
                            composeSectionHeader(contentUi, "scale.sec.engine", "ENGINE COORDINATE PIPELINE");

                            composeTableRow(contentUi, "scale.row.coord", "Coordinate Unit",
                                            "1.00 pt = " + formatFloat(activeScale, 2) + " px",
                                            theme.primaryText);

                            composeTableRow(contentUi, "scale.row.model", "Viewport Mapping",
                                            "windowWidth / effectiveScale x windowHeight / effectiveScale",
                                            theme.mutedText);

                            composeTableRow(contentUi, "scale.row.target", "Rasterizer Target",
                                            "OpenGL Retained Framebuffer",
                                            theme.mutedText);
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
