#pragma once

#include "core/dsl_runtime.h"
#include "core/debug/devtools_ui.h"

#include <functional>

namespace core::debug {

class DevtoolsHost {
public:
    bool beginFrame(core::window::Handle window,
                    int framebufferWidth,
                    int framebufferHeight,
                    float dpiScale,
                    bool inputEnabled);

    Rect contentBounds() const;
    int contentHeight() const;
    bool visible() const { return visible_; }
    DockPosition dockPosition() const { return dockPosition_; }
    void setDetachedWindowOpener(std::function<void()> opener);
    void setDetachedWindowCloser(std::function<void()> closer);
    void detachedWindowClosed();
    void composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen);
    void handleDetachedKey(const KeyEvent& key);
    void filterInput(std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent);
    bool update();
    void updateCursor(core::window::Handle window);
    void render(int width, int height, float dpiScale, const Rect* dirtyRect);
    void releaseGraphicsResources();
    void shutdown();

private:
    int panelSize() const;
    int minimumPanelSize() const;
    int maximumPanelSize() const;
    Rect panelBounds() const;
    void selectDockPosition(DockPosition position);
    void close();
    void composeUi(core::dsl::Ui& ui, float width, float height, const Rect& panel, bool detached);
    bool overResizeBoundary(double x, double y) const;
    void resetCursor();

    core::dsl::Runtime runtime_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float dpiScale_ = 1.0f;
    float panelHeightLogical_ = 0.0f;
    float panelWidthLogical_ = 0.0f;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
    int dragStartSize_ = 0;
    bool resizing_ = false;
    bool resizeCursorActive_ = false;
    bool resizeCursorApplied_ = false;
    core::window::CursorHandle handCursor_ = nullptr;
    core::window::Handle cursorWindow_ = nullptr;
    bool visible_ = false;
    DockPosition dockPosition_ = DockPosition::Bottom;
    std::function<void()> detachedWindowOpener_;
    std::function<void()> detachedWindowCloser_;
    bool moreMenuOpen_ = false;
    bool composeRequested_ = true;
};

DevtoolsHost& devtoolsHost();

} // namespace core::debug
