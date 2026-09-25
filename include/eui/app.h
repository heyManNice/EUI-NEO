#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

using DslWindowCompose = std::function<void(eui::Ui&, const eui::Screen&)>;

namespace detail {
struct DslWindowState {
    enum class Phase { Pending, Open, Closed };
    Phase phase = Phase::Pending;
    bool closeRequested = false;
};
}

class DslWindowHandle {
public:
    DslWindowHandle() = default;

    // Window handles are used on the UI thread. A close request is processed by the window manager.
    explicit operator bool() const { return static_cast<bool>(state_); }
    bool isOpen() const { return state_ && state_->phase == detail::DslWindowState::Phase::Open; }
    bool isClosed() const { return state_ && state_->phase == detail::DslWindowState::Phase::Closed; }
    void requestClose() const;

private:
    explicit DslWindowHandle(std::shared_ptr<detail::DslWindowState> state)
        : state_(std::move(state)) {}

    template <typename> friend class DslWindowManager;
    friend DslWindowHandle openWindow(const struct DslWindowConfig&, DslWindowCompose);
    std::shared_ptr<detail::DslWindowState> state_;
};

struct DslWindowRequest {
    std::string title = "Window";
    std::string pageId = "window";
    eui::Color clearColor = {0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    bool modal = false;
    std::function<void(const eui::KeyEvent&)> onKeyEvent;
    std::function<void()> onClosed;
    DslWindowCompose compose;
    DslWindowHandle handle;
};

const char* windowTitle();
bool showDebugStatsInTitle();
double debugTitleUpdateInterval();
bool showDebugOverlay();
double frameRateLimit();
int initialWindowWidth();
int initialWindowHeight();
int initialWindowX();
int initialWindowY();
bool initialWindowPositionSet();
int minimumWindowWidth();
int minimumWindowHeight();
int maximumWindowWidth();
int maximumWindowHeight();
bool windowResizable();
bool windowHighDpi();
bool windowDecorated();
bool windowAlwaysOnTop();
bool windowMaximized();
float uiScale();
bool trayEnabled();
const char* trayTitle();
const char* trayIconPath();
void requestUpdate();
bool initialize(eui::window::Handle window);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested, bool inputEnabled);
bool isAnimating();
void render(int windowWidth, int windowHeight, float dpiScale);
void releaseGraphicsResources();
void shutdown();
std::vector<DslWindowRequest> consumeWindowRequests();

namespace detail {
void requestFullPaint();
}

} // namespace app
