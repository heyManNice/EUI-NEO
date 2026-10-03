#include "modules/devtools/input.h"
#include "modules/devtools/theme.h"
#include "components/button.h"
#include "components/virtuallist.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace modules::devtools {

const char* inputCategoryName(InputCategory cat) {
    switch (cat) {
        case InputCategory::All: return "All";
        case InputCategory::Pointer: return "Pointer";
        case InputCategory::Scroll: return "Scroll";
        case InputCategory::Keys: return "Keys";
        case InputCategory::Focus: return "Focus";
    }
    return "All";
}

static const char* toolingInputKindName(core::dsl::runtime::ToolingInputKind kind) {
    switch (kind) {
        case core::dsl::runtime::ToolingInputKind::PointerPress: return "PRESS";
        case core::dsl::runtime::ToolingInputKind::PointerRelease: return "RELEASE";
        case core::dsl::runtime::ToolingInputKind::PointerClick: return "CLICK";
        case core::dsl::runtime::ToolingInputKind::Scroll: return "SCROLL";
        case core::dsl::runtime::ToolingInputKind::KeyDown: return "KEY DOWN";
        case core::dsl::runtime::ToolingInputKind::KeyUp: return "KEY UP";
        case core::dsl::runtime::ToolingInputKind::TextInput: return "TEXT";
        case core::dsl::runtime::ToolingInputKind::FocusChange: return "FOCUS";
    }
    return "EVENT";
}

static core::Color toolingInputKindColor(core::dsl::runtime::ToolingInputKind kind, const DevtoolsTheme& theme) {
    switch (kind) {
        case core::dsl::runtime::ToolingInputKind::PointerClick:
            return core::Color{0.35f, 0.85f, 0.55f, 1.0f}; // green
        case core::dsl::runtime::ToolingInputKind::PointerPress:
            return theme.accent; // blue
        case core::dsl::runtime::ToolingInputKind::PointerRelease:
            return core::Color{0.45f, 0.70f, 0.95f, 1.0f}; // light blue
        case core::dsl::runtime::ToolingInputKind::Scroll:
            return core::Color{1.0f, 0.70f, 0.30f, 1.0f}; // amber
        case core::dsl::runtime::ToolingInputKind::KeyDown:
        case core::dsl::runtime::ToolingInputKind::KeyUp:
        case core::dsl::runtime::ToolingInputKind::TextInput:
            return core::Color{0.80f, 0.55f, 0.95f, 1.0f}; // purple
        case core::dsl::runtime::ToolingInputKind::FocusChange:
            return core::Color{0.30f, 0.85f, 0.85f, 1.0f}; // cyan/teal
    }
    return theme.metricLabel;
}

static const char* elementKindName(core::dsl::ElementKind kind) {
    switch (kind) {
        case core::dsl::ElementKind::Row: return "Row";
        case core::dsl::ElementKind::Column: return "Column";
        case core::dsl::ElementKind::Stack: return "Stack";
        case core::dsl::ElementKind::Rect: return "Rect";
        case core::dsl::ElementKind::Polygon: return "Polygon";
        case core::dsl::ElementKind::Text: return "Text";
        case core::dsl::ElementKind::Image: return "Image";
        case core::dsl::ElementKind::Svg: return "Svg";
        case core::dsl::ElementKind::Flow: return "Flow";
        case core::dsl::ElementKind::Shadertoy: return "ShaderToy";
    }
    return "Element";
}

static std::string truncateMiddle(const std::string& str, std::size_t maxLen) {
    if (str.size() <= maxLen) return str;
    if (maxLen <= 5) return str.substr(0, maxLen);
    const std::size_t head = (maxLen - 3) / 2;
    const std::size_t tail = maxLen - 3 - head;
    return str.substr(0, head) + "..." + str.substr(str.size() - tail);
}

static std::string formatTimestamp(double sec) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << sec << "s";
    return ss.str();
}

InputSnapshot captureInputSnapshot(const core::dsl::Runtime& page) {
    InputSnapshot snapshot;
#if EUI_TOOLING_ENABLED
    snapshot.focusedId = page.focusedId();
    snapshot.hoverTargetId = page.hoverTargetId();
    snapshot.capturedId = page.capturedInteractionIdSeam();
    snapshot.pointerX = static_cast<float>(page.lastPointerEvent().x);
    snapshot.pointerY = static_cast<float>(page.lastPointerEvent().y);
    snapshot.totalEventsCount = page.inputEventCount();

    // Extract input history in reverse order (newest first)
    const auto& history = page.inputHistory();
    snapshot.events.reserve(history.size());
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        InputEventItem item;
        item.timestamp = it->timestamp;
        item.kind = it->kind;
        item.targetId = it->targetId;
        item.detail = it->detail;
        item.x = it->x;
        item.y = it->y;
        item.button = it->button;
        item.key = it->key;
        item.text = it->text;
        snapshot.events.push_back(std::move(item));
    }

    // Capture hit-test chain at current cursor position
    const float dpi = page.lastPointerDpiScale() > 0.0f ? page.lastPointerDpiScale() : 1.0f;
    const auto rawChain = page.hitTestChain(snapshot.pointerX, snapshot.pointerY, dpi);
    snapshot.hitChain.reserve(rawChain.size());
    bool foundTarget = false;
    for (const auto& raw : rawChain) {
        HitChainItem item;
        item.id = raw.id;
        item.kind = raw.kind;
        item.interactive = raw.interactive;
        item.disabled = raw.disabled;
        item.focusable = raw.focusable;
        item.hitTestMode = raw.hitTestMode;
        item.frame = raw.frame;

        if (!foundTarget && item.interactive && !item.disabled &&
            item.hitTestMode != core::dsl::HitTestMode::None) {
            item.isTopmostTarget = true;
            foundTarget = true;
        } else if (foundTarget && item.interactive) {
            item.isBlocked = true;
        }
        snapshot.hitChain.push_back(std::move(item));
    }
#endif
    return snapshot;
}

static void composeCategoryButton(core::dsl::Ui& ui, const std::string& id,
                                  const std::string& label, bool selected,
                                  const std::function<void()>& onClick) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color bg = selected
        ? theme.accent
        : core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.40f};
    const core::Color textCol = selected ? core::Color{1.0f, 1.0f, 1.0f, 1.0f} : theme.metricLabel;

    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(24.0f)
        .content([&] {
            auto bgRect = ui.rect(id + ".bg");
            bgRect.fill()
                .ignoreLayout()
                .color(bg)
                .radius(4.0f)
                .states(bg, theme.elementRowHover, theme.elementRowHover);
            if (onClick) {
                bgRect.onClick(onClick);
            }
            bgRect.build();
            ui.text(id + ".text")
                .fontFamily(theme.fontFamily)
                .margin(8.0f, 0.0f, 8.0f, 0.0f)
                .width(core::SizeValue::wrapContent())
                .height(24.0f)
                .text(label)
                .fontSize(theme.captionFontSize)
                .fontWeight(selected ? 600 : 500)
                .color(textCol)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .onClick(onClick)
        .build();
}

void composeInputTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    static const InputSnapshot emptySnapshot;
    const DevtoolsTheme& theme = devtoolsTheme();
    const InputSnapshot& snapshot = state.input != nullptr ? *state.input : emptySnapshot;
    const float scrollOffset = state.panelState != nullptr ? state.panelState->inputScrollOffset : 0.0f;
    const float hitChainScrollOffset = state.panelState != nullptr ? state.panelState->inputHitChainScrollOffset : 0.0f;
    const InputCategory currentCategory = state.panelState != nullptr ? state.panelState->inputCategory : InputCategory::All;
    const std::string selectedTarget = state.panelState != nullptr ? state.panelState->selectedInputTargetId : "";
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));

    // Filter events
    std::vector<const InputEventItem*> filteredEvents;
    filteredEvents.reserve(snapshot.events.size());
    for (const auto& ev : snapshot.events) {
        if (currentCategory == InputCategory::Pointer) {
            if (ev.kind == core::dsl::runtime::ToolingInputKind::PointerPress ||
                ev.kind == core::dsl::runtime::ToolingInputKind::PointerRelease ||
                ev.kind == core::dsl::runtime::ToolingInputKind::PointerClick) {
                filteredEvents.push_back(&ev);
            }
        } else if (currentCategory == InputCategory::Scroll) {
            if (ev.kind == core::dsl::runtime::ToolingInputKind::Scroll) {
                filteredEvents.push_back(&ev);
            }
        } else if (currentCategory == InputCategory::Keys) {
            if (ev.kind == core::dsl::runtime::ToolingInputKind::KeyDown ||
                ev.kind == core::dsl::runtime::ToolingInputKind::KeyUp ||
                ev.kind == core::dsl::runtime::ToolingInputKind::TextInput) {
                filteredEvents.push_back(&ev);
            }
        } else if (currentCategory == InputCategory::Focus) {
            if (ev.kind == core::dsl::runtime::ToolingInputKind::FocusChange) {
                filteredEvents.push_back(&ev);
            }
        } else {
            filteredEvents.push_back(&ev);
        }
    }

    const float rowH = 26.0f;
    const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.28f};

    const float footerHeight = !selectedTarget.empty() ? 26.0f : 0.0f;
    const float mainAreaHeight = std::max(0.0f, contentHeight - footerHeight);

    // Two-column split: left column for hit-test chain, right column for event timeline
    const float hitChainColWidth = std::clamp(state.panel.width * 0.36f, 280.0f, 360.0f);
    const float eventStreamColWidth = std::max(200.0f, state.panel.width - hitChainColWidth - 1.0f);

    ui.column("input.main")
        .size(state.panel.width, contentHeight)
        .content([&] {
            // ---- Main Two-Column Workbench ----
            ui.row("input.workbench")
                .size(state.panel.width, mainAreaHeight)
                .content([&] {
                    // ========================================================
                    // Column 1 (Left): Hit-Test Stack at Pointer (Z-Order)
                    // ========================================================
                    ui.column("input.hitchain.col")
                        .size(hitChainColWidth, mainAreaHeight)
                        .content([&] {
                            // Hit-test Header
                            ui.stack("input.hitchain.header")
                                .width(core::SizeValue::fill())
                                .height(32.0f)
                                .content([&] {
                                    ui.rect("input.hitchain.header.bg")
                                        .fill()
                                        .ignoreLayout()
                                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.40f})
                                        .build();
                                    ui.rect("input.hitchain.header.border")
                                        .position(0.0f, 31.0f)
                                        .width(core::SizeValue::fill())
                                        .height(1.0f)
                                        .ignoreLayout()
                                        .color(theme.panelBorder)
                                        .build();
                                    ui.row("input.hitchain.header.row")
                                        .fill()
                                        .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            ui.text("input.hitchain.title")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::wrapContent())
                                                .height(32.0f)
                                                .text("HIT STACK (UNDER CURSOR)")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.primaryText)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.hitchain.coords")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(32.0f)
                                                .text(" @ (" + std::to_string(static_cast<int>(snapshot.pointerX)) + ", " + std::to_string(static_cast<int>(snapshot.pointerY)) + ")")
                                                .fontSize(theme.captionFontSize)
                                                .color(theme.accent)
                                                .horizontalAlign(core::HorizontalAlign::Right)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                        })
                                        .build();
                                })
                                .build();

                            // Hit-test Subheader
                            ui.stack("input.hitchain.subth")
                                .width(core::SizeValue::fill())
                                .height(rowH)
                                .content([&] {
                                    ui.rect("input.hitchain.subth.border")
                                        .position(0.0f, rowH - 1.0f)
                                        .width(core::SizeValue::fill())
                                        .height(1.0f)
                                        .ignoreLayout()
                                        .color(dividerColor)
                                        .build();
                                    ui.row("input.hitchain.subth.cols")
                                        .fill()
                                        .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            ui.text("input.hitchain.th.status")
                                                .fontFamily(theme.fontFamily)
                                                .width(80.0f)
                                                .height(rowH)
                                                .text("STATUS")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.hitchain.th.id")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(rowH)
                                                .text("ELEMENT ID")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.hitchain.th.kind")
                                                .fontFamily(theme.fontFamily)
                                                .width(60.0f)
                                                .height(rowH)
                                                .text("KIND")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .horizontalAlign(core::HorizontalAlign::Right)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                        })
                                        .build();
                                })
                                .build();

                            // Hit-test List
                            const float hitListHeight = std::max(0.0f, mainAreaHeight - 32.0f - rowH);
                            if (snapshot.hitChain.empty()) {
                                ui.stack("input.hitchain.empty")
                                    .size(hitChainColWidth, hitListHeight)
                                    .align(core::Align::CENTER, core::Align::CENTER)
                                    .content([&] {
                                        ui.text("input.hitchain.empty.text")
                                            .fontFamily(theme.fontFamily)
                                            .width(core::SizeValue::wrapContent())
                                            .height(24.0f)
                                            .text("Move cursor over window to inspect layers.")
                                            .fontSize(theme.elementRowFontSize)
                                            .color(theme.mutedText)
                                            .build();
                                    })
                                    .build();
                            } else {
                                components::virtualList(ui, "input.hitchain.list")
                                    .size(hitChainColWidth, hitListHeight)
                                    .itemCount(static_cast<std::int64_t>(snapshot.hitChain.size()))
                                    .rowHeight(rowH)
                                    .scrollbarGap(0.0f)
                                    .offset(hitChainScrollOffset)
                                    .onChange(actions.input.setHitChainScrollOffset)
                                    .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width, float height) {
                                        if (index < 0 || index >= static_cast<std::int64_t>(snapshot.hitChain.size())) return;
                                        const HitChainItem& item = snapshot.hitChain[static_cast<std::size_t>(index)];
                                        const bool isSelected = (!selectedTarget.empty() && selectedTarget == item.id);
                                        const core::Color rowBg = isSelected ? theme.elementRowSelected : core::Color{0.0f, 0.0f, 0.0f, 0.0f};

                                        const auto onSelectRow = [onSelect = actions.input.selectTarget, id = item.id] {
                                            core::window::setClipboardText(id);
                                            if (onSelect) onSelect(id);
                                        };

                                        rowUi.stack(rowId)
                                            .size(width, height)
                                            .content([&] {
                                                auto bgRect = rowUi.rect(rowId + ".bg");
                                                bgRect.fill()
                                                    .ignoreLayout()
                                                    .color(rowBg)
                                                    .states(rowBg, theme.elementRowHover, theme.elementRowHover);
                                                bgRect.onClick(onSelectRow);
                                                bgRect.build();
                                                rowUi.rect(rowId + ".border")
                                                    .position(0.0f, height - 1.0f)
                                                    .width(core::SizeValue::fill())
                                                    .height(1.0f)
                                                    .ignoreLayout()
                                                    .color(dividerColor)
                                                    .build();
                                                rowUi.row(rowId + ".cols")
                                                    .fill()
                                                    .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                                    .alignItems(core::Align::CENTER)
                                                    .content([&] {
                                                        // Status Badge (80px)
                                                        const char* statusText = item.isTopmostTarget ? "TARGET" : (item.isBlocked ? "BLOCKED" : (item.disabled ? "DISABLED" : "PASS"));
                                                        const core::Color statusColor = item.isTopmostTarget
                                                            ? core::Color{0.35f, 0.85f, 0.55f, 1.0f}
                                                            : (item.isBlocked ? core::Color{1.0f, 0.55f, 0.30f, 1.0f} : theme.mutedText);

                                                        rowUi.text(rowId + ".status")
                                                            .fontFamily(theme.fontFamily)
                                                            .width(80.0f)
                                                            .height(height)
                                                            .text(statusText)
                                                            .fontSize(theme.captionFontSize)
                                                            .fontWeight(item.isTopmostTarget ? 700 : 500)
                                                            .color(statusColor)
                                                            .verticalAlign(core::VerticalAlign::Center)
                                                            .build();

                                                        // ID (fill, clipped, truncated)
                                                        rowUi.stack(rowId + ".id.box")
                                                            .width(core::SizeValue::fill())
                                                            .height(height)
                                                            .clip()
                                                            .content([&] {
                                                                rowUi.text(rowId + ".id")
                                                                    .fontFamily(theme.fontFamily)
                                                                    .fill()
                                                                    .text(truncateMiddle(item.id, 24))
                                                                    .fontSize(theme.elementRowFontSize)
                                                                    .fontWeight(item.isTopmostTarget ? 600 : 400)
                                                                    .color(item.isTopmostTarget ? theme.primaryText : (item.isBlocked ? theme.metricLabel : theme.mutedText))
                                                                    .verticalAlign(core::VerticalAlign::Center)
                                                                    .build();
                                                            })
                                                            .build();

                                                        // Kind (60px)
                                                        rowUi.text(rowId + ".kind")
                                                            .fontFamily(theme.fontFamily)
                                                            .width(60.0f)
                                                            .height(height)
                                                            .text(elementKindName(item.kind))
                                                            .fontSize(theme.captionFontSize)
                                                            .color(theme.elementKindText)
                                                            .horizontalAlign(core::HorizontalAlign::Right)
                                                            .verticalAlign(core::VerticalAlign::Center)
                                                            .build();
                                                    })
                                                    .build();
                                            })
                                            .onClick(onSelectRow)
                                            .build();
                                    })
                                    .build();
                            }
                        })
                        .build();

                    // Column Vertical Divider
                    ui.rect("input.wb.divider")
                        .width(1.0f)
                        .height(mainAreaHeight)
                        .color(theme.panelBorder)
                        .build();

                    // ========================================================
                    // Column 2 (Right): Live Event Timeline Stream
                    // ========================================================
                    ui.column("input.stream.col")
                        .size(eventStreamColWidth, mainAreaHeight)
                        .content([&] {
                            // Category Filter Bar + Clear Button
                            ui.stack("input.stream.header")
                                .width(core::SizeValue::fill())
                                .height(32.0f)
                                .content([&] {
                                    ui.rect("input.stream.header.bg")
                                        .fill()
                                        .ignoreLayout()
                                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.40f})
                                        .build();
                                    ui.rect("input.stream.header.border")
                                        .position(0.0f, 31.0f)
                                        .width(core::SizeValue::fill())
                                        .height(1.0f)
                                        .ignoreLayout()
                                        .color(theme.panelBorder)
                                        .build();
                                    ui.row("input.stream.header.row")
                                        .fill()
                                        .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            ui.row("input.cats")
                                                .width(core::SizeValue::fill())
                                                .height(24.0f)
                                                .gap(6.0f)
                                                .content([&] {
                                                    const auto addBtn = [&](InputCategory cat, const std::string& lbl) {
                                                        composeCategoryButton(ui, "input.cat." + std::string(inputCategoryName(cat)),
                                                                              lbl, currentCategory == cat,
                                                                              [cat, onSelect = actions.input.setCategory] {
                                                                                  if (onSelect) onSelect(cat);
                                                                              });
                                                    };
                                                    addBtn(InputCategory::All, "All");
                                                    addBtn(InputCategory::Pointer, "Pointer");
                                                    addBtn(InputCategory::Scroll, "Scroll");
                                                    addBtn(InputCategory::Keys, "Keys");
                                                    addBtn(InputCategory::Focus, "Focus");
                                                })
                                                .build();

                                            // Clear Log Button
                                            if (actions.input.clearHistory) {
                                                ui.stack("input.clear.btn")
                                                    .width(core::SizeValue::wrapContent())
                                                    .height(22.0f)
                                                    .content([&] {
                                                        auto bgRect = ui.rect("input.clear.btn.bg");
                                                        bgRect.fill()
                                                            .ignoreLayout()
                                                            .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.5f})
                                                            .radius(3.0f)
                                                            .states(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.5f},
                                                                    theme.elementRowHover, theme.elementRowHover);
                                                        if (actions.input.clearHistory) {
                                                            bgRect.onClick(actions.input.clearHistory);
                                                        }
                                                        bgRect.build();
                                                        ui.text("input.clear.btn.text")
                                                            .fontFamily(theme.fontFamily)
                                                            .margin(8.0f, 0.0f, 8.0f, 0.0f)
                                                            .width(core::SizeValue::wrapContent())
                                                            .height(22.0f)
                                                            .text("Clear Log")
                                                            .fontSize(theme.captionFontSize)
                                                            .color(theme.mutedText)
                                                            .verticalAlign(core::VerticalAlign::Center)
                                                            .build();
                                                    })
                                                    .onClick(actions.input.clearHistory)
                                                    .build();
                                            }
                                        })
                                        .build();
                                })
                                .build();

                            // Table Header
                            ui.stack("input.stream.th")
                                .width(core::SizeValue::fill())
                                .height(rowH)
                                .content([&] {
                                    ui.rect("input.stream.th.border")
                                        .position(0.0f, rowH - 1.0f)
                                        .width(core::SizeValue::fill())
                                        .height(1.0f)
                                        .ignoreLayout()
                                        .color(dividerColor)
                                        .build();
                                    ui.row("input.stream.th.cols")
                                        .fill()
                                        .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            ui.text("input.th.time")
                                                .fontFamily(theme.fontFamily)
                                                .width(65.0f)
                                                .height(rowH)
                                                .text("TIME")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.th.type")
                                                .fontFamily(theme.fontFamily)
                                                .width(75.0f)
                                                .height(rowH)
                                                .text("TYPE")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.th.target")
                                                .fontFamily(theme.fontFamily)
                                                .width(180.0f)
                                                .height(rowH)
                                                .text("TARGET ELEMENT")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                            ui.text("input.th.detail")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::fill())
                                                .height(rowH)
                                                .text("EVENT DETAILS / PAYLOAD")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(600)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();
                                        })
                                        .build();
                                })
                                .build();

                            // Event Stream List
                            const float streamListHeight = std::max(0.0f, mainAreaHeight - 32.0f - rowH);
                            if (filteredEvents.empty()) {
                                ui.stack("input.stream.empty")
                                    .size(eventStreamColWidth, streamListHeight)
                                    .align(core::Align::CENTER, core::Align::CENTER)
                                    .content([&] {
                                        ui.text("input.stream.empty.text")
                                            .fontFamily(theme.fontFamily)
                                            .width(core::SizeValue::wrapContent())
                                            .height(24.0f)
                                            .text("No input events recorded yet.")
                                            .fontSize(theme.elementRowFontSize)
                                            .color(theme.mutedText)
                                            .build();
                                    })
                                    .build();
                            } else {
                                components::virtualList(ui, "input.stream.list")
                                    .size(eventStreamColWidth, streamListHeight)
                                    .itemCount(static_cast<std::int64_t>(filteredEvents.size()))
                                    .rowHeight(rowH)
                                    .scrollbarGap(0.0f)
                                    .offset(scrollOffset)
                                    .onChange(actions.input.setScrollOffset)
                                    .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width, float height) {
                                        if (index < 0 || index >= static_cast<std::int64_t>(filteredEvents.size())) return;
                                        const InputEventItem& item = *filteredEvents[static_cast<std::size_t>(index)];
                                        const bool isSelected = (!selectedTarget.empty() && selectedTarget == item.targetId);
                                        const core::Color rowBg = isSelected ? theme.elementRowSelected : core::Color{0.0f, 0.0f, 0.0f, 0.0f};

                                        const auto onSelectRow = [onSelect = actions.input.selectTarget, id = item.targetId, det = item.detail] {
                                            const std::string textToCopy = !id.empty() ? id : det;
                                            core::window::setClipboardText(textToCopy);
                                            if (onSelect) onSelect(id);
                                        };

                                        rowUi.stack(rowId)
                                            .size(width, height)
                                            .content([&] {
                                                auto bgRect = rowUi.rect(rowId + ".bg");
                                                bgRect.fill()
                                                    .ignoreLayout()
                                                    .color(rowBg)
                                                    .states(rowBg, theme.elementRowHover, theme.elementRowHover);
                                                bgRect.onClick(onSelectRow);
                                                bgRect.build();
                                                rowUi.rect(rowId + ".border")
                                                    .position(0.0f, height - 1.0f)
                                                    .width(core::SizeValue::fill())
                                                    .height(1.0f)
                                                    .ignoreLayout()
                                                    .color(dividerColor)
                                                    .build();
                                                rowUi.row(rowId + ".cols")
                                                    .fill()
                                                    .padding(10.0f, 0.0f, 10.0f, 0.0f)
                                                    .alignItems(core::Align::CENTER)
                                                    .content([&] {
                                                        // Time (65px)
                                                        rowUi.text(rowId + ".time")
                                                            .fontFamily(theme.fontFamily)
                                                            .width(65.0f)
                                                            .height(height)
                                                            .text(formatTimestamp(item.timestamp))
                                                            .fontSize(theme.captionFontSize)
                                                            .color(theme.mutedText)
                                                            .verticalAlign(core::VerticalAlign::Center)
                                                            .build();

                                                        // Type Badge (75px)
                                                        rowUi.text(rowId + ".type")
                                                            .fontFamily(theme.fontFamily)
                                                            .width(75.0f)
                                                            .height(height)
                                                            .text(toolingInputKindName(item.kind))
                                                            .fontSize(theme.captionFontSize)
                                                            .fontWeight(600)
                                                            .color(toolingInputKindColor(item.kind, theme))
                                                            .verticalAlign(core::VerticalAlign::Center)
                                                            .build();

                                                        // Target ID (180px, clipped & truncated)
                                                        rowUi.stack(rowId + ".target.box")
                                                            .width(180.0f)
                                                            .height(height)
                                                            .clip()
                                                            .content([&] {
                                                                rowUi.text(rowId + ".target")
                                                                    .fontFamily(theme.fontFamily)
                                                                    .fill()
                                                                    .text(item.targetId.empty() ? "(none)" : truncateMiddle(item.targetId, 22))
                                                                    .fontSize(theme.elementRowFontSize)
                                                                    .fontWeight(item.targetId.empty() ? 400 : 500)
                                                                    .color(item.targetId.empty() ? theme.mutedText : theme.primaryText)
                                                                    .verticalAlign(core::VerticalAlign::Center)
                                                                    .build();
                                                            })
                                                            .build();

                                                        // Detail Payload (fill, clipped)
                                                        rowUi.stack(rowId + ".detail.box")
                                                            .width(core::SizeValue::fill())
                                                            .height(height)
                                                            .clip()
                                                            .content([&] {
                                                                rowUi.text(rowId + ".detail")
                                                                    .fontFamily(theme.fontFamily)
                                                                    .fill()
                                                                    .text(item.detail)
                                                                    .fontSize(theme.elementRowFontSize)
                                                                    .color(theme.metricValue)
                                                                    .verticalAlign(core::VerticalAlign::Center)
                                                                    .build();
                                                            })
                                                            .build();
                                                    })
                                                    .build();
                                            })
                                            .onClick(onSelectRow)
                                            .build();
                                    })
                                    .build();
                            }
                        })
                        .build();
                })
                .build();

            // ---- Selection Footer Bar ----
            if (!selectedTarget.empty()) {
                ui.stack("input.footer")
                    .size(state.panel.width, footerHeight)
                    .content([&] {
                        ui.rect("input.footer.bg")
                            .fill()
                            .ignoreLayout()
                            .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.45f})
                            .build();
                        ui.rect("input.footer.border")
                            .position(0.0f, 0.0f)
                            .width(core::SizeValue::fill())
                            .height(1.0f)
                            .ignoreLayout()
                            .color(theme.panelBorder)
                            .build();
                        ui.row("input.footer.cols")
                            .fill()
                            .padding(12.0f, 0.0f, 12.0f, 0.0f)
                            .alignItems(core::Align::CENTER)
                            .content([&] {
                                ui.text("input.footer.tag")
                                    .fontFamily(theme.fontFamily)
                                    .width(core::SizeValue::wrapContent())
                                    .height(footerHeight)
                                    .text("Selected (Copied): ")
                                    .fontSize(theme.captionFontSize)
                                    .fontWeight(600)
                                    .color(theme.accent)
                                    .verticalAlign(core::VerticalAlign::Center)
                                    .build();
                                ui.text("input.footer.val")
                                    .fontFamily(theme.fontFamily)
                                    .width(core::SizeValue::fill())
                                    .height(footerHeight)
                                    .text(selectedTarget)
                                    .fontSize(theme.elementRowFontSize)
                                    .fontWeight(600)
                                    .color(theme.primaryText)
                                    .verticalAlign(core::VerticalAlign::Center)
                                    .build();
                            })
                            .build();
                    })
                    .build();
            }
        })
        .build();
}

} // namespace modules::devtools
