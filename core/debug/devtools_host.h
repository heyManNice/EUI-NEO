#pragma once

#include "core/dsl_runtime.h"

namespace core::debug {

class DevtoolsHost {
public:
    bool beginFrame(core::window::Handle window,
                    int framebufferWidth,
                    int framebufferHeight,
                    float dpiScale,
                    bool inputEnabled);

    int contentHeight() const;
    bool visible() const { return visible_; }
    void filterInput(std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent);
    bool update();
    void render(int width, int height, float dpiScale, const Rect* dirtyRect);
    void releaseGraphicsResources();
    void shutdown();

private:
    int panelHeight() const;

    core::dsl::Runtime runtime_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float dpiScale_ = 1.0f;
    bool visible_ = false;
    bool composeRequested_ = true;
};

DevtoolsHost& devtoolsHost();

} // namespace core::debug
