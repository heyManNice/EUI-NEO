#include "modules/devtools/performance.h"

#include "components/scrollview.h"
#include "modules/devtools/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace modules::devtools {

namespace {

std::string metricValue(double value, int decimals = 1, const char* suffix = "") {
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f%s", decimals, value, suffix);
    return text;
}

void composeHeroCard(core::dsl::Ui& ui, const std::string& id, float cardWidth,
                     const char* title, const std::string& value,
                     const core::Color& valueColor, const char* subtitle) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color cardBg{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.40f};
    const core::Color cardBorder = theme.panelBorder;

    ui.stack(id)
        .width(cardWidth)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .color(cardBg)
                .radius(0.0f)
                .border(1.0f, cardBorder)
                .build();
            ui.column(id + ".content")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(10.0f, 8.0f, 10.0f, 8.0f)
                .gap(2.0f)
                .content([&] {
                    ui.text(id + ".title")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(16.0f)
                        .text(title)
                        .fontSize(theme.captionFontSize)
                        .color(theme.metricLabel)
                        .build();
                    ui.text(id + ".value")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(26.0f)
                        .text(value)
                        .fontSize(theme.metricValueFontSize)
                        .fontWeight(600)
                        .color(valueColor)
                        .build();
                    ui.text(id + ".sub")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(15.0f)
                        .text(subtitle)
                        .fontSize(theme.captionFontSize)
                        .color(theme.mutedText)
                        .build();
                })
                .build();
        })
        .build();
}

void composeCardHeader(core::dsl::Ui& ui, const std::string& id, const std::string& title) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color dividerColor = theme.panelBorder;

    ui.row(id + ".header")
        .width(core::SizeValue::fill())
        .height(20.0f)
        .alignItems(core::Align::CENTER)
        .content([&] {
            ui.text(id + ".title")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::fill())
                .height(20.0f)
                .text(title)
                .fontSize(theme.captionFontSize)
                .fontWeight(600)
                .color(theme.sectionLabel)
                .build();
        })
        .build();

    ui.rect(id + ".divider")
        .width(core::SizeValue::fill())
        .height(1.0f)
        .color(dividerColor)
        .build();
}

void composeMetric(core::dsl::Ui& ui, const std::string& id, const std::string& label, const std::string& value) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.row(id)
        .width(core::SizeValue::fill())
        .height(21.0f)
        .alignItems(core::Align::CENTER)
        .content([&] {
            ui.text(id + ".label")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::fill())
                .height(21.0f)
                .text(label)
                .fontSize(theme.captionFontSize)
                .color(theme.metricLabel)
                .build();
            ui.text(id + ".value")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::wrapContent())
                .height(21.0f)
                .text(value)
                .fontSize(theme.captionFontSize)
                .color(theme.metricValue)
                .build();
        })
        .build();
}

template <typename ContentFn>
void composeCard(core::dsl::Ui& ui, const std::string& id, float width, ContentFn&& content) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color cardBg{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.35f};
    const core::Color cardBorder = theme.panelBorder;

    ui.stack(id)
        .width(width)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .color(cardBg)
                .radius(0.0f)
                .border(1.0f, cardBorder)
                .build();
            ui.column(id + ".content")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(10.0f, 8.0f, 10.0f, 8.0f)
                .gap(3.0f)
                .content(std::forward<ContentFn>(content))
                .build();
        })
        .build();
}

void composePerformanceMetrics(core::dsl::Ui& ui, const app::PerformanceSnapshot& snapshot, float width, float panelWidth) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float padLeft = 14.0f;
    const float padY = 12.0f;
    const float scrollbarAllowance = std::max(0.0f, panelWidth - width);
    const float padRight = std::max(0.0f, padLeft - scrollbarAllowance);
    const float availableWidth = std::max(60.0f, width - padLeft - padRight);

    ui.column("performance.metrics")
        .width(width)
        .height(core::SizeValue::wrapContent())
        .padding(padLeft, padY, padRight, padY)
        .gap(12.0f)
        .content([&] {
            if (snapshot.revision == 0) {
                ui.text("performance.waiting")
                    .fontFamily(theme.fontFamily)
                    .width(core::SizeValue::fill())
                    .height(theme.sectionHeight)
                    .text("Waiting for the first sample...")
                    .fontSize(theme.sectionFontSize)
                    .color(theme.mutedText)
                    .build();
                return;
            }

            // --- 1. Top Section: Key Performance Indicators ---
            const core::Color fpsColor = core::Color{0.30f, 0.85f, 0.50f, 1.0f};

            core::Color frameTimeColor = theme.metricValue;
            if (snapshot.hasRenderDuration) {
                frameTimeColor = snapshot.renderDurationMs <= 16.7
                                     ? core::Color{0.30f, 0.85f, 0.50f, 1.0f}
                                     : core::Color{0.95f, 0.75f, 0.25f, 1.0f};
            }

            const std::string fpsText = metricValue(snapshot.framesPerSecond, 1);
            const std::string frameTimeText = snapshot.hasRenderDuration ? metricValue(snapshot.renderDurationMs, 2, " ms") : "n/a";
            const std::string cpuText = snapshot.processUsage.hasCpuPercent ? metricValue(snapshot.processUsage.cpuPercent, 0, "%") : "n/a";
            const std::string gpuText = snapshot.processUsage.hasGpuPercent ? metricValue(snapshot.processUsage.gpuPercent, 0, "%") : "n/a";

            const float heroGap = 8.0f;
            if (availableWidth >= 560.0f) {
                const float heroWidth = std::floor((availableWidth - 3.0f * heroGap) / 4.0f);
                ui.row("performance.hero.row")
                    .width(core::SizeValue::fill())
                    .height(core::SizeValue::wrapContent())
                    .gap(heroGap)
                    .content([&] {
                        composeHeroCard(ui, "performance.hero.fps", heroWidth, "FPS", fpsText, fpsColor, "Adaptive rate");
                        composeHeroCard(ui, "performance.hero.render", heroWidth, "Frame Time", frameTimeText, frameTimeColor, "Render + present");
                        composeHeroCard(ui, "performance.hero.cpu", heroWidth, "Process CPU", cpuText, theme.accent, "App thread");
                        composeHeroCard(ui, "performance.hero.gpu", heroWidth, "Process GPU", gpuText, theme.accent, "Graphics driver");
                    })
                    .build();
            } else if (availableWidth >= 280.0f) {
                const float heroWidth = std::floor((availableWidth - heroGap) / 2.0f);
                ui.row("performance.hero.row1")
                    .width(core::SizeValue::fill())
                    .height(core::SizeValue::wrapContent())
                    .gap(heroGap)
                    .content([&] {
                        composeHeroCard(ui, "performance.hero.fps", heroWidth, "FPS", fpsText, fpsColor, "Adaptive rate");
                        composeHeroCard(ui, "performance.hero.render", heroWidth, "Frame Time", frameTimeText, frameTimeColor, "Render + present");
                    })
                    .build();
                ui.row("performance.hero.row2")
                    .width(core::SizeValue::fill())
                    .height(core::SizeValue::wrapContent())
                    .gap(heroGap)
                    .content([&] {
                        composeHeroCard(ui, "performance.hero.cpu", heroWidth, "Process CPU", cpuText, theme.accent, "App thread");
                        composeHeroCard(ui, "performance.hero.gpu", heroWidth, "Process GPU", gpuText, theme.accent, "Graphics driver");
                    })
                    .build();
            } else {
                composeHeroCard(ui, "performance.hero.fps", availableWidth, "FPS", fpsText, fpsColor, "Adaptive rate");
                composeHeroCard(ui, "performance.hero.render", availableWidth, "Frame Time", frameTimeText, frameTimeColor, "Render + present");
                composeHeroCard(ui, "performance.hero.cpu", availableWidth, "Process CPU", cpuText, theme.accent, "App thread");
                composeHeroCard(ui, "performance.hero.gpu", availableWidth, "Process GPU", gpuText, theme.accent, "Graphics driver");
            }

            if (!snapshot.hasRenderStats) {
                return;
            }

            // --- 2. Bottom Section: Detailed Engine & Backend Metrics ---
            const app::RenderStatsAverages& render = snapshot.render;

            auto composePaintCard = [&](float cardWidth) {
                composeCard(ui, "performance.card.paint", cardWidth, [&] {
                    composeCardHeader(ui, "performance.paint", "Paint and cache");
                    composeMetric(ui, "performance.dirty.rects", "Dirty rects", metricValue(render.dirtyRects));
                    composeMetric(ui, "performance.dirty.area", "Dirty area", metricValue(render.dirtyAreaPercent, 0, "%"));
                    composeMetric(ui, "performance.blit.area", "Blit area", metricValue(render.blitAreaPercent, 0, "%"));
                    composeMetric(ui, "performance.full", "Full paint", metricValue(render.fullPaintPercent, 0, "%"));
                    composeMetric(ui, "performance.cache", "Render cache used", metricValue(render.renderCachePercent, 0, "%"));
                    composeMetric(ui, "performance.cache.recreated", "Cache recreated", metricValue(render.renderCacheRecreatedPercent, 0, "%"));
                    composeMetric(ui, "performance.cache.blits", "Cache blits", metricValue(render.cacheBlits));
                    composeMetric(ui, "performance.direct", "Direct passes", metricValue(render.renderDirectPasses));
                    composeMetric(ui, "performance.clear", "Clear calls", metricValue(render.clearCalls));
                });
            };

            auto composeDrawCard = [&](float cardWidth) {
                composeCard(ui, "performance.card.draw", cardWidth, [&] {
                    composeCardHeader(ui, "performance.draw", "Draw and layers");
                    composeMetric(ui, "performance.rect", "Rect draws", metricValue(render.rectDraws));
                    composeMetric(ui, "performance.polygon", "Polygon draws", metricValue(render.polygonDraws));
                    composeMetric(ui, "performance.text.prepare", "Text prepares", metricValue(render.textPrepares));
                    composeMetric(ui, "performance.text.draw", "Text draws", metricValue(render.textDraws));
                    composeMetric(ui, "performance.image", "Image draws", metricValue(render.imageDraws));
                    composeMetric(ui, "performance.batch.flush", "Batch flushes", metricValue(render.textBatchFlushes));
                    composeMetric(ui, "performance.batch.vertices", "Batch vertices", metricValue(render.textBatchVertices, 0));
                    composeMetric(ui, "performance.blur.capture", "Blur captures", metricValue(render.backdropCaptures));
                    composeMetric(ui, "performance.blur.reuse", "Blur reuses", metricValue(render.backdropCaptureReuses));
                    composeMetric(ui, "performance.layer.hit", "Layer hits", metricValue(render.retainedLayerHits));
                    composeMetric(ui, "performance.layer.miss", "Layer misses", metricValue(render.retainedLayerMisses));
                    composeMetric(ui, "performance.layer.draw", "Layer draws", metricValue(render.retainedLayerDraws));
                    composeMetric(ui, "performance.layer.rebuild", "Layer rebuilds", metricValue(render.retainedLayerRebuilds));
                });
            };

            auto composeBackendCard = [&](float cardWidth) {
                composeCard(ui, "performance.card.backend", cardWidth, [&] {
                    composeCardHeader(ui, "performance.backend", "Backend");
                    composeMetric(ui, "performance.pass", "Render passes", metricValue(render.backendRenderPasses));
                    composeMetric(ui, "performance.pass.area", "Pass area", metricValue(render.backendRenderPassAreaPercent, 0, "%"));
                    composeMetric(ui, "performance.copy", "Copy regions", metricValue(render.backendCopyRegions));
                    composeMetric(ui, "performance.barrier", "Barriers", metricValue(render.backendBarriers));
                    composeMetric(ui, "performance.submit", "Submits", metricValue(render.backendSubmits));
                    composeMetric(ui, "performance.present", "Presents", metricValue(render.backendPresents));
                    composeMetric(ui, "performance.present.area", "Present area", metricValue(render.backendPresentAreaPercent, 0, "%"));
                    composeMetric(ui, "performance.incremental", "Incremental presents", metricValue(render.backendIncrementalPresents));
                    composeMetric(ui, "performance.incremental.support", "Incremental support", metricValue(render.backendIncrementalPresentSupported));
                    composeMetric(ui, "performance.resolve", "Resolve draws", metricValue(render.backendResolveDraws));
                });
            };

            const float colGap = 10.0f;
            if (availableWidth >= 860.0f) {
                // Wide layout (e.g. bottom docked panel): 3 cards side by side
                const float cardWidth = std::floor((availableWidth - 2.0f * colGap) / 3.0f);
                ui.row("performance.cards.row")
                    .width(core::SizeValue::fill())
                    .height(core::SizeValue::wrapContent())
                    .alignItems(core::Align::START)
                    .gap(colGap)
                    .content([&] {
                        composePaintCard(cardWidth);
                        composeDrawCard(cardWidth);
                        composeBackendCard(cardWidth);
                    })
                    .build();
            } else if (availableWidth >= 540.0f) {
                // Medium layout: 2 columns
                const float cardWidth = std::floor((availableWidth - colGap) / 2.0f);
                ui.row("performance.cards.row")
                    .width(core::SizeValue::fill())
                    .height(core::SizeValue::wrapContent())
                    .alignItems(core::Align::START)
                    .gap(colGap)
                    .content([&] {
                        ui.column("performance.cards.col0")
                            .width(cardWidth)
                            .height(core::SizeValue::wrapContent())
                            .gap(colGap)
                            .content([&] {
                                composePaintCard(cardWidth);
                                composeBackendCard(cardWidth);
                            })
                            .build();
                        ui.column("performance.cards.col1")
                            .width(cardWidth)
                            .height(core::SizeValue::wrapContent())
                            .content([&] {
                                composeDrawCard(cardWidth);
                            })
                            .build();
                    })
                    .build();
            } else {
                // Narrow layout (e.g. side docked panel): 1 column
                ui.column("performance.cards.col")
                    .width(availableWidth)
                    .height(core::SizeValue::wrapContent())
                    .gap(colGap)
                    .content([&] {
                        composePaintCard(availableWidth);
                        composeDrawCard(availableWidth);
                        composeBackendCard(availableWidth);
                    })
                    .build();
            }
        })
        .build();
}

} // namespace

void composePerformanceTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    static const app::PerformanceSnapshot emptySnapshot;
    const DevtoolsTheme& theme = devtoolsTheme();
    const app::PerformanceSnapshot& snapshot = state.performance != nullptr ? *state.performance : emptySnapshot;
    const float scrollOffset = state.panelState != nullptr ? state.panelState->performanceScrollOffset : 0.0f;
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));
    components::scrollView(ui, "performance.scroll")
        .size(state.panel.width, contentHeight)
        .offset(scrollOffset)
        .scrollbarGap(0.0f)
        .onChange(actions.performance.setScrollOffset)
        .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
            composePerformanceMetrics(contentUi, snapshot, contentWidth, state.panel.width);
        })
        .build();
}

} // namespace modules::devtools
