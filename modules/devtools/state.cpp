#include "modules/devtools/state.h"

#include "components/virtuallist.h"
#include "core/window/window_backend.h"
#include "modules/devtools/theme.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace modules::devtools {

namespace {

std::string formatFloat(float value, int decimals = 1) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

std::string formatRect(const core::Rect& r) {
    char text[96];
    std::snprintf(text, sizeof(text), "(%.0f, %.0f, %.0fx%.0f)", r.x, r.y, r.width, r.height);
    return text;
}

std::string formatRect(const core::LayoutRect& r) {
    char text[96];
    std::snprintf(text, sizeof(text), "(%.0f, %.0f, %.0fx%.0f)", r.x, r.y, r.width, r.height);
    return text;
}

std::string formatColor(const core::Color& c) {
    char text[64];
    std::snprintf(text, sizeof(text), "rgba(%.0f,%.0f,%.0f,%.2f)",
                  c.r * 255.0f, c.g * 255.0f, c.b * 255.0f, c.a);
    return text;
}

const char* boolStr(bool val) {
    return val ? "true" : "false";
}

void composeHeroCard(core::dsl::Ui& ui, const std::string& id, float cardWidth,
                     const char* title, const std::string& value,
                     const core::Color& valueColor, const char* subtitle) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color cardBg{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.40f};
    const core::Color cardBorder = theme.panelBorder;

    ui.stack(id)
        .width(cardWidth)
        .height(core::SizeValue::wrapContent())
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .color(cardBg)
                .radius(0.0f)
                .border(1.0f, cardBorder)
                .build();
            ui.column(id + ".content")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .padding(10.0f, 8.0f, 10.0f, 8.0f)
                .gap(2.0f)
                .content([&] {
                    ui.text(id + ".title")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(16.0f)
                        .text(title)
                        .fontSize(theme.captionFontSize)
                        .color(theme.metricLabel)
                        .build();
                    ui.text(id + ".value")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(26.0f)
                        .text(value)
                        .fontSize(theme.metricValueFontSize)
                        .fontWeight(600)
                        .color(valueColor)
                        .build();
                    ui.text(id + ".sub")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(15.0f)
                        .text(subtitle)
                        .fontSize(theme.captionFontSize)
                        .color(theme.mutedText)
                        .build();
                })
                .build();
        })
        .build();
}

void composeCategoryButton(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                           bool selected, const std::function<void()>& onClick,
                           bool isWarning = false) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color bg = selected
        ? theme.accent
        : core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.50f};
    const core::Color border = selected
        ? theme.accent
        : (isWarning ? core::Color{1.0f, 0.55f, 0.30f, 0.80f}
                     : core::Color{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.50f});
    const core::Color text = selected
        ? core::Color{1.0f, 1.0f, 1.0f, 1.0f}
        : (isWarning ? core::Color{1.0f, 0.65f, 0.35f, 1.0f} : theme.primaryText);

    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(24.0f)
        .content([&] {
            ui.rect(id + ".bg")
                .fill()
                .ignoreLayout()
                .color(bg)
                .border(1.0f, border)
                .radius(2.0f)
                .build();
            ui.text(id + ".lbl")
                .fontFamily(theme.fontFamily)
                .width(core::SizeValue::wrapContent())
                .height(24.0f)
                .padding(9.0f, 0.0f, 9.0f, 0.0f)
                .text(label)
                .fontSize(theme.captionFontSize)
                .fontWeight(selected ? 600 : 400)
                .color(text)
                .horizontalAlign(core::HorizontalAlign::Center)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .onClick(onClick)
        .build();
}

} // namespace

const char* stateCategoryName(StateCategory category) {
    switch (category) {
        case StateCategory::All: return "All";
        case StateCategory::Scroll: return "Scroll";
        case StateCategory::Slider: return "Slider";
        case StateCategory::Timers: return "Timers";
        case StateCategory::Interactions: return "Interactions";
        case StateCategory::Primitives: return "Primitives";
        case StateCategory::Layout: return "Layout";
        case StateCategory::Layers: return "Layers";
        case StateCategory::Unseen: return "Unseen (GC)";
    }
    return "Unknown";
}

const char* instanceKindName(InstanceKind kind) {
    switch (kind) {
        case InstanceKind::Rect: return "Rect";
        case InstanceKind::Polygon: return "Polygon";
        case InstanceKind::Text: return "Text";
        case InstanceKind::Image: return "Image";
        case InstanceKind::ShaderToy: return "ShaderToy";
        case InstanceKind::Interaction: return "Interaction";
        case InstanceKind::DirtyKey: return "DirtyKey";
        case InstanceKind::Layout: return "Layout";
        case InstanceKind::ScrollState: return "ScrollState";
        case InstanceKind::SliderState: return "SliderState";
        case InstanceKind::Timer: return "Timer";
        case InstanceKind::DependentVisualState: return "VisualState";
        case InstanceKind::FrameTarget: return "FrameTarget";
        case InstanceKind::PaintBounds: return "PaintBounds";
        case InstanceKind::RetainedLayer: return "RetainedLayer";
    }
    return "Unknown";
}

InstanceStateSnapshot captureInstanceState(const core::dsl::Runtime& page) {
    InstanceStateSnapshot snapshot;
#if EUI_TOOLING_ENABLED
    const auto& store = page.instances();

    // 1. Scroll states
    for (const auto& [id, inst] : store.scrollStates) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::ScrollState;
        entry.category = StateCategory::Scroll;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "offset: " + formatFloat(inst.offset, 1) +
                        " / max: " + formatFloat(inst.maxOffset, 1) +
                        ", vel: " + formatFloat(inst.velocity, 1) +
                        ", step: " + formatFloat(inst.step, 1);
        if (inst.maxOffset <= 0.0f) {
            entry.summary += " [STUCK: maxOffset=0]";
        } else if (inst.offset >= inst.maxOffset) {
            entry.summary += " [AT END]";
        }
        entry.details = "dragStart: " + formatFloat(inst.dragStartOffset, 1) +
                        (inst.hasDirtyRect ? (", dirty: " + formatRect(inst.dirtyRect)) : "");
        snapshot.entries.push_back(std::move(entry));
        snapshot.scrollCount++;
    }

    // 2. Slider states
    for (const auto& [id, inst] : store.sliderStates) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::SliderState;
        entry.category = StateCategory::Slider;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "value: " + formatFloat(inst.value, 3) +
                        ", dragging: " + boolStr(inst.dragging) +
                        ", width: " + formatFloat(inst.width, 1) +
                        ", knob: " + formatFloat(inst.knobSize, 1);
        entry.details = inst.hasDirtyRect ? ("dirty: " + formatRect(inst.dirtyRect)) : "";
        snapshot.entries.push_back(std::move(entry));
        snapshot.sliderCount++;
    }

    // 3. Timers
    for (const auto& [id, inst] : store.timers) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Timer;
        entry.category = StateCategory::Timers;
        entry.seen = inst.seen;
        entry.initialized = true;
        entry.summary = "elapsed: " + formatFloat(inst.elapsed, 2) +
                        "s / " + formatFloat(inst.seconds, 2) +
                        "s, active: " + boolStr(inst.active);
        snapshot.entries.push_back(std::move(entry));
        snapshot.timerCount++;
    }

    // 4. Interactions
    for (const auto& [id, inst] : store.interactions) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Interaction;
        entry.category = StateCategory::Interactions;
        entry.seen = inst.seen;
        entry.initialized = true;
        entry.summary = "hover: " + std::string(boolStr(inst.state.hover)) +
                        ", pressed: " + boolStr(inst.state.pressed) +
                        ", active: " + boolStr(inst.state.active);
        entry.details = "contextActive: " + std::string(boolStr(inst.contextActive)) +
                        ", contextDragged: " + boolStr(inst.contextDragged);
        snapshot.entries.push_back(std::move(entry));
        snapshot.interactionCount++;
    }

    // 5. Rects
    for (const auto& [id, inst] : store.rects) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Rect;
        entry.category = StateCategory::Primitives;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "frame: " + formatRect(inst.frame.value()) +
                        ", color: " + formatColor(inst.color.value()) +
                        ", radius: " + formatFloat(inst.radius.value(), 1);
        snapshot.entries.push_back(std::move(entry));
        snapshot.primitiveCount++;
    }

    // 6. Polygons
    for (const auto& [id, inst] : store.polygons) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Polygon;
        entry.category = StateCategory::Primitives;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "points: " + std::to_string(inst.points.size()) +
                        ", color: " + formatColor(inst.color.value());
        snapshot.entries.push_back(std::move(entry));
        snapshot.primitiveCount++;
    }

    // 7. Texts
    for (const auto& [id, inst] : store.texts) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Text;
        entry.category = StateCategory::Primitives;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        std::string snippet = inst.text.substr(0, 24);
        if (inst.text.size() > 24) snippet += "...";
        entry.summary = "\"" + snippet + "\", font: " + formatFloat(inst.fontSize, 1) +
                        "px, weight: " + std::to_string(inst.fontWeight) +
                        ", wrap: " + boolStr(inst.wrap);
        snapshot.entries.push_back(std::move(entry));
        snapshot.primitiveCount++;
    }

    // 8. Images
    for (const auto& [id, inst] : store.images) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Image;
        entry.category = StateCategory::Primitives;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "src: " + inst.source + ", gpuRev: " + std::to_string(inst.gpuImageRevision);
        snapshot.entries.push_back(std::move(entry));
        snapshot.primitiveCount++;
    }

    // 9. ShaderToys
    for (const auto& [id, inst] : store.shaderToys) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::ShaderToy;
        entry.category = StateCategory::Primitives;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "graphHash: " + std::to_string(inst.graphHash) +
                        ", resetKey: " + std::to_string(inst.resetKey);
        snapshot.entries.push_back(std::move(entry));
        snapshot.primitiveCount++;
    }

    // 10. Layouts
    for (const auto& [id, inst] : store.layouts) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::Layout;
        entry.category = StateCategory::Layout;
        entry.seen = inst.seen;
        entry.initialized = true;
        entry.summary = "opacity: " + formatFloat(inst.opacity.value(), 2);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layoutCount++;
    }

    // 11. FrameTargets
    for (const auto& [id, inst] : store.frameTargets) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::FrameTarget;
        entry.category = StateCategory::Layout;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "target: " + formatRect(inst.frame);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layoutCount++;
    }

    // 12. PaintBounds
    for (const auto& [id, inst] : store.paintBounds) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::PaintBounds;
        entry.category = StateCategory::Layout;
        entry.seen = inst.seen;
        entry.initialized = true;
        entry.summary = "drawCost: " + std::to_string(inst.drawCost) +
                        ", own: " + (inst.hasOwn ? formatRect(inst.own) : "none") +
                        ", subtreeAnim: " + boolStr(inst.subtreeAnimating);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layoutCount++;
    }

    // 13. RetainedLayers
    for (const auto& [id, inst] : store.retainedLayers) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::RetainedLayer;
        entry.category = StateCategory::Layers;
        entry.seen = inst.seen;
        entry.initialized = (inst.handle != nullptr);
        entry.summary = "size: " + std::to_string(inst.width) + "x" + std::to_string(inst.height) +
                        ", valid: " + boolStr(inst.valid) +
                        ", stableFrames: " + std::to_string(inst.pendingStableFrames);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layerCount++;
    }

    // 14. DirtyKeys
    for (const auto& [id, inst] : store.dirtyKeys) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::DirtyKey;
        entry.category = StateCategory::Layers;
        entry.seen = inst.seen;
        entry.initialized = inst.initialized;
        entry.summary = "key: " + inst.key + ", rect: " + formatRect(inst.rect);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layerCount++;
    }

    // 15. DependentVisualStates
    for (const auto& [id, inst] : store.dependentVisualStates) {
        InstanceEntry entry;
        entry.id = id;
        entry.kind = InstanceKind::DependentVisualState;
        entry.category = StateCategory::Layers;
        entry.seen = inst.seen;
        entry.initialized = true;
        entry.summary = "opacity: " + formatFloat(inst.opacity, 2) +
                        ", scale: " + formatFloat(inst.scale, 2) +
                        ", rect: " + formatRect(inst.rect);
        snapshot.entries.push_back(std::move(entry));
        snapshot.layerCount++;
    }

    // Count totals
    snapshot.totalCount = snapshot.entries.size();
    for (const auto& entry : snapshot.entries) {
        if (entry.seen) {
            snapshot.seenCount++;
        } else {
            snapshot.unseenCount++;
        }
    }

    // Sort: Scroll/Slider first, then by ID
    std::sort(snapshot.entries.begin(), snapshot.entries.end(), [](const InstanceEntry& a, const InstanceEntry& b) {
        const auto priority = [](InstanceKind k) {
            if (k == InstanceKind::ScrollState) return 0;
            if (k == InstanceKind::SliderState) return 1;
            if (k == InstanceKind::Timer) return 2;
            if (k == InstanceKind::Interaction) return 3;
            return 10;
        };
        const int pa = priority(a.kind);
        const int pb = priority(b.kind);
        if (pa != pb) return pa < pb;
        return a.id < b.id;
    });
#endif
    return snapshot;
}

static std::string truncateMiddle(const std::string& str, std::size_t maxLen) {
    if (str.size() <= maxLen) return str;
    if (maxLen <= 5) return str.substr(0, maxLen);
    const std::size_t head = (maxLen - 3) / 2;
    const std::size_t tail = maxLen - 3 - head;
    return str.substr(0, head) + "..." + str.substr(str.size() - tail);
}

void composeStateTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    static const InstanceStateSnapshot emptySnapshot;
    const DevtoolsTheme& theme = devtoolsTheme();
    const InstanceStateSnapshot& snapshot = state.instanceState != nullptr ? *state.instanceState : emptySnapshot;
    const float scrollOffset = state.panelState != nullptr ? state.panelState->stateScrollOffset : 0.0f;
    const StateCategory currentCategory = state.panelState != nullptr ? state.panelState->stateCategory : StateCategory::All;
    const std::string selectedId = state.panelState != nullptr ? state.panelState->selectedInstanceId : "";
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));

    // Pre-filter entries by current category
    std::vector<const InstanceEntry*> filteredEntries;
    filteredEntries.reserve(snapshot.entries.size());
    const InstanceEntry* selectedEntry = nullptr;
    for (const auto& entry : snapshot.entries) {
        if (entry.id == selectedId) {
            selectedEntry = &entry;
        }
        if (currentCategory == StateCategory::Unseen) {
            if (!entry.seen) filteredEntries.push_back(&entry);
        } else if (currentCategory != StateCategory::All) {
            if (entry.category == currentCategory) filteredEntries.push_back(&entry);
        } else {
            filteredEntries.push_back(&entry);
        }
    }

    const float availableWidth = std::max(120.0f, state.panel.width - theme.metricPaddingHorizontal * 2.0f);
    const int numCards = 4;
    const float colGap = 8.0f;
    const float cardWidth = std::max(120.0f, (availableWidth - colGap * (numCards - 1)) / static_cast<float>(numCards));
    const float rowH = 26.0f;
    const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.28f};

    const float colKindWidth = 100.0f;
    const float colIdWidth = 230.0f;
    const float colGcWidth = 90.0f;

    // Height of the top pinned section: Hero cards (68px) + gap (8px) + categories (24px) + gap (8px) + table header (26px) + vertical padding (14px)
    const float topSectionHeight = 148.0f;
    const float footerHeight = selectedEntry != nullptr ? 26.0f : 0.0f;
    const float listHeight = std::max(0.0f, contentHeight - topSectionHeight - footerHeight);

    ui.column("state.main")
        .size(state.panel.width, contentHeight)
        .content([&] {
            // ---- Pinned Top Section ----
            ui.column("state.top")
                .width(state.panel.width)
                .height(topSectionHeight)
                .padding(theme.metricPaddingHorizontal, 8.0f, theme.metricPaddingHorizontal, 0.0f)
                .gap(8.0f)
                .content([&] {
                    // 1. Hero Cards
                    ui.row("state.hero.cards")
                        .width(core::SizeValue::fill())
                        .height(core::SizeValue::wrapContent())
                        .gap(colGap)
                        .content([&] {
                            composeHeroCard(ui, "state.hero.total", cardWidth,
                                            "TOTAL INSTANCES", std::to_string(snapshot.totalCount),
                                            theme.metricValue, "Retained runtime objects");
                            composeHeroCard(ui, "state.hero.active", cardWidth,
                                            "ACTIVE (SEEN)", std::to_string(snapshot.seenCount),
                                            theme.accent, "Referenced in tree");
                            const core::Color orphanColor = snapshot.unseenCount > 0
                                ? core::Color{1.0f, 0.55f, 0.30f, 1.0f}
                                : theme.mutedText;
                            composeHeroCard(ui, "state.hero.orphan", cardWidth,
                                            "UNSEEN (GC CANDIDATE)", std::to_string(snapshot.unseenCount),
                                            orphanColor, snapshot.unseenCount > 0 ? "Pending collection" : "Clean (No leaks)");
                            composeHeroCard(ui, "state.hero.interactive", cardWidth,
                                            "INTERACTIVE", std::to_string(snapshot.scrollCount + snapshot.sliderCount),
                                            core::Color{0.35f, 0.85f, 0.55f, 1.0f}, "Scroll & Slider controllers");
                        })
                        .build();

                    // 2. Category Filter Row
                    ui.row("state.categories")
                        .width(core::SizeValue::fill())
                        .height(24.0f)
                        .gap(6.0f)
                        .content([&] {
                            const auto addCatBtn = [&](StateCategory cat, const std::string& label, bool warning = false) {
                                composeCategoryButton(ui, "state.cat." + std::string(stateCategoryName(cat)),
                                                      label, currentCategory == cat,
                                                      [cat, onSelect = actions.state.setCategory] {
                                                          if (onSelect) onSelect(cat);
                                                      }, warning);
                            };
                            addCatBtn(StateCategory::All, "All (" + std::to_string(snapshot.totalCount) + ")");
                            addCatBtn(StateCategory::Scroll, "Scroll (" + std::to_string(snapshot.scrollCount) + ")");
                            addCatBtn(StateCategory::Slider, "Slider (" + std::to_string(snapshot.sliderCount) + ")");
                            addCatBtn(StateCategory::Timers, "Timers (" + std::to_string(snapshot.timerCount) + ")");
                            addCatBtn(StateCategory::Interactions, "Interactions (" + std::to_string(snapshot.interactionCount) + ")");
                            addCatBtn(StateCategory::Primitives, "Primitives (" + std::to_string(snapshot.primitiveCount) + ")");
                            addCatBtn(StateCategory::Layout, "Layout (" + std::to_string(snapshot.layoutCount) + ")");
                            addCatBtn(StateCategory::Layers, "Layers (" + std::to_string(snapshot.layerCount) + ")");
                            addCatBtn(StateCategory::Unseen, "Unseen GC (" + std::to_string(snapshot.unseenCount) + ")", snapshot.unseenCount > 0);
                        })
                        .build();

                    // 3. Table Header
                    ui.stack("state.table.header")
                        .width(core::SizeValue::fill())
                        .height(rowH)
                        .content([&] {
                            ui.rect("state.table.header.bg")
                                .fill()
                                .ignoreLayout()
                                .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.35f})
                                .build();
                            ui.rect("state.table.header.border")
                                .position(0.0f, rowH - 1.0f)
                                .width(core::SizeValue::fill())
                                .height(1.0f)
                                .ignoreLayout()
                                .color(theme.panelBorder)
                                .build();
                            ui.row("state.table.header.cols")
                                .fill()
                                .padding(12.0f, 0.0f, 12.0f, 0.0f)
                                .alignItems(core::Align::CENTER)
                                .content([&] {
                                    ui.text("state.th.kind")
                                        .fontFamily(theme.fontFamily)
                                        .width(colKindWidth)
                                        .height(rowH)
                                        .text("TYPE")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(600)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                    ui.text("state.th.id")
                                        .fontFamily(theme.fontFamily)
                                        .width(colIdWidth)
                                        .height(rowH)
                                        .text("INSTANCE ID")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(600)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                    ui.text("state.th.state")
                                        .fontFamily(theme.fontFamily)
                                        .width(core::SizeValue::fill())
                                        .height(rowH)
                                        .text("RUNTIME STATE / METRICS")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(600)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                    ui.text("state.th.gc")
                                        .fontFamily(theme.fontFamily)
                                        .width(colGcWidth)
                                        .height(rowH)
                                        .text("GC STATUS")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(600)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                })
                                .build();
                        })
                        .build();
                })
                .build();

            // ---- Virtualized Rows (Only renders visible rows in viewport!) ----
            if (filteredEntries.empty()) {
                ui.stack("state.empty")
                    .size(state.panel.width, listHeight)
                    .align(core::Align::CENTER, core::Align::CENTER)
                    .content([&] {
                        ui.text("state.empty.text")
                            .fontFamily(theme.fontFamily)
                            .width(core::SizeValue::wrapContent())
                            .height(24.0f)
                            .text("No instances found for this category.")
                            .fontSize(theme.elementRowFontSize)
                            .color(theme.mutedText)
                            .build();
                    })
                    .build();
            } else {
                components::virtualList(ui, "state.list")
                    .size(state.panel.width, listHeight)
                    .itemCount(static_cast<std::int64_t>(filteredEntries.size()))
                    .rowHeight(rowH)
                    .scrollbarGap(0.0f)
                    .offset(scrollOffset)
                    .onChange(actions.state.setScrollOffset)
                    .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width, float height) {
                        if (index < 0 || index >= static_cast<std::int64_t>(filteredEntries.size())) {
                            return;
                        }
                        const InstanceEntry& entry = *filteredEntries[static_cast<std::size_t>(index)];
                        const bool isSelected = (selectedId == entry.id);
                        const core::Color rowBg = isSelected
                            ? theme.elementRowSelected
                            : core::Color{0.0f, 0.0f, 0.0f, 0.0f};

                        const auto onSelectRow = [onSelect = actions.state.selectInstance, id = entry.id] {
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
                                    .padding(12.0f, 0.0f, 12.0f, 0.0f)
                                    .alignItems(core::Align::CENTER)
                                    .content([&] {
                                        // Kind (100px)
                                        rowUi.text(rowId + ".kind")
                                            .fontFamily(theme.fontFamily)
                                            .width(colKindWidth)
                                            .height(height)
                                            .text(instanceKindName(entry.kind))
                                            .fontSize(theme.captionFontSize)
                                            .fontWeight(500)
                                            .color(theme.elementKindText)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();

                                        // ID (colIdWidth, clipped with smart middle truncation)
                                        rowUi.stack(rowId + ".id.box")
                                            .width(colIdWidth)
                                            .height(height)
                                            .clip()
                                            .content([&] {
                                                rowUi.text(rowId + ".id")
                                                    .fontFamily(theme.fontFamily)
                                                    .fill()
                                                    .text(truncateMiddle(entry.id, 28))
                                                    .fontSize(theme.elementRowFontSize)
                                                    .fontWeight(isSelected ? 600 : 400)
                                                    .color(isSelected ? theme.accent : theme.primaryText)
                                                    .verticalAlign(core::VerticalAlign::Center)
                                                    .build();
                                            })
                                            .build();

                                        // Summary (fill, clipped)
                                        const core::Color stateColor = (entry.kind == InstanceKind::ScrollState || entry.kind == InstanceKind::SliderState)
                                            ? theme.primaryText
                                            : theme.mutedText;
                                        rowUi.stack(rowId + ".summary.box")
                                            .width(core::SizeValue::fill())
                                            .height(height)
                                            .clip()
                                            .content([&] {
                                                rowUi.text(rowId + ".summary")
                                                    .fontFamily(theme.fontFamily)
                                                    .fill()
                                                    .text(entry.summary)
                                                    .fontSize(theme.elementRowFontSize)
                                                    .fontWeight(400)
                                                    .color(stateColor)
                                                    .verticalAlign(core::VerticalAlign::Center)
                                                    .build();
                                            })
                                            .build();

                                        // GC Status (colGcWidth)
                                        const core::Color gcColor = entry.seen
                                            ? core::Color{0.35f, 0.85f, 0.55f, 1.0f}
                                            : core::Color{1.0f, 0.55f, 0.30f, 1.0f};
                                        rowUi.text(rowId + ".gc")
                                            .fontFamily(theme.fontFamily)
                                            .width(colGcWidth)
                                            .height(height)
                                            .text(entry.seen ? "Seen" : "Unseen (GC)")
                                            .fontSize(theme.captionFontSize)
                                            .fontWeight(600)
                                            .color(gcColor)
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

            // ---- Detail / Status Footer ----
            if (selectedEntry != nullptr) {
                ui.stack("state.footer")
                    .size(state.panel.width, footerHeight)
                    .content([&] {
                        ui.rect("state.footer.bg")
                            .fill()
                            .ignoreLayout()
                            .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.45f})
                            .build();
                        ui.rect("state.footer.border")
                            .position(0.0f, 0.0f)
                            .width(core::SizeValue::fill())
                            .height(1.0f)
                            .ignoreLayout()
                            .color(theme.panelBorder)
                            .build();
                        ui.row("state.footer.cols")
                            .fill()
                            .padding(12.0f, 0.0f, 12.0f, 0.0f)
                            .alignItems(core::Align::CENTER)
                            .content([&] {
                                ui.text("state.footer.tag")
                                    .fontFamily(theme.fontFamily)
                                    .width(core::SizeValue::wrapContent())
                                    .height(footerHeight)
                                    .text("Selected (Copied): ")
                                    .fontSize(theme.captionFontSize)
                                    .fontWeight(600)
                                    .color(theme.accent)
                                    .verticalAlign(core::VerticalAlign::Center)
                                    .build();
                                ui.text("state.footer.id")
                                    .fontFamily(theme.fontFamily)
                                    .width(core::SizeValue::wrapContent())
                                    .height(footerHeight)
                                    .text(selectedEntry->id)
                                    .fontSize(theme.elementRowFontSize)
                                    .fontWeight(600)
                                    .color(theme.primaryText)
                                    .verticalAlign(core::VerticalAlign::Center)
                                    .build();
                                if (!selectedEntry->details.empty()) {
                                    ui.text("state.footer.details")
                                        .fontFamily(theme.fontFamily)
                                        .width(core::SizeValue::fill())
                                        .height(footerHeight)
                                        .text("  [" + selectedEntry->details + "]")
                                        .fontSize(theme.captionFontSize)
                                        .color(theme.mutedText)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                }
                            })
                            .build();
                    })
                    .build();
            }
        })
        .build();
}

} // namespace modules::devtools
