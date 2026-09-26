#pragma once

#include "core/platform/performance_stats.h"

#include <cstdint>

namespace app {

struct RenderStatsAverages {
    double dirtyRects = 0.0;
    double dirtyAreaPercent = 0.0;
    double blitAreaPercent = 0.0;
    double rectDraws = 0.0;
    double polygonDraws = 0.0;
    double textPrepares = 0.0;
    double textDraws = 0.0;
    double imageDraws = 0.0;
    double textBatchFlushes = 0.0;
    double textBatchVertices = 0.0;
    double backdropCaptures = 0.0;
    double backdropCaptureReuses = 0.0;
    double retainedLayerHits = 0.0;
    double retainedLayerMisses = 0.0;
    double retainedLayerDraws = 0.0;
    double retainedLayerRebuilds = 0.0;
    double renderDirectPasses = 0.0;
    double clearCalls = 0.0;
    double cacheBlits = 0.0;
    double backendRenderPasses = 0.0;
    double backendRenderPassAreaPercent = 0.0;
    double backendCopyRegions = 0.0;
    double backendBarriers = 0.0;
    double backendSubmits = 0.0;
    double backendPresents = 0.0;
    double backendPresentAreaPercent = 0.0;
    double backendIncrementalPresents = 0.0;
    double backendIncrementalPresentSupported = 0.0;
    double backendResolveDraws = 0.0;
    double fullPaintPercent = 0.0;
    double renderCachePercent = 0.0;
    double renderCacheRecreatedPercent = 0.0;
};

struct PerformanceSnapshot {
    std::uint64_t revision = 0;
    double elapsedSeconds = 0.0;
    double framesPerSecond = 0.0;
    core::platform::ProcessUsageSample processUsage;
    bool hasRenderDuration = false;
    double renderDurationMs = 0.0;
    bool hasRenderStats = false;
    RenderStatsAverages render;
};

} // namespace app
