#include "core/debug/devtools_host.h"
#include "core/debug/devtools_icons.h"

#include "core/input/input_state.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace core::debug {

namespace {

constexpr double kOutsidePointer = -1000000.0;
constexpr float kResizeBoundaryHalfWidth = 4.0f;
constexpr float kToolbarHeight = 31.0f;
constexpr float kTabFontSize = 14.0f;
constexpr float kTabHorizontalPadding = 14.0f;
constexpr float kMoreMenuWidth = 136.0f;
constexpr float kMoreMenuRowHeight = 22.67f;
constexpr float kMoreMenuPadding = 2.0f;

void composeToolbarIcon(core::dsl::Ui& ui, const std::string& id, const char* svg,
                        const std::function<void()>& onClick = {}) {
    ui.stack(id)
        .size(24.0f, 24.0f)
        .align(core::Align::CENTER, core::Align::CENTER)
        .content([&] {
            auto background = ui.rect(id + ".background");
            background.fill()
                .ignoreLayout()
                .states({0.0f, 0.0f, 0.0f, 0.0f},
                        {0.25f, 0.31f, 0.39f, 1.0f},
                        {0.25f, 0.31f, 0.39f, 1.0f})
                .instantStates()
                .radius(5.0f);
            if (onClick) {
                background.onClick(onClick);
            }
            background.build();
            ui.svg(id + ".icon")
                .size(16.0f, 16.0f)
                .source(svg)
                .tint("#C8D5E4")
                .contain()
                .build();
        })
        .build();
}

void queueDevtoolsPointer(const PointerEvent& event, bool inside) {
    const double x = inside ? event.x : kOutsidePointer;
    const double y = inside ? event.y : kOutsidePointer;
    if (event.action == PointerAction::Press || event.action == PointerAction::Release) {
        core::queuePointerButton(nullptr, x, y, event.button, event.action, event.modifiers);
    } else if (event.action == PointerAction::Cancel) {
        core::cancelPointerInput(nullptr);
    } else {
        core::queuePointerMotion(nullptr, x, y, event.buttons, event.modifiers);
    }
}

void composeToolbarTab(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                       bool selected) {
    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(kToolbarHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states({0.0f, 0.0f, 0.0f, 0.0f},
                        {0.23f, 0.28f, 0.34f, 1.0f},
                        {0.23f, 0.28f, 0.34f, 1.0f})
                .instantStates()
                .build();
            ui.row(id + ".content")
                .width(core::SizeValue::wrapContent())
                .height(kToolbarHeight)
                .padding(kTabHorizontalPadding, 0.0f)
                .content([&] {
                    ui.text(id + ".label")
                        .width(core::SizeValue::wrapContent())
                        .height(kToolbarHeight)
                        .text(label)
                        .fontSize(kTabFontSize)
                        .color(selected ? "#DCE7F5" : "#9CA9B8")
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
            if (selected) {
                ui.rect(id + ".indicator")
                    .width(core::SizeValue::fill())
                    .height(2.0f)
                    .margin(4.0f, 0.0f, 4.0f, 0.0f)
                    .y(kToolbarHeight - 2.0f)
                    .ignoreLayout()
                    .color("#66A9F7")
                    .build();
            }
        })
        .build();
}

void composeDockOption(core::dsl::Ui& ui, const std::string& id, const char* svg,
                       const std::string& label, bool selected) {
    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(kMoreMenuRowHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states(selected ? core::Color{0.20f, 0.34f, 0.52f, 1.0f}
                                 : core::Color{0.0f, 0.0f, 0.0f, 0.0f},
                        {0.27f, 0.35f, 0.45f, 1.0f},
                        {0.27f, 0.35f, 0.45f, 1.0f})
                .radius(5.0f)
                .instantStates()
                .interactive()
                .build();
            ui.row(id + ".content")
                .fill()
                .padding(5.0f, 0.0f)
                .gap(3.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.svg(id + ".icon")
                        .size(17.0f, 17.0f)
                        .source(svg)
                        .tint(selected ? "#91C1FF" : "#C8D5E4")
                        .contain()
                        .build();
                    ui.text(id + ".label")
                        .width(core::SizeValue::fill())
                        .height(kMoreMenuRowHeight)
                        .text(label)
                        .fontSize(13.0f)
                        .color(selected ? "#DCEBFF" : "#DCE7F5")
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void composeMoreMenu(core::dsl::Ui& ui, float x, float y) {
    ui.stack("more.menu")
        .position(x, y)
        .width(kMoreMenuWidth)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect("more.menu.background")
                .fill()
                .ignoreLayout()
                .color("#303741")
                .radius(7.0f)
                .shadow(14.0f, 0.0f, 5.0f, core::Color{0.0f, 0.0f, 0.0f, 0.30f})
                .build();
            ui.column("more.menu.rows")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(kMoreMenuPadding)
                .content([&] {
                    composeDockOption(ui, "more.menu.dock.floating", icons::kDockFloatingSvg,
                                      "Separate Window", false);
                    composeDockOption(ui, "more.menu.dock.left", icons::kDockLeftSvg,
                                      "Dock to Left", false);
                    composeDockOption(ui, "more.menu.dock.bottom", icons::kDockBottomSvg,
                                      "Dock to Bottom", true);
                    composeDockOption(ui, "more.menu.dock.right", icons::kDockRightSvg,
                                      "Dock to Right", false);
                })
                .build();
        })
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
    cursorWindow_ = window;

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
            if (!visible_) {
                moreMenuOpen_ = false;
                resizing_ = false;
                resizeCursorActive_ = false;
                resetCursor();
            }
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
    const int minimumPanel = minimumPanelHeight();
    const int maximumPanel = maximumPanelHeight();
    const int preferred = std::clamp(static_cast<int>(std::lround(framebufferHeight_ * 0.42f)),
                                     minimumPanel,
                                     std::min(maximumPanel, static_cast<int>(std::lround(320.0f * dpiScale_))));
    const int requested = panelHeightLogical_ > 0.0f
        ? static_cast<int>(std::lround(panelHeightLogical_ * dpiScale_)) : preferred;
    return std::clamp(requested, minimumPanel, maximumPanel);
}

int DevtoolsHost::minimumPanelHeight() const {
    return std::min(maximumPanelHeight(), static_cast<int>(std::lround(180.0f * dpiScale_)));
}

int DevtoolsHost::maximumPanelHeight() const {
    const int minimumContent = std::max(80, static_cast<int>(std::lround(120.0f * dpiScale_)));
    return std::max(0, framebufferHeight_ - minimumContent);
}

int DevtoolsHost::contentHeight() const {
    return framebufferHeight_ - panelHeight();
}

bool DevtoolsHost::overResizeBoundary(double x, double y) const {
    return x >= 0.0 && x < framebufferWidth_ &&
           std::abs(y - contentHeight()) <= kResizeBoundaryHalfWidth * dpiScale_;
}

void DevtoolsHost::filterInput(std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent) {
    if (!visible_ || panelHeight() <= 0) {
        return;
    }
    bool pointerInPanel = false;
    for (PointerEvent& event : pointerEvents) {
        const bool overBoundary = overResizeBoundary(event.x, event.y);
        const bool captured = resizing_;
        if (event.isPress(PointerButton::Left) && overBoundary) {
            resizing_ = true;
            dragStartY_ = event.y;
            dragStartHeight_ = panelHeight();
        }
        if (resizing_ && (event.action == PointerAction::Move || event.isRelease(PointerButton::Left))) {
            const int height = std::clamp(static_cast<int>(std::lround(dragStartHeight_ - (event.y - dragStartY_))),
                                          minimumPanelHeight(), maximumPanelHeight());
            if (height != panelHeight()) {
                panelHeightLogical_ = static_cast<float>(height) / dpiScale_;
                composeRequested_ = true;
            }
        }
        if (event.isRelease(PointerButton::Left) || event.action == PointerAction::Cancel ||
            (resizing_ && event.action == PointerAction::Move && !event.isDown(PointerButton::Left))) {
            resizing_ = false;
        }
        resizeCursorActive_ = resizing_ || overResizeBoundary(event.x, event.y);
        const int panelTop = contentHeight();
        const bool inside = event.y >= panelTop && event.y < framebufferHeight_ &&
                            event.x >= 0.0 && event.x < framebufferWidth_;
        queueDevtoolsPointer(event, inside && !resizeCursorActive_);
        if (!inside && !overBoundary && !captured) {
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
    if (!resizeCursorActive_) {
        resetCursor();
    }
}

bool DevtoolsHost::update() {
    if (!visible_ || panelHeight() <= 0 || dpiScale_ <= 0.0f) {
        return false;
    }

    const auto composePanel = [&] {
        const float width = static_cast<float>(framebufferWidth_) / dpiScale_;
        const float height = static_cast<float>(framebufferHeight_) / dpiScale_;
        const float panelTop = static_cast<float>(contentHeight()) / dpiScale_;
        const float panelSize = height - panelTop;
        runtime_.compose("eui.devtools", width, height, [&](core::dsl::Ui& ui, const core::dsl::Screen&) {
            ui.stack("root")
                .size(width, height)
                .content([&] {
                    ui.column("panel")
                        .position(0.0f, panelTop)
                        .size(width, panelSize)
                        .content([&] {
                            ui.rect("panel.background")
                                .fill()
                                .ignoreLayout()
                                .color("#20252D")
                                .build();
                            ui.rect("panel.border")
                                .width(core::SizeValue::fill())
                                .height(1.0f)
                                .color("#596574")
                                .build();
                            ui.stack("toolbar")
                                .width(core::SizeValue::fill())
                                .height(kToolbarHeight)
                                .content([&] {
                                    ui.rect("toolbar.background")
                                        .fill()
                                        .ignoreLayout()
                                        .color("#292F38")
                                        .build();
                                    ui.row("toolbar.items")
                                        .fill()
                                        .padding(8.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            ui.row("toolbar.leading")
                                                .width(core::SizeValue::wrapContent())
                                                .height(24.0f)
                                                .gap(4.0f)
                                                .content([&] {
                                                    composeToolbarIcon(ui, "selectElement", icons::kSelectElementSvg);
                                                    composeToolbarIcon(ui, "deviceViewport", icons::kDeviceViewportSvg);
                                                })
                                                .build();
                                            ui.row("toolbar.tabs")
                                                .width(core::SizeValue::wrapContent())
                                                .height(kToolbarHeight)
                                                .margin(8.0f, 0.0f, 0.0f, 0.0f)
                                                .content([&] {
                                                    composeToolbarTab(ui, "elements.tab", "Elements", true);
                                                })
                                                .build();
                                            ui.stack("toolbar.spacer")
                                                .width(core::SizeValue::fill())
                                                .height(1.0f)
                                                .build();
                                            ui.row("toolbar.trailing")
                                                .width(core::SizeValue::wrapContent())
                                                .height(24.0f)
                                                .gap(4.0f)
                                                .content([&] {
                                                    composeToolbarIcon(ui, "settings", icons::kSettingsSvg);
                                                    composeToolbarIcon(ui, "more", icons::kMoreSvg, [this] {
                                                        moreMenuOpen_ = !moreMenuOpen_;
                                                        composeRequested_ = true;
                                                    });
                                                    composeToolbarIcon(ui, "close", icons::kCloseSvg, [this] {
                                                        visible_ = false;
                                                        moreMenuOpen_ = false;
                                                        composeRequested_ = true;
                                                        resizing_ = false;
                                                        resizeCursorActive_ = false;
                                                        resetCursor();
                                                    });
                                                })
                                                .build();
                                        })
                                        .build();
                                })
                                .build();
                            ui.column("panel.content")
                                .width(core::SizeValue::fill())
                                .height(core::SizeValue::fill())
                                .padding(24.0f, 28.0f, 24.0f, 0.0f)
                                .gap(10.0f)
                                .content([&] {
                                    ui.text("empty.title")
                                        .width(core::SizeValue::fill())
                                        .height(28.0f)
                                        .text("EUI DevTools")
                                        .fontSize(19.0f)
                                        .color("#ECF3FA")
                                        .build();
                                    ui.text("empty.description")
                                        .width(core::SizeValue::fill())
                                        .height(24.0f)
                                        .text("Element inspection is the next milestone.")
                                        .fontSize(13.0f)
                                        .color("#9CA9B8")
                                        .build();
                                })
                                .build();
                        })
                        .build();
                    if (moreMenuOpen_) {
                        composeMoreMenu(ui, std::max(8.0f, width - kMoreMenuWidth - 36.0f),
                                        panelTop + kToolbarHeight + 6.0f);
                    }
                })
                .build();
        });
        composeRequested_ = false;
    };
    if (composeRequested_) {
        composePanel();
    }
    const bool wasVisible = visible_;
    const bool repainted = runtime_.update(nullptr, 0.0f, 1.0f, dpiScale_);
    const bool menuChanged = visible_ && composeRequested_;
    if (menuChanged) {
        composePanel();
        runtime_.update(nullptr, 0.0f, 1.0f, dpiScale_);
    }
    return repainted || wasVisible != visible_ || menuChanged;
}

void DevtoolsHost::updateCursor(core::window::Handle window) {
    if (visible_ && resizeCursorActive_) {
        if (!handCursor_) {
            handCursor_ = core::window::createStandardCursor(core::window::CursorType::Hand);
        }
        if (handCursor_) {
            core::window::setCursor(window, handCursor_);
            resizeCursorApplied_ = true;
        }
    }
}

void DevtoolsHost::resetCursor() {
    if (resizeCursorApplied_ && cursorWindow_) {
        core::window::setCursor(cursorWindow_, nullptr);
        resizeCursorApplied_ = false;
    }
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
    resetCursor();
    if (handCursor_) {
        core::window::destroyCursor(handCursor_);
        handCursor_ = nullptr;
    }
    runtime_.shutdown(false);
    visible_ = false;
    moreMenuOpen_ = false;
    resizing_ = false;
    resizeCursorActive_ = false;
    panelHeightLogical_ = 0.0f;
    resizeCursorApplied_ = false;
    cursorWindow_ = nullptr;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace core::debug
