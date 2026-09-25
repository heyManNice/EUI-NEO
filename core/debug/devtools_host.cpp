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
                       const std::string& label, DockPosition position, DockPosition selectedPosition,
                       const std::function<void(DockPosition)>& onSelect) {
    const bool selected = position == selectedPosition;
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
                .onClick([onSelect, position] { onSelect(position); })
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

void composeMoreMenu(core::dsl::Ui& ui, float x, float y, DockPosition selectedPosition,
                     const std::function<void(DockPosition)>& onSelect) {
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
                                      "Separate Window", DockPosition::Floating, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.left", icons::kDockLeftSvg,
                                      "Dock to Left", DockPosition::Left, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.bottom", icons::kDockBottomSvg,
                                      "Dock to Bottom", DockPosition::Bottom, selectedPosition, onSelect);
                    composeDockOption(ui, "more.menu.dock.right", icons::kDockRightSvg,
                                      "Dock to Right", DockPosition::Right, selectedPosition, onSelect);
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

void DevtoolsHost::setDetachedWindowOpener(std::function<void()> opener) {
    detachedWindowOpener_ = std::move(opener);
}

void DevtoolsHost::setDetachedWindowCloser(std::function<void()> closer) {
    detachedWindowCloser_ = std::move(closer);
}

void DevtoolsHost::close() {
    visible_ = false;
    moreMenuOpen_ = false;
    composeRequested_ = true;
    resizing_ = false;
    resizeCursorActive_ = false;
    if (dockPosition_ == DockPosition::Floating && detachedWindowCloser_) {
        detachedWindowCloser_();
    }
    resetCursor();
    core::platform::requestUiUpdate();
}

void DevtoolsHost::selectDockPosition(DockPosition position) {
    moreMenuOpen_ = false;
    if (position == dockPosition_) {
        composeRequested_ = true;
        return;
    }
    if (dockPosition_ == DockPosition::Floating && detachedWindowCloser_) {
        detachedWindowCloser_();
    }
    dockPosition_ = position;
    resizing_ = false;
    resizeCursorActive_ = false;
    resetCursor();
    composeRequested_ = true;
    if (position == DockPosition::Floating) {
        if (detachedWindowOpener_) {
            detachedWindowOpener_();
        }
    }
    core::platform::requestUiUpdate();
}

void DevtoolsHost::detachedWindowClosed() {
    if (dockPosition_ == DockPosition::Floating) {
        close();
    }
}

void DevtoolsHost::handleDetachedKey(const KeyEvent& key) {
    if (key.action == KeyAction::Press &&
        (key.key == InputKey::F12 ||
         (key.key == InputKey::I && key.modifiers.control && key.modifiers.shift))) {
        close();
    }
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
            if (visible_) {
                close();
            } else {
                visible_ = true;
                composeRequested_ = true;
                if (dockPosition_ == DockPosition::Floating && detachedWindowOpener_) {
                    detachedWindowOpener_();
                }
            }
            toggled = true;
        }
        return true;
    }), keys.end());
    return toggled;
}

int DevtoolsHost::panelSize() const {
    if (!visible_ || dockPosition_ == DockPosition::Floating ||
        framebufferWidth_ <= 0 || framebufferHeight_ <= 0) {
        return 0;
    }
    const bool horizontal = dockPosition_ != DockPosition::Bottom;
    const int available = horizontal ? framebufferWidth_ : framebufferHeight_;
    const int maximum = maximumPanelSize();
    const int minimum = minimumPanelSize();
    const int preferred = std::clamp(static_cast<int>(std::lround(available * (horizontal ? 0.35f : 0.42f))),
                                     minimum, std::min(maximum, static_cast<int>(std::lround(
                                         (horizontal ? 460.0f : 320.0f) * dpiScale_))));
    const float savedLogical = horizontal ? panelWidthLogical_ : panelHeightLogical_;
    const int requested = savedLogical > 0.0f
        ? static_cast<int>(std::lround(savedLogical * dpiScale_)) : preferred;
    return std::clamp(requested, minimum, maximum);
}

int DevtoolsHost::minimumPanelSize() const {
    const float logical = dockPosition_ == DockPosition::Bottom ? 180.0f : 300.0f;
    return std::min(maximumPanelSize(), static_cast<int>(std::lround(logical * dpiScale_)));
}

int DevtoolsHost::maximumPanelSize() const {
    const int minimumContent = std::max(80, static_cast<int>(std::lround(120.0f * dpiScale_)));
    const int available = dockPosition_ == DockPosition::Bottom ? framebufferHeight_ : framebufferWidth_;
    return std::max(0, available - minimumContent);
}

Rect DevtoolsHost::panelBounds() const {
    const float size = static_cast<float>(panelSize());
    if (dockPosition_ == DockPosition::Left) {
        return {0.0f, 0.0f, size, static_cast<float>(framebufferHeight_)};
    }
    if (dockPosition_ == DockPosition::Right) {
        return {static_cast<float>(framebufferWidth_) - size, 0.0f, size,
                static_cast<float>(framebufferHeight_)};
    }
    if (dockPosition_ == DockPosition::Bottom) {
        return {0.0f, static_cast<float>(framebufferHeight_) - size,
                static_cast<float>(framebufferWidth_), size};
    }
    return {};
}

Rect DevtoolsHost::contentBounds() const {
    const float width = static_cast<float>(framebufferWidth_);
    const float height = static_cast<float>(framebufferHeight_);
    if (!visible_ || dockPosition_ == DockPosition::Floating) {
        return {0.0f, 0.0f, width, height};
    }
    const Rect panel = panelBounds();
    if (dockPosition_ == DockPosition::Left) {
        return {panel.width, 0.0f, width - panel.width, height};
    }
    if (dockPosition_ == DockPosition::Right) {
        return {0.0f, 0.0f, width - panel.width, height};
    }
    return {0.0f, 0.0f, width, height - panel.height};
}

int DevtoolsHost::contentHeight() const {
    return static_cast<int>(contentBounds().height);
}

bool DevtoolsHost::overResizeBoundary(double x, double y) const {
    const Rect panel = panelBounds();
    if (dockPosition_ == DockPosition::Bottom) {
        return x >= 0.0 && x < framebufferWidth_ &&
               std::abs(y - panel.y) <= kResizeBoundaryHalfWidth * dpiScale_;
    }
    const float boundary = dockPosition_ == DockPosition::Left ? panel.width : panel.x;
    return y >= 0.0 && y < framebufferHeight_ &&
           std::abs(x - boundary) <= kResizeBoundaryHalfWidth * dpiScale_;
}

void DevtoolsHost::filterInput(std::vector<PointerEvent>& pointerEvents, ScrollEvent& scrollEvent) {
    if (!visible_ || panelSize() <= 0) {
        return;
    }
    bool pointerInPanel = false;
    for (PointerEvent& event : pointerEvents) {
        const bool overBoundary = overResizeBoundary(event.x, event.y);
        const bool captured = resizing_;
        if (event.isPress(PointerButton::Left) && overBoundary) {
            resizing_ = true;
            dragStartX_ = event.x;
            dragStartY_ = event.y;
            dragStartSize_ = panelSize();
        }
        if (resizing_ && (event.action == PointerAction::Move || event.isRelease(PointerButton::Left))) {
            const double delta = dockPosition_ == DockPosition::Bottom ? dragStartY_ - event.y
                : dockPosition_ == DockPosition::Left ? event.x - dragStartX_ : dragStartX_ - event.x;
            const int size = std::clamp(static_cast<int>(std::lround(dragStartSize_ + delta)),
                                        minimumPanelSize(), maximumPanelSize());
            if (size != panelSize()) {
                (dockPosition_ == DockPosition::Bottom ? panelHeightLogical_ : panelWidthLogical_) =
                    static_cast<float>(size) / dpiScale_;
                composeRequested_ = true;
            }
        }
        if (event.isRelease(PointerButton::Left) || event.action == PointerAction::Cancel ||
            (resizing_ && event.action == PointerAction::Move && !event.isDown(PointerButton::Left))) {
            resizing_ = false;
        }
        resizeCursorActive_ = resizing_ || overResizeBoundary(event.x, event.y);
        const Rect panel = panelBounds();
        const bool inside = event.x >= panel.x && event.x < panel.x + panel.width &&
                            event.y >= panel.y && event.y < panel.y + panel.height;
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

void DevtoolsHost::composeUi(core::dsl::Ui& ui, float width, float height,
                             const Rect& panel, bool detached) {
    ui.stack("root")
        .size(width, height)
        .content([&] {
            ui.column("panel")
                .position(panel.x, panel.y)
                .size(panel.width, panel.height)
                .content([&] {
                    ui.rect("panel.background")
                        .fill()
                        .ignoreLayout()
                        .color("#20252D")
                        .build();
                    if (!detached) {
                        ui.rect("panel.border")
                            .width(core::SizeValue::fill())
                            .height(1.0f)
                            .color("#596574")
                            .build();
                    }
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
                                                close();
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
                composeMoreMenu(ui,
                                std::max(panel.x + 8.0f, panel.x + panel.width - kMoreMenuWidth - 36.0f),
                                panel.y + kToolbarHeight + 6.0f, dockPosition_,
                                [this](DockPosition position) { selectDockPosition(position); });
            }
        })
        .build();
}

void DevtoolsHost::composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    composeUi(ui, screen.width, screen.height, {0.0f, 0.0f, screen.width, screen.height}, true);
}

bool DevtoolsHost::update() {
    if (!visible_ || dockPosition_ == DockPosition::Floating || panelSize() <= 0 || dpiScale_ <= 0.0f) {
        return false;
    }

    const auto composePanel = [&] {
        const float width = static_cast<float>(framebufferWidth_) / dpiScale_;
        const float height = static_cast<float>(framebufferHeight_) / dpiScale_;
        const Rect pixelPanel = panelBounds();
        const Rect logicalPanel{pixelPanel.x / dpiScale_, pixelPanel.y / dpiScale_,
                                pixelPanel.width / dpiScale_, pixelPanel.height / dpiScale_};
        runtime_.compose("eui.devtools", width, height, [&](core::dsl::Ui& ui, const core::dsl::Screen&) {
            composeUi(ui, width, height, logicalPanel, false);
        });
        composeRequested_ = false;
    };
    if (composeRequested_) {
        composePanel();
    }
    const bool wasVisible = visible_;
    const DockPosition previousDock = dockPosition_;
    const bool repainted = runtime_.update(nullptr, 0.0f, 1.0f, dpiScale_);
    const bool panelChanged = visible_ && dockPosition_ != DockPosition::Floating && composeRequested_;
    if (panelChanged) {
        composePanel();
        runtime_.update(nullptr, 0.0f, 1.0f, dpiScale_);
    }
    return repainted || wasVisible != visible_ || previousDock != dockPosition_ || panelChanged;
}

void DevtoolsHost::updateCursor(core::window::Handle window) {
    if (visible_ && dockPosition_ != DockPosition::Floating && resizeCursorActive_) {
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
    if (visible_ && dockPosition_ != DockPosition::Floating) {
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
    dockPosition_ = DockPosition::Bottom;
    detachedWindowOpener_ = {};
    detachedWindowCloser_ = {};
    moreMenuOpen_ = false;
    resizing_ = false;
    resizeCursorActive_ = false;
    panelHeightLogical_ = 0.0f;
    panelWidthLogical_ = 0.0f;
    resizeCursorApplied_ = false;
    cursorWindow_ = nullptr;
    composeRequested_ = true;
    framebufferWidth_ = 0;
    framebufferHeight_ = 0;
    dpiScale_ = 1.0f;
}

} // namespace core::debug
