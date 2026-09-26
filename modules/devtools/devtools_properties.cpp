#include "modules/devtools/devtools_properties.h"

#include "components/slider.h"
#include "components/switch.h"
#include "components/theme.h"
#include "components/virtuallist.h"
#include "modules/devtools/devtools_theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace modules::devtools {

namespace {

using core::dsl::runtime::DebugElementProperties;
using core::dsl::runtime::DebugPropertyId;
using core::dsl::runtime::DebugPropertyType;

// One property the area shows, with the range its editor covers. The table is the
// single place that knows how a property is presented: the runtime only knows how to
// read and write it, so adding a property is a row here plus a case there.
struct PropertyDescriptor {
    DebugPropertyId id;
    const char* label;
    float minimum;
    float maximum;
    bool textOnly;
};

const std::vector<PropertyDescriptor>& propertyDescriptors() {
    static const std::vector<PropertyDescriptor> descriptors{
        {DebugPropertyId::Color, "Color", 0.0f, 1.0f, false},
        {DebugPropertyId::Opacity, "Opacity", 0.0f, 1.0f, false},
        {DebugPropertyId::Radius, "Radius", 0.0f, 64.0f, false},
        {DebugPropertyId::BorderWidth, "Border", 0.0f, 16.0f, false},
        {DebugPropertyId::BorderColor, "Border color", 0.0f, 1.0f, false},
        {DebugPropertyId::Blur, "Blur", 0.0f, 64.0f, false},
        {DebugPropertyId::ShadowEnabled, "Shadow", 0.0f, 1.0f, false},
        {DebugPropertyId::ShadowColor, "Shadow color", 0.0f, 1.0f, false},
        {DebugPropertyId::ShadowBlur, "Shadow blur", 0.0f, 64.0f, false},
        {DebugPropertyId::ShadowOffsetY, "Shadow Y", -64.0f, 64.0f, false},
        {DebugPropertyId::TextColor, "Text color", 0.0f, 1.0f, true},
    };
    return descriptors;
}

const PropertyDescriptor* findDescriptor(DebugPropertyId property) {
    const std::vector<PropertyDescriptor>& descriptors = propertyDescriptors();
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
                                    [property](const PropertyDescriptor& descriptor) {
                                        return descriptor.id == property;
                                    });
    return found != descriptors.end() ? &*found : nullptr;
}

// Sliders and switches take a component theme, so the panel hands them its own
// palette instead of the default light one.
const components::theme::ThemeColorTokens& controlTheme() {
    static const components::theme::ThemeColorTokens tokens = [] {
        const DevtoolsTheme& theme = devtoolsTheme();
        auto value = components::theme::dark();
        value.background = theme.panelBackground;
        value.primary = theme.accent;
        value.surface = theme.toolbarBackground;
        value.surfaceHover = theme.toolbarHover;
        value.surfaceActive = theme.menuRowSelected;
        value.text = theme.primaryText;
        value.border = theme.panelBorder;
        return value;
    }();
    return tokens;
}

core::Transition controlTransition() {
    return core::Transition::make(0.16f, core::Ease::OutCubic);
}

float propertyNumber(const DebugElementProperties& properties, DebugPropertyId property) {
    switch (property) {
    case DebugPropertyId::Opacity: return properties.opacity;
    case DebugPropertyId::Radius: return properties.radius;
    case DebugPropertyId::BorderWidth: return properties.borderWidth;
    case DebugPropertyId::Blur: return properties.blur;
    case DebugPropertyId::ShadowBlur: return properties.shadow.blur;
    case DebugPropertyId::ShadowOffsetY: return properties.shadow.offset.y;
    default: return 0.0f;
    }
}

core::Color propertyColor(const DebugElementProperties& properties, DebugPropertyId property) {
    switch (property) {
    case DebugPropertyId::BorderColor: return properties.borderColor;
    case DebugPropertyId::ShadowColor: return properties.shadow.color;
    case DebugPropertyId::TextColor: return properties.textColor;
    default: return properties.color;
    }
}

bool propertyFlag(const DebugElementProperties& properties, DebugPropertyId property) {
    return property == DebugPropertyId::ShadowEnabled ? properties.shadow.enabled : false;
}

bool propertyOverridden(const DebugElementProperties& properties, DebugPropertyId property) {
    return (properties.overridden & core::dsl::runtime::debugPropertyBit(property)) != 0;
}

// The colour editor exposes the channels a picker would, so a colour can be built
// without a text field: the panel has no keyboard input yet.
void colorToHsv(const core::Color& color, float& hue, float& saturation, float& value) {
    const float maximum = std::max(std::max(color.r, color.g), color.b);
    const float minimum = std::min(std::min(color.r, color.g), color.b);
    const float delta = maximum - minimum;
    value = maximum;
    saturation = maximum <= 0.0f ? 0.0f : delta / maximum;
    if (delta <= 0.0f) {
        hue = 0.0f;
        return;
    }
    if (maximum == color.r) {
        hue = 60.0f * std::fmod((color.g - color.b) / delta, 6.0f);
    } else if (maximum == color.g) {
        hue = 60.0f * (((color.b - color.r) / delta) + 2.0f);
    } else {
        hue = 60.0f * (((color.r - color.g) / delta) + 4.0f);
    }
    if (hue < 0.0f) {
        hue += 360.0f;
    }
}

core::Color colorFromHsv(float hue, float saturation, float value, float alpha) {
    const float h = std::fmod(std::fmod(hue, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const float c = value * saturation;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = value - c;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (h < 1.0f) {
        r = c; g = x;
    } else if (h < 2.0f) {
        r = x; g = c;
    } else if (h < 3.0f) {
        g = c; b = x;
    } else if (h < 4.0f) {
        g = x; b = c;
    } else if (h < 5.0f) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }
    return {std::clamp(r + m, 0.0f, 1.0f), std::clamp(g + m, 0.0f, 1.0f), std::clamp(b + m, 0.0f, 1.0f),
            std::clamp(alpha, 0.0f, 1.0f)};
}

std::string formatNumber(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), std::fabs(value) < 10.0f ? "%.2f" : "%.1f", value);
    return buffer;
}

std::string formatHex(const core::Color& color) {
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", channel(color.r), channel(color.g), channel(color.b));
    return buffer;
}

// The property area is a flat list of uniform rows, so it reuses the virtualized list
// the tree uses: a colour editor that opens adds rows instead of nesting a layout.
enum class PropertyRowKind { Summary, Header, Property, Channel };

struct PropertyRow {
    PropertyRowKind kind = PropertyRowKind::Summary;
    const char* label = "";
    std::string value;
    DebugPropertyId property = DebugPropertyId::Color;
    int channel = 0;
    bool overridden = false;
    bool copyable = false;
};

const char* const kColorChannelLabels[4] = {"Hue", "Sat", "Value", "Alpha"};

void appendSummary(std::vector<PropertyRow>& rows, const char* label, std::string value) {
    PropertyRow row;
    row.kind = PropertyRowKind::Summary;
    row.label = label;
    row.value = std::move(value);
    rows.push_back(std::move(row));
}

std::vector<PropertyRow> buildPropertyRows(const DebugElementProperties& properties,
                                           const DevtoolsPanelState* panelState) {
    std::vector<PropertyRow> rows;
    rows.reserve(propertyDescriptors().size() + 10);

    char buffer[128];
    appendSummary(rows, "Id", properties.id);
    // The id is the one value worth copying out of the panel.
    rows.back().copyable = true;
    std::snprintf(buffer, sizeof(buffer), "%.0f, %.0f   %.0f x %.0f", properties.frame.x, properties.frame.y,
                  properties.frame.width, properties.frame.height);
    appendSummary(rows, "Frame", buffer);

    std::string flags = elementKindName(properties.kind);
    if (properties.clip) {
        flags += " | clip";
    }
    if (properties.interactive) {
        flags += " | interactive";
    }
    if (properties.disabled) {
        flags += " | disabled";
    }
    appendSummary(rows, "Flags", flags);

    std::snprintf(buffer, sizeof(buffer), "m %.0f/%.0f/%.0f/%.0f   p %.0f/%.0f/%.0f/%.0f   b %.0f",
                  properties.margin.left, properties.margin.top, properties.margin.right,
                  properties.margin.bottom, properties.padding.left, properties.padding.top,
                  properties.padding.right, properties.padding.bottom, properties.borderWidth);
    appendSummary(rows, "Box", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", properties.zIndex);
    appendSummary(rows, "Z", buffer);

    PropertyRow header;
    header.kind = PropertyRowKind::Header;
    header.label = "Visual";
    rows.push_back(header);

    for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
        if (descriptor.textOnly && properties.kind != core::dsl::ElementKind::Text) {
            continue;
        }
        PropertyRow row;
        row.kind = PropertyRowKind::Property;
        row.label = descriptor.label;
        row.property = descriptor.id;
        row.overridden = propertyOverridden(properties, descriptor.id);
        switch (core::dsl::runtime::debugPropertyType(descriptor.id)) {
        case DebugPropertyType::Color:
            row.value = formatHex(propertyColor(properties, descriptor.id));
            break;
        case DebugPropertyType::Flag:
            row.value = propertyFlag(properties, descriptor.id) ? "on" : "off";
            break;
        case DebugPropertyType::Number:
            row.value = formatNumber(propertyNumber(properties, descriptor.id));
            break;
        }
        rows.push_back(std::move(row));

        // The channels of the colour being edited sit right under their row, so the
        // list stays the only thing that scrolls.
        if (panelState == nullptr || !panelState->colorEditorOpen ||
            panelState->colorEditorProperty != descriptor.id ||
            core::dsl::runtime::debugPropertyType(descriptor.id) != DebugPropertyType::Color) {
            continue;
        }
        for (int channel = 0; channel < 4; ++channel) {
            PropertyRow channelRow;
            channelRow.kind = PropertyRowKind::Channel;
            channelRow.label = kColorChannelLabels[channel];
            channelRow.property = descriptor.id;
            channelRow.channel = channel;
            rows.push_back(std::move(channelRow));
        }
    }
    return rows;
}

// The way back to the element's own value. It only exists while the property is
// overridden, so the row says what a debug session changed by itself.
void composeRevertButton(core::dsl::Ui& ui, const std::string& id, const std::string& elementId,
                         DebugPropertyId property, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id + ".revert")
        .size(theme.propertyRevertWidth, theme.elementRowHeight)
        .icon(theme.iconRevert)
        .fontSize(theme.elementFontSize)
        .lineHeight(theme.elementRowHeight)
        .color(theme.accent)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .onClick([clear = actions.clearElementProperty, elementId, property] {
            if (clear) {
                clear(elementId, property);
            }
        })
        .build();
}

void composeColorSwatch(core::dsl::Ui& ui, const std::string& id, const std::string& elementId,
                        DebugPropertyId property, const core::Color& color, bool open,
                        const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    core::Color swatch = color;
    swatch.a = 1.0f;
    ui.stack(id + ".swatch")
        .size(theme.propertySwatchWidth, theme.elementRowHeight)
        .content([&] {
            ui.rect(id + ".swatch.box")
                .size(theme.propertySwatchSize, theme.propertySwatchSize)
                .position((theme.propertySwatchWidth - theme.propertySwatchSize) * 0.5f,
                          (theme.elementRowHeight - theme.propertySwatchSize) * 0.5f)
                .color(swatch)
                .radius(2.0f)
                .border(1.0f, open ? theme.accent : theme.panelBorder)
                .transition(controlTransition())
                .animate(core::AnimProperty::Color | core::AnimProperty::Border)
                .build();
        })
        .onClick([toggle = actions.togglePropertyColorEditor, property, open] {
            if (toggle) {
                toggle(property, !open);
            }
        })
        .build();
}

void composeValueText(core::dsl::Ui& ui, const std::string& id, const PropertyRow& row) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id + ".value")
        .size(theme.propertyValueWidth, theme.elementRowHeight)
        .text(row.value)
        .fontSize(theme.elementRowFontSize)
        .color(row.overridden ? theme.accent : theme.metricValue)
        .horizontalAlign(core::HorizontalAlign::Right)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

// One row of the property list. Numbers and colour channels drag, a colour opens its
// channels and a flag toggles; every row that a debug session replaced offers the way
// back.
void composePropertyRow(core::dsl::Ui& ui,
                        const std::string& id,
                        const PropertyRow& row,
                        const std::string& elementId,
                        const ElementPropertiesState& properties,
                        const DevtoolsUiState& state,
                        const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const bool hasRevert = row.kind == PropertyRowKind::Property && row.overridden;
    const float editorWidth = std::max(
        24.0f,
        state.panel.width - theme.propertyLabelWidth - theme.propertyValueWidth - theme.propertyRevertWidth -
            theme.elementDetailsPadding * 2.0f);

    ui.text(id + ".label")
        .size(theme.propertyLabelWidth, theme.elementRowHeight)
        .text(row.label)
        .fontSize(theme.elementRowFontSize)
        .color(row.kind == PropertyRowKind::Header ? theme.sectionLabel : theme.metricLabel)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
    if (row.kind == PropertyRowKind::Header) {
        return;
    }
    if (row.kind == PropertyRowKind::Summary) {
        const std::function<void()> copy = row.copyable && actions.copyElementId
            ? std::function<void()>([copy = actions.copyElementId, elementId] { copy(elementId); })
            : std::function<void()>{};
        ui.text(id + ".value")
            .width(core::SizeValue::fill())
            .height(theme.elementRowHeight)
            .text(row.value)
            .fontSize(theme.elementRowFontSize)
            .color(copy ? theme.accent : theme.metricValue)
            .verticalAlign(core::VerticalAlign::Center)
            .onClick(copy)
            .build();
        return;
    }

    if (row.kind == PropertyRowKind::Channel) {
        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        core::Color current{1.0f, 1.0f, 1.0f, 1.0f};
        if (state.panelState != nullptr) {
            current = propertyColor(*properties.properties, state.panelState->colorEditorProperty);
        }
        colorToHsv(current, hue, saturation, value);
        const float channels[4] = {hue / 360.0f, saturation, value, current.a};
        const float channelValue = channels[row.channel];

        components::slider(ui, id + ".slider")
            .size(std::max(24.0f, editorWidth), theme.elementRowHeight)
            .value(channelValue)
            .theme(controlTheme())
            .onChange([set = actions.setElementPropertyColor, elementId, property = row.property,
                       channel = row.channel, current](float normalized) {
                if (!set) {
                    return;
                }
                float h = 0.0f;
                float s = 0.0f;
                float v = 0.0f;
                colorToHsv(current, h, s, v);
                switch (channel) {
                case 0: h = normalized * 360.0f; break;
                case 1: s = normalized; break;
                case 2: v = normalized; break;
                default: break;
                }
                const float alpha = channel == 3 ? normalized : current.a;
                set(elementId, property, colorFromHsv(h, s, v, alpha));
            })
            .build();
        ui.text(id + ".value")
            .size(theme.propertyValueWidth, theme.elementRowHeight)
            .text(formatNumber(row.channel == 0 ? channelValue * 360.0f : channelValue))
            .fontSize(theme.elementRowFontSize)
            .color(theme.metricValue)
            .horizontalAlign(core::HorizontalAlign::Right)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
        return;
    }

    const DebugPropertyType type = core::dsl::runtime::debugPropertyType(row.property);
    switch (type) {
    case DebugPropertyType::Color: {
        if (properties.properties != nullptr && state.panelState != nullptr) {
            composeColorSwatch(ui, id, elementId, row.property,
                               propertyColor(*properties.properties, row.property),
                               state.panelState->colorEditorOpen &&
                                   state.panelState->colorEditorProperty == row.property,
                               actions);
        }
        ui.text(id + ".hex")
            .width(core::SizeValue::fill())
            .height(theme.elementRowHeight)
            .text(row.value)
            .fontSize(theme.elementRowFontSize)
            .color(theme.mutedText)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
        break;
    }
    case DebugPropertyType::Flag: {
        components::toggleSwitch(ui, id + ".switch")
            .size(theme.propertySwitchWidth, theme.elementRowHeight)
            .checked(row.value == "on")
            .trackSize(theme.propertySwitchTrackWidth, theme.propertySwitchTrackHeight)
            .theme(controlTheme())
            .onChange([set = actions.setElementPropertyFlag, elementId, property = row.property](bool value) {
                if (set) {
                    set(elementId, property, value);
                }
            })
            .build();
        ui.stack(id + ".spacer").width(core::SizeValue::fill()).height(theme.elementRowHeight).build();
        break;
    }
    case DebugPropertyType::Number: {
        const PropertyDescriptor* descriptor = findDescriptor(row.property);
        const float minimum = descriptor != nullptr ? descriptor->minimum : 0.0f;
        const float maximum = descriptor != nullptr ? descriptor->maximum : 1.0f;
        const float range = maximum - minimum;
        const float current = properties.properties != nullptr
            ? propertyNumber(*properties.properties, row.property)
            : 0.0f;
        components::slider(ui, id + ".slider")
            .size(std::max(24.0f, editorWidth), theme.elementRowHeight)
            .value(range > 0.0f ? (current - minimum) / range : 0.0f)
            .theme(controlTheme())
            .onChange([set = actions.setElementPropertyNumber, elementId, property = row.property, minimum,
                       range](float normalized) {
                if (set) {
                    set(elementId, property, minimum + normalized * range);
                }
            })
            .build();
        composeValueText(ui, id, row);
        if (!hasRevert) {
            return;
        }
        composeRevertButton(ui, id, elementId, row.property, actions);
        return;
    }
    }

    if (hasRevert) {
        composeRevertButton(ui, id, elementId, row.property, actions);
    } else {
        // A colour row keeps its value text aligned with the rows that have a revert
        // button, so the columns do not jump while the pointer moves around.
        ui.stack(id + ".revertSpacer").width(theme.propertyRevertWidth).height(theme.elementRowHeight).build();
    }
}

// The footer says whether the page still matches its code and offers to put every
// element back, so a debug session cannot leave a page looking like its source.
void composePropertyFooter(core::dsl::Ui& ui,
                           const std::string& id,
                           float width,
                           const ElementPropertiesState& properties,
                           const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    char buffer[96];
    if (properties.overrideCount == 0) {
        std::snprintf(buffer, sizeof(buffer), "Temporary edits, not written back");
    } else {
        std::snprintf(buffer, sizeof(buffer), "%zu element(s) overridden", properties.overrideCount);
    }
    ui.text(id + ".text")
        .size(std::max(0.0f, width - theme.propertyResetWidth), theme.elementRowHeight)
        .text(buffer)
        .fontSize(theme.elementRowFontSize - 1.0f)
        .color(properties.overrideCount == 0 ? theme.mutedText : theme.accent)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
    if (properties.overrideCount == 0) {
        return;
    }
    ui.text(id + ".reset")
        .size(theme.propertyResetWidth, theme.elementRowHeight)
        .text("Reset")
        .fontSize(theme.elementRowFontSize)
        .color(theme.accent)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .onClick([clear = actions.clearElementProperties] {
            if (clear) {
                clear();
            }
        })
        .build();
}

} // namespace

const std::vector<core::dsl::runtime::DebugPropertyId>& elementPropertyIds() {
    static const std::vector<core::dsl::runtime::DebugPropertyId> ids = [] {
        std::vector<core::dsl::runtime::DebugPropertyId> value;
        value.reserve(propertyDescriptors().size());
        for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
            value.push_back(descriptor.id);
        }
        return value;
    }();
    return ids;
}

void composeElementProperties(core::dsl::Ui& ui,
                              const std::string& id,
                              const core::Rect& area,
                              const ElementPropertiesState& properties,
                              const DevtoolsUiState& state,
                              const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.stack(id)
        .position(area.x, area.y)
        .size(area.width, area.height)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .color(theme.detailsBackground)
                .build();
            if (properties.properties == nullptr || !properties.properties->active) {
                ui.text(id + ".empty")
                    .width(core::SizeValue::fill())
                    .height(theme.elementRowHeight * 2.0f)
                    .text("Select an element to inspect it.")
                    .fontSize(theme.elementRowFontSize)
                    .color(theme.mutedText)
                    .horizontalAlign(core::HorizontalAlign::Center)
                    .verticalAlign(core::VerticalAlign::Center)
                    .build();
                return;
            }

            const std::string elementId = properties.properties->id;
            const std::vector<PropertyRow> rows = buildPropertyRows(*properties.properties, state.panelState);
            const float padding = theme.elementDetailsPadding;
            const float contentWidth = std::max(0.0f, area.width - padding * 2.0f);
            const float footerHeight = theme.elementRowHeight;
            const float listHeight = std::max(0.0f, area.height - footerHeight);
            const float scrollOffset =
                state.panelState != nullptr ? state.panelState->propertiesScrollOffset : 0.0f;

            // Everything inside the area is placed relative to it, which is what a
            // positioned stack expects of its children.
            components::virtualList(ui, id + ".list")
                .position(padding, 0.0f)
                .size(contentWidth, listHeight)
                .itemCount(static_cast<std::int64_t>(rows.size()))
                .rowHeight(theme.elementRowHeight)
                .offset(scrollOffset)
                .onChange(actions.setPropertiesScrollOffset)
                .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width,
                         float height) {
                    (void)width;
                    (void)height;
                    if (index < 0 || index >= static_cast<std::int64_t>(rows.size())) {
                        return;
                    }
                    rowUi.row(rowId + ".row")
                        .fill()
                        .gap(theme.propertyColumnGap)
                        .content([&] {
                            composePropertyRow(rowUi, rowId, rows[static_cast<std::size_t>(index)], elementId,
                                               properties, state, actions);
                        })
                        .build();
                })
                .build();

            ui.row(id + ".footer")
                .position(padding, listHeight)
                .size(contentWidth, footerHeight)
                .content([&] {
                    composePropertyFooter(ui, id + ".footer.inner",
                                          std::max(0.0f, area.width - theme.elementDetailsPadding * 2.0f),
                                          properties, actions);
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
