#pragma once

#include "eui/dsl_app.h"
#include "eui/detail/overlay_host.h"
#include "eui/network.h"

#include "3rd/stb_image.h"
#include "core/dsl_runtime.h"
#include "core/app/performance_snapshot.h"
#include "core/platform/platform.h"
#include "core/render/text.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace app {

namespace detail {

inline void publishPerformanceSnapshot(const PerformanceSnapshot& snapshot) {
#if defined(EUI_DEBUG_BUILD)
    detail::OverlayHost* overlay = detail::overlayHost();
    if (overlay != nullptr) {
        overlay->setPerformanceSnapshot(snapshot);
        return;
    }
#endif
    (void)snapshot;
}

inline core::dsl::Runtime& dslRuntime() {
    static core::dsl::Runtime runtime;
    return runtime;
}

inline std::vector<DslWindowRequest>& dslWindowRequests() {
    static std::vector<DslWindowRequest> requests;
    return requests;
}

struct DslAppState {
    bool composed = false;
    bool iconApplied = false;
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
#if defined(EUI_DEBUG_BUILD)
    std::uint64_t elementTreeRevision = 0;
    double elementTreeRefreshTime = 0.0;
    std::string elementPropertiesId;
    std::uint64_t elementPropertiesRevision = 0;
    double elementPropertiesRefreshTime = 0.0;
    bool elementPropertiesStale = true;
#endif
};

inline DslAppState& dslAppState() {
    static DslAppState state;
    return state;
}

#if defined(EUI_DEBUG_BUILD)
// Frame-only changes (animation, scrolling, hover) refresh the published tree at
// most this often. A structural change always refreshes immediately, so a viewer
// reacts to the page it inspects without waiting.
inline constexpr double kElementTreeRefreshSeconds = 0.25;

// Copies the page element tree to the overlay that displays it. Walking the tree
// costs time and memory proportional to the page, and the overlay only pays for it
// while it asks for the tree.
inline void publishElementTree(OverlayHost& overlay) {
    if (!overlay.wantsElementTree()) {
        return;
    }
    DslAppState& state = dslAppState();
    const std::uint64_t revision = dslRuntime().elementStructureRevision();
    const double now = core::window::timeSeconds();
    if (revision == state.elementTreeRevision &&
        now - state.elementTreeRefreshTime < kElementTreeRefreshSeconds) {
        return;
    }
    state.elementTreeRevision = revision;
    state.elementTreeRefreshTime = now;
    overlay.setElementTree(dslRuntime().elementTree());
}

// Properties are read for the single element the overlay shows, and only when it
// asks. A tree walk per frame would cost as much as the page is big, so the read is
// throttled like the tree and repeats immediately after an edit, when the panel has
// to see what its own edit did.
inline void publishElementProperties(OverlayHost& overlay) {
    const std::string& id = overlay.propertiesElement();
    if (id.empty()) {
        return;
    }
    DslAppState& state = dslAppState();
    const std::uint64_t revision = dslRuntime().elementStructureRevision();
    const double now = core::window::timeSeconds();
    const bool sameElement = id == state.elementPropertiesId;
    const bool throttled = revision == state.elementPropertiesRevision &&
                           now - state.elementPropertiesRefreshTime < kElementTreeRefreshSeconds;
    if (sameElement && !state.elementPropertiesStale && throttled) {
        return;
    }
    state.elementPropertiesId = id;
    state.elementPropertiesRevision = revision;
    state.elementPropertiesRefreshTime = now;
    state.elementPropertiesStale = false;
    overlay.setElementProperties(dslRuntime().debugElementProperties(id));
    overlay.setElementPropertyOverrideCount(dslRuntime().debugElementOverrideCount());
}

// Applies the edits the overlay made to the page. This is the only direction that
// writes: the runtime keeps them on top of the app's own values until they are
// cleared, and the app state the page is built from is never touched.
inline void applyElementPropertyEdits(OverlayHost& overlay) {
    OverlayHost::ElementPropertyEdit edit;
    while (overlay.takeElementPropertyEdit(edit)) {
        if (edit.clear && edit.id.empty()) {
            dslRuntime().clearAllDebugElementOverrides();
        } else if (edit.clear) {
            dslRuntime().clearDebugElementOverride(edit.id, edit.property);
        } else {
            switch (core::dsl::runtime::debugPropertyType(edit.property)) {
            case core::dsl::runtime::DebugPropertyType::Number:
                dslRuntime().setDebugElementOverride(edit.id, edit.property, edit.number);
                break;
            case core::dsl::runtime::DebugPropertyType::Color:
                dslRuntime().setDebugElementOverride(edit.id, edit.property, edit.color);
                break;
            case core::dsl::runtime::DebugPropertyType::Flag:
                dslRuntime().setDebugElementOverride(edit.id, edit.property, edit.flag);
                break;
            }
        }
        dslAppState().elementPropertiesStale = true;
    }
    overlay.setElementPropertyOverrideCount(dslRuntime().debugElementOverrideCount());
}
#endif

inline std::string resolveIconPath(const std::string& iconPath) {
    if (iconPath.empty()) {
        return {};
    }

    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path requested(iconPath);
    const fs::path current = fs::current_path(error);
    std::vector<fs::path> candidates;
    candidates.push_back(requested);
    if (!error) {
        candidates.push_back(current / requested);
        candidates.push_back(current / "assets" / requested.filename());
    }

    fs::path executableDir;
#if defined(__APPLE__)
    char executablePath[4096];
    uint32_t executablePathSize = sizeof(executablePath);
    if (_NSGetExecutablePath(executablePath, &executablePathSize) == 0) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(_WIN32)
    char executablePath[MAX_PATH];
    const DWORD executablePathSize = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (executablePathSize > 0 && executablePathSize < MAX_PATH) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(__linux__)
    char executablePath[4096];
    const ssize_t executablePathSize = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
    if (executablePathSize > 0) {
        executablePath[executablePathSize] = '\0';
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#endif
    if (!executableDir.empty()) {
        candidates.push_back(executableDir / requested);
        candidates.push_back(executableDir / "assets" / requested.filename());
    }

    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::exists(candidate, error) && !error) {
            return fs::absolute(candidate, error).string();
        }
    }
    return {};
}

inline void applyWindowIcon(core::window::Handle window) {
    if (window == nullptr) {
        return;
    }

    const std::string iconPath = resolveIconPath(dslAppConfig().iconPathValue);
    if (iconPath.empty()) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(iconPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return;
    }

    core::window::setWindowIcon(window, width, height, pixels);
    stbi_image_free(pixels);
}

} // namespace detail

void DslWindowHandle::requestClose() const {
    if (state_ && state_->phase != detail::DslWindowState::Phase::Closed) {
        state_->closeRequested = true;
        requestUpdate();
    }
}

DslWindowHandle openWindow(const DslWindowConfig& config, DslWindowCompose composeFn) {
    if (!composeFn) {
        return {};
    }

    DslWindowRequest request;
    request.handle = DslWindowHandle(std::make_shared<detail::DslWindowState>());
    request.title = config.titleValue.empty() ? "Window" : config.titleValue;
    request.pageId = config.pageIdValue.empty() ? request.title : config.pageIdValue;
    request.clearColor = config.clearColorValue;
    request.width = std::max(160, config.windowWidthValue);
    request.height = std::max(120, config.windowHeightValue);
    request.modal = config.modalValue;
    request.onKeyEvent = config.keyEventHandler;
    request.onClosed = config.closedHandler;
    request.compose = std::move(composeFn);
    DslWindowHandle handle = request.handle;
    detail::dslWindowRequests().push_back(std::move(request));
    requestUpdate();
    return handle;
}

DslWindowHandle openWindow(const char* title, int width, int height, DslWindowCompose composeFn) {
    return openWindow(DslWindowConfig{}
                   .title(title != nullptr ? title : "Window")
                   .pageId(title != nullptr ? title : "window")
                   .windowSize(width, height),
               std::move(composeFn));
}

std::vector<DslWindowRequest> consumeWindowRequests() {
    std::vector<DslWindowRequest> requests = std::move(detail::dslWindowRequests());
    detail::dslWindowRequests().clear();
    return requests;
}

const char* windowTitle() {
    return dslAppConfig().titleValue.c_str();
}

bool showDebugStatsInTitle() {
    return dslAppConfig().showDebugStatsInTitleValue;
}

double debugTitleUpdateInterval() {
    const double interval = dslAppConfig().debugTitleIntervalValue;
    return std::isfinite(interval) && interval > 0.0 ? interval : 1.0;
}

bool showDebugOverlay() {
    return dslAppConfig().showDebugOverlayValue &&
           static_cast<bool>(dslAppConfig().debugOverlayCompose);
}

double frameRateLimit() {
    return dslAppConfig().fpsValue;
}

int initialWindowWidth() {
    return dslAppConfig().windowWidthValue;
}

int initialWindowHeight() {
    return dslAppConfig().windowHeightValue;
}

int initialWindowX() {
    return dslAppConfig().windowXValue;
}

int initialWindowY() {
    return dslAppConfig().windowYValue;
}

bool initialWindowPositionSet() {
    return dslAppConfig().windowPositionSetValue;
}

int minimumWindowWidth() {
    return dslAppConfig().minWindowWidthValue;
}

int minimumWindowHeight() {
    return dslAppConfig().minWindowHeightValue;
}

int maximumWindowWidth() {
    return dslAppConfig().maxWindowWidthValue;
}

int maximumWindowHeight() {
    return dslAppConfig().maxWindowHeightValue;
}

bool windowResizable() {
    return dslAppConfig().resizableValue;
}

bool windowHighDpi() {
    return dslAppConfig().highDpiValue;
}

bool windowDecorated() {
    return dslAppConfig().decoratedValue;
}

bool windowAlwaysOnTop() {
    return dslAppConfig().alwaysOnTopValue;
}

bool windowMaximized() {
    return dslAppConfig().maximizedValue;
}

float uiScale() {
    const float configuredScale = dslAppConfig().uiScaleValue;
    return configuredScale > 0.0f ? configuredScale : 1.0f;
}

bool trayEnabled() {
    return dslAppConfig().trayEnabledValue;
}

const char* trayTitle() {
    const DslAppConfig& config = dslAppConfig();
    return (config.trayTitleValue.empty() ? config.titleValue : config.trayTitleValue).c_str();
}

const char* trayIconPath() {
    const DslAppConfig& config = dslAppConfig();
    return (config.trayIconPathValue.empty() ? config.iconPathValue : config.trayIconPathValue).c_str();
}

void requestUpdate() {
    core::platform::requestUiUpdate();
}

namespace detail {

void requestFullPaint() {
    dslRuntime().requestFullPaint();
    core::platform::requestUiUpdate();
}

} // namespace detail

bool initialize(core::window::Handle window) {
    const DslAppConfig& config = dslAppConfig();
    core::TextPrimitive::setDefaultFontFiles(config.textFontFileValue, config.iconFontFileValue);
#if defined(EUI_DEBUG_BUILD)
    detail::OverlayHost* overlay = detail::overlayHost();
    if (overlay != nullptr) {
        detail::dslRuntime().setInputFilter([overlay](std::vector<core::PointerEvent>& pointerEvents,
                                                      core::ScrollEvent& scrollEvent) {
            overlay->filterInput(pointerEvents, scrollEvent);
        });
        detail::dslRuntime().setOverlayRenderer([overlay](int width, int height, float dpiScale,
                                                          const core::Rect* dirtyRect) {
            overlay->render(width, height, dpiScale, dirtyRect);
        });
        const std::function<void(const eui::KeyEvent&)> appKeyHandler = config.keyEventHandler;
        detail::dslRuntime().setKeyEventHandler([appKeyHandler](const eui::KeyEvent& key) {
            detail::OverlayHost* active = detail::overlayHost();
            if (active != nullptr && active->handleHotkey(key)) {
                return;
            }
            if (appKeyHandler) {
                appKeyHandler(key);
            }
        });
        // The overlay describes its own window, but the app layer owns window
        // creation and closing.
        const std::shared_ptr<DslWindowHandle> detachedHandle = std::make_shared<DslWindowHandle>();
        const std::shared_ptr<unsigned int> detachedGeneration = std::make_shared<unsigned int>(0);
        overlay->setDetachedWindowOpener([overlay, detachedHandle, detachedGeneration] {
            detail::DetachedWindowOptions options;
            overlay->describeDetachedWindow(options);
            const unsigned int generation = ++*detachedGeneration;
            *detachedHandle = openWindow(DslWindowConfig{}
                    .title(options.title)
                    .pageId("eui.overlay.detached")
                    .clearColor(options.clearColor)
                    .windowSize(options.width, options.height)
                    .onKeyEvent([overlay](const eui::KeyEvent& key) {
                        overlay->handleHotkey(key);
                    })
                    .onClosed([overlay, detachedGeneration, generation] {
                        // A window from an earlier detach must not touch the overlay.
                        if (*detachedGeneration == generation) {
                            overlay->detachedWindowClosed();
                        }
                    }),
                [overlay](eui::Ui& ui, const eui::Screen& screen) {
                    overlay->composeDetached(ui, screen);
                });
        });
        overlay->setDetachedWindowCloser([detachedHandle] {
            detachedHandle->requestClose();
        });
    } else {
        detail::dslRuntime().setKeyEventHandler(config.keyEventHandler);
    }
#else
    detail::dslRuntime().setKeyEventHandler(config.keyEventHandler);
#endif

    detail::DslAppState& state = detail::dslAppState();
    if (!state.iconApplied) {
        detail::applyWindowIcon(window);
        state.iconApplied = true;
    }
    return detail::dslRuntime().initialize(window);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale) {
    const bool asyncReady = core::async::dispatchReady();
    const bool updateRequested = core::platform::consumeUiUpdate();
    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested || asyncReady);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested) {
    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested, true);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested, bool inputEnabled) {
    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return false;
    }

    const DslAppConfig& config = dslAppConfig();
    const float effectiveScale = dpiScale * uiScale();
    int contentX = 0;
    int contentWidth = windowWidth;
    int contentHeight = windowHeight;
#if defined(EUI_DEBUG_BUILD)
    detail::OverlayHost* overlay = detail::overlayHost();
    if (overlay != nullptr) {
        const core::Rect content = overlay->contentBounds();
        contentX = static_cast<int>(content.x);
        contentWidth = static_cast<int>(content.width);
        contentHeight = static_cast<int>(content.height);
    }
#endif
    float logicalWidth = static_cast<float>(contentWidth) / effectiveScale;
    float logicalHeight = static_cast<float>(contentHeight) / effectiveScale;
    detail::DslAppState& state = detail::dslAppState();

    const auto composeFrame = [&] {
        const auto composeContent = [&](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
            compose(ui, screen);
            if (showDebugOverlay()) {
                config.debugOverlayCompose(ui, screen);
            }
        };
        if (contentX != 0 || contentWidth != windowWidth || contentHeight != windowHeight) {
            // An overlay reserved part of the window: the page is composed into
            // the remaining area and clipped to it.
            detail::dslRuntime().compose(config.pageIdValue,
                                         core::Rect{static_cast<float>(contentX) / effectiveScale, 0.0f,
                                                    logicalWidth, logicalHeight},
                                         composeContent);
        } else {
            detail::dslRuntime().compose(config.pageIdValue, logicalWidth, logicalHeight, composeContent);
        }
        state.composed = true;
        state.logicalWidth = logicalWidth;
        state.logicalHeight = logicalHeight;
    };

    if (!state.composed || state.logicalWidth != logicalWidth || state.logicalHeight != logicalHeight) {
        composeFrame();
    }

    bool changed = false;
    if (updateRequested) {
        composeFrame();
        changed = true;
    }

    changed = detail::dslRuntime().update(window, deltaSeconds, pointerScale, effectiveScale, inputEnabled) || changed;
    if (detail::dslRuntime().composeRequested()) {
        // A compose can change retained content without changing the element structure.
        // Rebuild the complete cache so state and release visuals update in this frame.
        detail::dslRuntime().requestFullPaint();
        composeFrame();
        changed = detail::dslRuntime().update(window, 0.0f, pointerScale, effectiveScale, inputEnabled) || changed;
        changed = true;
    }

#if defined(EUI_DEBUG_BUILD)
    if (overlay != nullptr) {
        // Publish before the overlay update so it composes with the tree of this
        // frame; the overlay asks for the tree only while it displays it.
        publishElementTree(*overlay);
        // The overlay edits the page through the app layer: it asks for the values
        // of the element it shows, and the edits it made land on the page here.
        applyElementPropertyEdits(*overlay);
        publishElementProperties(*overlay);
        // The overlay decides which element the page should preview, and the page
        // draws it with the same transform and clip as the element itself.
        detail::dslRuntime().setHoveredElement(overlay->hoveredElement());
        // The overlay draws on top of the rendered app frame, so an overlay
        // repaint never forces the app render cache to be rebuilt.
        if (overlay->update(windowWidth, windowHeight, effectiveScale, deltaSeconds)) {
            // The overlay draws inside the app render cache, so its repaint has
            // to rebuild the cached frame it belongs to.
            detail::dslRuntime().requestFullPaint();
            changed = true;
        }
        const core::Rect updatedContent = overlay->contentBounds();
        if (contentX != static_cast<int>(updatedContent.x) ||
            contentWidth != static_cast<int>(updatedContent.width) ||
            contentHeight != static_cast<int>(updatedContent.height)) {
            contentX = static_cast<int>(updatedContent.x);
            contentWidth = static_cast<int>(updatedContent.width);
            contentHeight = static_cast<int>(updatedContent.height);
            logicalWidth = static_cast<float>(contentWidth) / effectiveScale;
            logicalHeight = static_cast<float>(contentHeight) / effectiveScale;
            detail::dslRuntime().requestFullPaint();
            composeFrame();
            changed = detail::dslRuntime().update(window, 0.0f, pointerScale, effectiveScale, inputEnabled) || changed;
            changed = true;
        }
    }
#endif

    return changed;
}

bool isAnimating() {
    return detail::dslRuntime().isAnimating();
}

void render(int windowWidth, int windowHeight, float dpiScale) {
    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return;
    }

    const DslAppConfig& config = dslAppConfig();
    const float effectiveScale = dpiScale * uiScale();
    detail::dslRuntime().render(windowWidth, windowHeight, effectiveScale, config.clearColorValue);
}

void releaseGraphicsResources() {
#if defined(EUI_DEBUG_BUILD)
    if (detail::OverlayHost* overlay = detail::overlayHost(); overlay != nullptr) {
        overlay->releaseGraphicsResources();
    }
#endif
    detail::dslRuntime().releaseGraphicsResources();
}

void shutdown() {
    core::async::shutdown();
    if (dslAppConfig().shutdownHandler) dslAppConfig().shutdownHandler();
#if defined(EUI_DEBUG_BUILD)
    if (detail::OverlayHost* overlay = detail::overlayHost(); overlay != nullptr) {
        overlay->shutdown();
    }
#endif
    detail::dslRuntime().shutdown();
    eui::network::shutdown();
}

} // namespace app
