#include "modules/devtools/devtools_scale.h"

#include "components/scrollview.h"
#include "modules/devtools/devtools_theme.h"

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
        : core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.70f};
    const core::Color border = selected
        ? theme.accent
        : core::Color{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.40f};
    const core::Color text = selected ? core::Color{1.0f, 1.0f, 1.0f, 1.0f} : theme.primaryText;

    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(26.0f)
        .content([&] {
            ui.rect(id + ".bg")
                .fill()
                .ignoreLayout()
                .color(bg)
                .border(1.0f, border)
                .radius(4.0f)
                .build();
            ui.text(id + ".lbl")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::wrapContent())
                .height(26.0f)
                .padding(10.0f, 0.0f, 10.0f, 0.0f)
                .text(label)
                .fontSize(11.5f)
                .fontWeight(selected ? 600 : 400)
                .color(text)
                .horizontalAlign(core::HorizontalAlign::Center)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .onClick(onClick)
        .build();
}

void composeScaleContent(core::dsl::Ui& ui, const DevtoolsUiState& state,
                         const DevtoolsUiActions& actions, float availableWidth) {
    const DevtoolsTheme& theme = devtoolsTheme();

    const float activeScale = state.scaleOverride > 0.0f ? state.scaleOverride : state.dpiScale;
    const bool isOverridden = state.scaleOverride > 0.0f;
    const int percent = static_cast<int>(std::round(activeScale * 100.0f));

    const core::Color cardBg{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.45f};
    const core::Color cardBorder{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.32f};
    const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.20f};

    ui.column("scale.root")
        .width(availableWidth)
        .height(core::SizeValue::wrapContent())
        .gap(12.0f)
        .content([&] {
            // Main unified card
            ui.stack("scale.main.card")
                .width(availableWidth)
                .height(core::SizeValue::wrapContent())
                .content([&] {
                    ui.rect("scale.main.bg")
                        .fill()
                        .ignoreLayout()
                        .color(cardBg)
                        .radius(6.0f)
                        .border(1.0f, cardBorder)
                        .build();

                    ui.column("scale.main.content")
                        .width(core::SizeValue::fill())
                        .height(core::SizeValue::wrapContent())
                        .padding(16.0f, 14.0f, 16.0f, 16.0f)
                        .gap(12.0f)
                        .content([&] {
                            // ---- Header Row ----
                            ui.row("scale.header.row")
                                .width(core::SizeValue::fill())
                                .height(20.0f)
                                .alignItems(core::Align::CENTER)
                                .content([&] {
                                    ui.text("scale.header.title")
                                        .fontFamily(theme.fontFamily)
                                        .width(core::SizeValue::wrapContent())
                                        .height(20.0f)
                                        .text("SCALE & RESOLUTION")
                                        .fontSize(12.0f)
                                        .fontWeight(600)
                                        .color(theme.sectionLabel)
                                        .build();

                                    ui.stack("scale.header.spacer")
                                        .width(core::SizeValue::fill())
                                        .height(1.0f)
                                        .build();

                                    if (isOverridden) {
                                        ui.text("scale.badge.override")
                                            .fontFamily(theme.fontFamily)
                                            .width(core::SizeValue::wrapContent())
                                            .height(20.0f)
                                            .padding(6.0f, 0.0f, 6.0f, 0.0f)
                                            .text("Live Override")
                                            .fontSize(10.5f)
                                            .fontWeight(600)
                                            .color(core::Color{0.45f, 0.85f, 0.58f, 1.0f})
                                            .build();
                                    } else {
                                        ui.text("scale.badge.system")
                                            .fontFamily(theme.fontFamily)
                                            .width(core::SizeValue::wrapContent())
                                            .height(20.0f)
                                            .text("System Default")
                                            .fontSize(10.5f)
                                            .color(theme.mutedText)
                                            .build();
                                    }
                                })
                                .build();

                            // ---- Section 1: Three Core Metrics ----
                            ui.row("scale.metrics.row")
                                .width(core::SizeValue::fill())
                                .height(core::SizeValue::wrapContent())
                                .alignItems(core::Align::START)
                                .gap(8.0f)
                                .content([&] {
                                    // 1. Current Scale
                                    ui.column("scale.metric.scale")
                                        .width(core::SizeValue::fill())
                                        .height(core::SizeValue::wrapContent())
                                        .gap(2.0f)
                                        .content([&] {
                                            ui.text("scale.m.scale.lbl")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(14.0f)
                                                .text("CURRENT SCALE")
                                                .fontSize(11.0f)
                                                .color(theme.metricLabel)
                                                .build();
                                            ui.text("scale.m.scale.val")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(26.0f)
                                                .text(std::to_string(percent) + "% (" + formatFloat(activeScale, 2) + "x)")
                                                .fontSize(20.0f)
                                                .fontWeight(600)
                                                .color(theme.accent)
                                                .build();
                                        })
                                        .build();

                                    // 2. Logical Resolution
                                    ui.column("scale.metric.logical")
                                        .width(core::SizeValue::fill())
                                        .height(core::SizeValue::wrapContent())
                                        .gap(2.0f)
                                        .content([&] {
                                            ui.text("scale.m.log.lbl")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(14.0f)
                                                .text("LOGICAL RESOLUTION")
                                                .fontSize(11.0f)
                                                .color(theme.metricLabel)
                                                .build();
                                            ui.text("scale.m.log.val")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(26.0f)
                                                .text(formatFloat(state.width, 0) + " x " + formatFloat(state.height, 0) + " pt")
                                                .fontSize(18.0f)
                                                .fontWeight(500)
                                                .color(theme.metricValue)
                                                .build();
                                        })
                                        .build();

                                    // 3. Physical Hardware Pixels
                                    ui.column("scale.metric.physical")
                                        .width(core::SizeValue::fill())
                                        .height(core::SizeValue::wrapContent())
                                        .gap(2.0f)
                                        .content([&] {
                                            ui.text("scale.m.phy.lbl")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(14.0f)
                                                .text("PHYSICAL FRAMEBUFFER")
                                                .fontSize(11.0f)
                                                .color(theme.metricLabel)
                                                .build();
                                            ui.text("scale.m.phy.val")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(26.0f)
                                                .text(std::to_string(state.framebufferWidth) + " x " + std::to_string(state.framebufferHeight) + " px")
                                                .fontSize(18.0f)
                                                .fontWeight(500)
                                                .color(core::Color{0.45f, 0.85f, 0.58f, 1.0f})
                                                .build();
                                        })
                                        .build();
                                })
                                .build();

                            // ---- Divider ----
                            ui.rect("scale.div")
                                .width(core::SizeValue::fill())
                                .height(1.0f)
                                .color(dividerColor)
                                .build();

                            // ---- Section 2: Live Scale Override Controls ----
                            ui.column("scale.controls.col")
                                .width(core::SizeValue::fill())
                                .height(core::SizeValue::wrapContent())
                                .gap(8.0f)
                                .content([&] {
                                    ui.text("scale.controls.title")
                                        .fontFamily(theme.fontFamily)
                                        .width(core::SizeValue::fill())
                                        .height(16.0f)
                                        .text("LIVE SCALE OVERRIDE")
                                        .fontSize(11.5f)
                                        .fontWeight(600)
                                        .color(theme.sectionLabel)
                                        .build();

                                    // Preset scale buttons
                                    ui.row("scale.presets.row")
                                        .width(core::SizeValue::fill())
                                        .height(26.0f)
                                        .alignItems(core::Align::CENTER)
                                        .gap(6.0f)
                                        .content([&] {
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

                                            ui.stack("scale.btn.spacer")
                                                .width(core::SizeValue::fill())
                                                .height(1.0f)
                                                .build();

                                            // Reset Button
                                            composePresetButton(ui, "scale.btn.reset", "Reset (System)", !isOverridden, [set = actions.scale.setScaleOverride] {
                                                if (set) set(0.0f);
                                            });
                                        })
                                        .build();
                                })
                                .build();
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace

void composeScaleTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float scrollOffset = state.panelState != nullptr ? state.panelState->scaleScrollOffset : 0.0f;
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));

    components::scrollView(ui, "scale.scroll")
        .size(state.panel.width, contentHeight)
        .offset(scrollOffset)
        .scrollbarGap(0.0f)
        .onChange(actions.scale.setScrollOffset)
        .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
            const float paddingHoriz = 16.0f;
            const float availableWidth = std::max(100.0f, contentWidth - paddingHoriz * 2.0f);

            contentUi.column("scale.content.wrapper")
                .width(contentWidth)
                .height(core::SizeValue::wrapContent())
                .padding(paddingHoriz, 14.0f, paddingHoriz, 20.0f)
                .content([&] {
                    composeScaleContent(contentUi, state, actions, availableWidth);
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
