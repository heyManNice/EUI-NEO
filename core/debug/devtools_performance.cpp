#include "core/debug/devtools_performance.h"

#include "components/scrollview.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace core::debug {

namespace {

std::string metricValue(double value, int decimals = 1, const char* suffix = "") {
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f%s", decimals, value, suffix);
    return text;
}

void composeSection(core::dsl::Ui& ui, const std::string& id, const std::string& label) {
    ui.text(id)
        .width(core::SizeValue::fill())
        .height(25.0f)
        .text(label)
        .fontSize(13.0f)
        .color("#91C1FF")
        .build();
}

void composeMetric(core::dsl::Ui& ui, const std::string& id, const std::string& label, const std::string& value) {
    ui.row(id)
        .width(core::SizeValue::fill())
        .height(21.0f)
        .alignItems(core::Align::CENTER)
        .content([&] {
            ui.text(id + ".label")
                .width(core::SizeValue::fill())
                .height(21.0f)
                .text(label)
                .fontSize(12.0f)
                .color("#AAB7C6")
                .build();
            ui.text(id + ".value")
                .width(core::SizeValue::wrapContent())
                .height(21.0f)
                .text(value)
                .fontSize(12.0f)
                .color("#ECF3FA")
                .build();
        })
        .build();
}

void composePerformanceMetrics(core::dsl::Ui& ui, const app::PerformanceSnapshot& snapshot, float width) {
    ui.column("performance.metrics")
        .width(width)
        .height(core::SizeValue::wrapContent())
        .padding(18.0f, 16.0f, 18.0f, 16.0f)
        .gap(4.0f)
        .content([&] {
            ui.text("performance.title")
                .width(core::SizeValue::fill())
                .height(28.0f)
                .text("Performance")
                .fontSize(19.0f)
                .color("#ECF3FA")
                .build();
            if (snapshot.revision == 0) {
                ui.text("performance.waiting")
                    .width(core::SizeValue::fill())
                    .height(24.0f)
                    .text("Waiting for the first sample...")
                    .fontSize(13.0f)
                    .color("#9CA9B8")
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
    const float contentHeight = std::max(0.0f, state.panel.height - kDevtoolsToolbarHeight - (state.detached ? 0.0f : 1.0f));
    components::scrollView(ui, "performance.scroll")
        .size(state.panel.width, contentHeight)
        .offset(state.performanceScrollOffset)
        .onChange(actions.setPerformanceScrollOffset)
        .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
            composePerformanceMetrics(contentUi, state.performance, contentWidth);
        })
        .build();
}

} // namespace core::debug
