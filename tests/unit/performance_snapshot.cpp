#include "core/app/app_runner.h"

#include <cassert>
#include <string>

namespace app {

bool titleStatsEnabled = false;

bool showDebugStatsInTitle() { return titleStatsEnabled; }
double debugTitleUpdateInterval() { return 1.0; }
const char* windowTitle() { return "Performance Test"; }

} // namespace app

int main() {
    app::AppRunner runner;
    runner.resetTiming(10.0);
    runner.renderedFrames = 60;
    runner.recordRenderDuration(3.5);

    core::render::RenderFrameStats frame;
    frame.rendered = true;
    frame.framebufferWidth = 100;
    frame.framebufferHeight = 100;
    frame.dirtyRectCount = 2;
    frame.dirtyPixels = 2500;
    frame.rectDraws = 7;
    frame.backendSubmits = 3;
    runner.recordRenderStats(frame);

    app::PerformanceSnapshot snapshot;
    std::string title;
    int published = 0;
    const auto setTitle = [&](const char* value) { title = value; };
    const auto publish = [&](const app::PerformanceSnapshot& value) {
        snapshot = value;
        ++published;
    };

    runner.updatePerformanceStats(10.5, setTitle, publish);
    assert(published == 0);
    assert(title.empty());

    runner.updatePerformanceStats(11.0, setTitle, publish);
#if defined(EUI_DEBUG_BUILD)
    assert(published == 1);
    assert(snapshot.revision == 1);
    assert(snapshot.framesPerSecond == 60.0);
    assert(snapshot.hasRenderDuration && snapshot.renderDurationMs == 3.5);
    assert(snapshot.hasRenderStats);
    assert(snapshot.render.dirtyRects == 2.0);
    assert(snapshot.render.dirtyAreaPercent == 25.0);
    assert(snapshot.render.rectDraws == 7.0);
    assert(snapshot.render.backendSubmits == 3.0);
#else
    assert(published == 0);
#endif
    assert(title.empty());

    app::titleStatsEnabled = true;
    runner.renderedFrames = 30;
    runner.updatePerformanceStats(12.0, setTitle, publish);
    assert(published > 0);
    assert(title.find("FPS") != std::string::npos);
}
