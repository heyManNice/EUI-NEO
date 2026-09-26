#include "modules/devtools/devtools_performance.h"

#include "components/scrollview.h"
#include "modules/devtools/devtools_theme.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace modules::devtools {

namespace {

std::string metricValue(double value, int decimals = 1, const char* suffix = "") {
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f%s", decimals, value, suffix);
    return text;
}

void composeSection(core::dsl::Ui& ui, const std::string& id, const std::string& label) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id)
        .width(core::SizeValue::fill())
        .height(theme.sectionHeight)
        .text(label)
        .fontSize(theme.sectionFontSize)
        .color(theme.sectionLabel)
        .build();
}

void composeMetric(core::dsl::Ui& ui, const std::string& id, const std::string& label, const std::string& value) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.row(id)
        .width(core::SizeValue::fill())
        .height(theme.metricHeight)
        .alignItems(core::Align::CENTER)
        .content([&] {
            ui.text(id + ".label")
                .width(core::SizeValue::fill())
                .height(theme.metricHeight)
                .text(label)
                .fontSize(theme.metricFontSize)
                .color(theme.metricLabel)
                .build();
            ui.text(id + ".value")
                .width(core::SizeValue::wrapContent())
                .height(theme.metricHeight)
                .text(value)
                .fontSize(theme.metricFontSize)
                .color(theme.metricValue)
                .build();
        })
        .build();
}

void composePerformanceMetrics(core::dsl::Ui& ui, const app::PerformanceSnapshot& snapshot, float width) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.column("performance.metrics")
        .width(width)
        .height(core::SizeValue::wrapContent())
        .padding(theme.metricPaddingHorizontal, theme.metricPaddingVertical,
                 theme.metricPaddingHorizontal, theme.metricPaddingVertical)
        .gap(theme.metricGap)
        .content([&] {
            if (snapshot.revision == 0) {
                ui.text("performance.waiting")
                    .width(core::SizeValue::fill())
                    .height(theme.sectionHeight)
                    .text("Waiting for the first sample...")
                    .fontSize(theme.sectionFontSize)
                    .color(theme.mutedText)
                    .build();
                return;
            }

            composeSection(ui, "performance.overview", "Overview");
            composeMetric(ui, "performance.fps", "FPS", metricValue(snapshot.framesPerSecond));
            composeMetric(ui, "performance.cpu", "Process CPU", snapshot.processUsage.hasCpuPercent ? metricValue(snapshot.processUsage.cpuPercent, 0, "%") : "n/a");
            composeMetric(ui, "performance.gpu", "Process GPU", snapshot.processUsage.hasGpuPercent ? metricValue(snapshot.processUsage.gpuPercent, 0, "%") : "n/a");
            composeMetric(ui, "performance.render", "Render + present", snapshot.hasRenderDuration ? metricValue(snapshot.renderDurationMs, 2, " ms") : "n/a");
            composeMetric(ui, "performance.sample", "Sample period", metricValue(snapshot.elapsedSeconds, 2, " s"));
            if (!snapshot.hasRenderStats) {
                return;
            }

            const app::RenderStatsAverages& render = snapshot.render;
            composeSection(ui, "performance.paint", "Paint and cache");
            composeMetric(ui, "performance.dirty.rects", "Dirty rects", metricValue(render.dirtyRects));
            composeMetric(ui, "performance.dirty.area", "Dirty area", metricValue(render.dirtyAreaPercent, 0, "%"));
            composeMetric(ui, "performance.blit.area", "Blit area", metricValue(render.blitAreaPercent, 0, "%"));
            composeMetric(ui, "performance.full", "Full paint", metricValue(render.fullPaintPercent, 0, "%"));
            composeMetric(ui, "performance.cache", "Render cache used", metricValue(render.renderCachePercent, 0, "%"));
            composeMetric(ui, "performance.cache.recreated", "Cache recreated", metricValue(render.renderCacheRecreatedPercent, 0, "%"));
            composeMetric(ui, "performance.cache.blits", "Cache blits", metricValue(render.cacheBlits));
            composeMetric(ui, "performance.direct", "Direct passes", metricValue(render.renderDirectPasses));
            composeMetric(ui, "performance.clear", "Clear calls", metricValue(render.clearCalls));

            composeSection(ui, "performance.draw", "Draw and layers");
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

            composeSection(ui, "performance.backend", "Backend");
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
        .onChange(actions.setPerformanceScrollOffset)
        .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
            composePerformanceMetrics(contentUi, snapshot, contentWidth);
        })
        .build();
}

} // namespace modules::devtools
