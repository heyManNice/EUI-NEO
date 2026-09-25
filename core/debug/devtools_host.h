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
    void updateCursor(core::window::Handle window);
    void render(int width, int height, float dpiScale, const Rect* dirtyRect);
    void releaseGraphicsResources();
    void shutdown();

private:
    int panelHeight() const;
    int minimumPanelHeight() const;
    int maximumPanelHeight() const;
    bool overResizeBoundary(double x, double y) const;
    void resetCursor();

    core::dsl::Runtime runtime_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float dpiScale_ = 1.0f;
    float panelHeightLogical_ = 0.0f;
    double dragStartY_ = 0.0;
    int dragStartHeight_ = 0;
    bool resizing_ = false;
    bool resizeCursorActive_ = false;
    bool resizeCursorApplied_ = false;
    core::window::CursorHandle handCursor_ = nullptr;
    core::window::Handle cursorWindow_ = nullptr;
    bool visible_ = false;
    bool moreMenuOpen_ = false;
    bool composeRequested_ = true;
};

DevtoolsHost& devtoolsHost();

} // namespace core::debug
