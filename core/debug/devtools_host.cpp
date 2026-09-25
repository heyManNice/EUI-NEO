#include "core/debug/devtools_host.h"
#include "core/debug/devtools_icons.h"

#include "core/input/input_state.h"

#include <algorithm>
#include <cmath>

namespace core::debug {

namespace {

constexpr double kOutsidePointer = -1000000.0;

void composeToolbarIcon(core::dsl::Ui& ui, const std::string& id, float x, float y,
                        const char* svg) {
    ui.rect(id + ".background")
        .position(x, y)
        .size(28.0f, 28.0f)
        .states({0.0f, 0.0f, 0.0f, 0.0f},
                {0.25f, 0.31f, 0.39f, 1.0f},
                {0.25f, 0.31f, 0.39f, 1.0f})
        .instantStates()
        .radius(5.0f)
        .build();
    ui.svg(id + ".icon")
        .position(x + 5.0f, y + 5.0f)
        .size(18.0f, 18.0f)
        .source(svg)
        .tint("#C8D5E4")
        .contain()
        .build();
}

} // namespace

DevtoolsHost& devtoolsHost() {
    static DevtoolsHost host;
    return host;
}

bool DevtoolsHost::beginFrame(core::window::Handle window,
                              int framebufferWidth,
                              int framebufferHeight,
                              float dpiScale,
                              bool inputEnabled) {
    if (framebufferWidth_ != framebufferWidth || framebufferHeight_ != framebufferHeight ||
        dpiScale_ != dpiScale) {
        composeRequested_ = true;
    }
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;
    dpiScale_ = dpiScale;

    if (!inputEnabled) {
        return false;
    }

    bool toggled = false;
    std::vector<KeyEvent>& keys = core::detail::inputQueue(window).keys;
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const KeyEvent& key) {
        const bool f12 = key.key == InputKey::F12;
        const bool shortcut = key.key == InputKey::I && key.modifiers.control && key.modifiers.shift;
        if (!f12 && !shortcut) {
            return false;
        }
        if (key.action == KeyAction::Press) {
            visible_ = !visible_;
            composeRequested_ = true;
            toggled = true;
        }
        return true;
    }), keys.end());
    return toggled;
}

int DevtoolsHost::panelHeight() const {
    if (!visible_ || framebufferHeight_ <= 0) {
        return 0;
    }
    const int minimumContent = std::max(80, static_cast<int>(std::lround(120.0f * dpiScale_)));
    const int minimumPanel = static_cast<int>(std::lround(180.0f * dpiScale_));
    const int maximumPanel = static_cast<int>(std::lround(320.0f * dpiScale_));
    const int preferred = static_cast<int>(std::lround(framebufferHeight_ * 0.42f));
    return std::min(std::max(0, framebufferHeight_ - minimumContent),
                    std::max(minimumPanel, std::min(maximumPanel, preferred)));
}

int DevtoolsHost::contentHeight() const {
    return framebufferHeight_ - panelHeight();
}

void DevtoolsHost::filterInput(std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent) {
    if (!visible_ || panelHeight() <= 0) {
        return;
    }
    const int panelTop = contentHeight();
    bool pointerInPanel = false;
    for (PointerEvent& event : pointerEvents) {
        const bool inside = event.y >= panelTop && event.y < framebufferHeight_ &&
                            event.x >= 0.0 && event.x < framebufferWidth_;
        core::queuePointerMotion(nullptr,
                                 inside ? event.x : kOutsidePointer,
                                 inside ? event.y : kOutsidePointer,
                                 {}, event.modifiers);
        if (!inside) {
            pointerInPanel = false;
            continue;
        }
        pointerInPanel = true;
        event.x = kOutsidePointer;
        event.y = kOutsidePointer;
        event.deltaX = 0.0;
        event.deltaY = 0.0;
    }
    if (pointerInPanel) {
        scrollEvent = {};
    }
}

bool DevtoolsHost::update() {
    if (!visible_ || panelHeight() <= 0 || dpiScale_ <= 0.0f) {
        return false;
    }

    if (composeRequested_) {
        const float width = static_cast<float>(framebufferWidth_) / dpiScale_;
        const float height = static_cast<float>(framebufferHeight_) / dpiScale_;
        const float panelTop = static_cast<float>(contentHeight()) / dpiScale_;
        const float panelSize = height - panelTop;
        runtime_.compose("eui.devtools", width, height, [&](core::dsl::Ui& ui, const core::dsl::Screen&) {
            ui.stack("root")
                .size(width, height)
                .content([&] {
                    ui.rect("panel")
                        .position(0.0f, panelTop)
                        .size(width, panelSize)
                        .color("#20252D")
                        .build();
                    ui.rect("border")
                        .position(0.0f, panelTop)
                        .size(width, 1.0f)
                        .color("#596574")
                        .build();
                    ui.rect("toolbar")
                        .position(0.0f, panelTop + 1.0f)
                        .size(width, 39.0f)
                        .color("#292F38")
                        .build();
                    composeToolbarIcon(ui, "selectElement", 8.0f, panelTop + 6.0f,
                                       icons::kSelectElementSvg);
                    composeToolbarIcon(ui, "deviceViewport", 42.0f, panelTop + 6.0f,
                                       icons::kDeviceViewportSvg);
                    ui.text("elements.tab")
                        .position(90.0f, panelTop + 9.0f)
                        .size(92.0f, 25.0f)
                        .text("Elements")
                        .fontSize(14.0f)
                        .color("#DCE7F5")
                        .build();
                    ui.rect("elements.indicator")
                        .position(86.0f, panelTop + 37.0f)
                        .size(88.0f, 2.0f)
                        .color("#66A9F7")
                        .build();
                    if (width >= 310.0f) {
                        composeToolbarIcon(ui, "settings", width - 104.0f, panelTop + 6.0f,
                                           icons::kSettingsSvg);
                    }
                    if (width >= 276.0f) {
                        composeToolbarIcon(ui, "more", width - 70.0f, panelTop + 6.0f,
                                           icons::kMoreSvg);
                    }
                    if (width >= 242.0f) {
                        composeToolbarIcon(ui, "close", width - 36.0f, panelTop + 6.0f,
                                           icons::kCloseSvg);
                    }
                    ui.text("empty.title")
                        .position(24.0f, panelTop + 68.0f)
                        .size(std::max(0.0f, width - 48.0f), 28.0f)
                        .text("EUI DevTools")
                        .fontSize(19.0f)
                        .color("#ECF3FA")
                        .build();
                    ui.text("empty.description")
                        .position(24.0f, panelTop + 106.0f)
                        .size(std::max(0.0f, width - 48.0f), 24.0f)
                        .text("Element inspection is the next milestone.")
                        .fontSize(13.0f)
                        .color("#9CA9B8")
                        .build();
                })
                .build();
        });
        composeRequested_ = false;
    }
    return runtime_.update(nullptr, 0.0f, 1.0f, dpiScale_);
}

void DevtoolsHost::render(int width, int height, float dpiScale, const Rect* dirtyRect) {
    if (visible_) {
        runtime_.renderDirectOverlay(width, height, dpiScale, dirtyRect);
    }
}

void DevtoolsHost::releaseGraphicsResources() {
    runtime_.releaseGraphicsResources(false);
    composeRequested_ = true;
}

void DevtoolsHost::shutdown() {
    runtime_.shutdown(false);
    visible_ = false;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace core::debug
