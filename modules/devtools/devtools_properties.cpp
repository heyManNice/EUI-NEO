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

using core::dsl::ElementKind;
using modules::devtools::ElementValues;
using modules::devtools::ElementField;
using modules::devtools::FieldKind;

// One property the area shows, with the range its editor covers and the kinds of element
// it can change something on. The label, the range and those kinds are the display side of
// a field; the field itself (its kind and the member it reads) is in devtools_fields.h, so
// adding a property is one row here and one row there.
struct PropertyDescriptor {
    ElementField field;
    const char* label;
    float minimum;
    float maximum;
    std::uint32_t kinds;
};

constexpr std::uint32_t elementKindBit(ElementKind kind) {
    return 1u << static_cast<std::uint32_t>(kind);
}

template <typename... Kinds>
constexpr std::uint32_t kindsOf(Kinds... kinds) {
    return (elementKindBit(kinds) | ...);
}

constexpr std::uint32_t kEveryKind = (elementKindBit(ElementKind::Shadertoy) << 1u) - 1u;

// Which kinds carry a property. Only a rect draws the rounded box that has a border, a
// shadow or a gradient; the polygon, image and shadertoy primitives each carry their own
// subset, and a text element paints glyphs instead of a box. A row that would change
// nothing on the selected element is left out, so the list stays about that element.
constexpr std::uint32_t kBoxKinds = kindsOf(ElementKind::Rect);
constexpr std::uint32_t kColorKinds =
    kindsOf(ElementKind::Rect, ElementKind::Polygon, ElementKind::Image, ElementKind::Svg);
constexpr std::uint32_t kRadiusKinds =
    kindsOf(ElementKind::Rect, ElementKind::Polygon, ElementKind::Image, ElementKind::Svg, ElementKind::Shadertoy);
constexpr std::uint32_t kBlurKinds = kindsOf(ElementKind::Rect, ElementKind::Image, ElementKind::Svg);

const std::vector<PropertyDescriptor>& propertyDescriptors() {
    static const std::vector<PropertyDescriptor> descriptors{
        {ElementField::Color, "Color", 0.0f, 1.0f, kColorKinds},
        {ElementField::Opacity, "Opacity", 0.0f, 1.0f, kEveryKind},
        {ElementField::Radius, "Radius", 0.0f, 64.0f, kRadiusKinds},
        {ElementField::BorderWidth, "Border", 0.0f, 16.0f, kBoxKinds},
        {ElementField::BorderColor, "Border color", 0.0f, 1.0f, kBoxKinds},
        {ElementField::Blur, "Blur", 0.0f, 64.0f, kBlurKinds},
        {ElementField::ShadowEnabled, "Shadow", 0.0f, 1.0f, kBoxKinds},
        {ElementField::ShadowColor, "Shadow color", 0.0f, 1.0f, kBoxKinds},
        {ElementField::ShadowBlur, "Shadow blur", 0.0f, 64.0f, kBoxKinds},
        {ElementField::ShadowSpread, "Shadow spread", 0.0f, 64.0f, kBoxKinds},
        {ElementField::ShadowOffsetX, "Shadow X", -64.0f, 64.0f, kBoxKinds},
        {ElementField::ShadowOffsetY, "Shadow Y", -64.0f, 64.0f, kBoxKinds},
        {ElementField::ShadowInset, "Shadow inset", 0.0f, 1.0f, kBoxKinds},
        {ElementField::GradientEnabled, "Gradient", 0.0f, 1.0f, kBoxKinds},
        {ElementField::GradientStart, "Gradient from", 0.0f, 1.0f, kBoxKinds},
        {ElementField::GradientEnd, "Gradient to", 0.0f, 1.0f, kBoxKinds},
        {ElementField::TextColor, "Text color", 0.0f, 1.0f, kindsOf(ElementKind::Text)},
    };
    return descriptors;
}

const PropertyDescriptor* findDescriptor(ElementField field) {
    const std::vector<PropertyDescriptor>& descriptors = propertyDescriptors();
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
                                    [field](const PropertyDescriptor& descriptor) {
                                        return descriptor.field == field;
                                    });
    return found != descriptors.end() ? &*found : nullptr;
}

core::Transition controlTransition() {
    return core::Transition::make(0.16f, core::Ease::OutCubic);
}

// A row reads a value it added without a switch of its own: the field table
// (devtools_fields.h) already carries the kind, and the three readers are here so a row
// reads like the kind it is rather than like the union.
float propertyNumber(const ElementValues& values, ElementField field) {
    return values.field(field).number;
}

core::Color propertyColor(const ElementValues& values, ElementField field) {
    return values.field(field).color;
}

bool propertyFlag(const ElementValues& values, ElementField field) {
    return values.field(field).flag;
}

bool propertyOverridden(const ElementValues& values, ElementField field) {
    return values.wasWritten(field);
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

const char* alignName(core::Align align) {
    switch (align) {
    case core::Align::START: return "Start";
    case core::Align::CENTER: return "Center";
    case core::Align::END: return "End";
    }
    return "Unknown";
}

const char* sizeModeName(core::SizeMode mode) {
    switch (mode) {
    case core::SizeMode::Fixed: return "Fixed";
    case core::SizeMode::WrapContent: return "WrapContent";
    case core::SizeMode::Fill: return "Fill";
    }
    return "Unknown";
}

std::string formatSizeValue(const core::SizeValue& size) {
    if (size.mode == core::SizeMode::Fixed) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Fixed(%.0f)", size.value);
        return buf;
    }
    return sizeModeName(size.mode);
}

const char* horizontalAlignName(core::HorizontalAlign align) {
    switch (align) {
    case core::HorizontalAlign::Left: return "Left";
    case core::HorizontalAlign::Center: return "Center";
    case core::HorizontalAlign::Right: return "Right";
    }
    return "Unknown";
}

const char* verticalAlignName(core::VerticalAlign align) {
    switch (align) {
    case core::VerticalAlign::Top: return "Top";
    case core::VerticalAlign::Center: return "Center";
    case core::VerticalAlign::Bottom: return "Bottom";
    }
    return "Unknown";
}

const char* imageFitName(core::ImageFit fit) {
    switch (fit) {
    case core::ImageFit::Cover: return "Cover";
    case core::ImageFit::Contain: return "Contain";
    case core::ImageFit::Stretch: return "Stretch";
    }
    return "Unknown";
}

const char* cursorShapeName(core::CursorShape cursor) {
    switch (cursor) {
    case core::CursorShape::Arrow: return "Arrow";
    case core::CursorShape::Hand: return "Hand";
    }
    return "Unknown";
}

// The property area is a flat list of uniform rows, so it reuses the virtualized list
// the tree uses: a colour editor that opens adds rows instead of nesting a layout.
enum class PropertyRowKind { Summary, Header, Property, Channel };

struct PropertyRow {
    PropertyRowKind kind = PropertyRowKind::Summary;
    const char* label = "";
    std::string value;
    ElementField field = ElementField::Color;
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

std::vector<PropertyRow> buildVisualPropertyRows(const ElementValues& properties,
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
        if ((descriptor.kinds & elementKindBit(properties.kind)) == 0) {
            continue;
        }
        PropertyRow row;
        row.kind = PropertyRowKind::Property;
        row.label = descriptor.label;
        row.field = descriptor.field;
        row.overridden = propertyOverridden(properties, descriptor.field);
        switch (fieldKind(descriptor.field)) {
        case FieldKind::Color:
            row.value = formatHex(propertyColor(properties, descriptor.field));
            break;
        case FieldKind::Flag:
            row.value = propertyFlag(properties, descriptor.field) ? "on" : "off";
            break;
        case FieldKind::Number:
            row.value = formatNumber(propertyNumber(properties, descriptor.field));
            break;
        }
        rows.push_back(std::move(row));

        // The channels of the colour being edited sit right under their row, so the
        // list stays the only thing that scrolls.
        if (panelState == nullptr || !panelState->colorEditorOpen ||
            panelState->colorEditorField != descriptor.field ||
            fieldKind(descriptor.field) != FieldKind::Color) {
            continue;
        }
        for (int channel = 0; channel < 4; ++channel) {
            PropertyRow channelRow;
            channelRow.kind = PropertyRowKind::Channel;
            channelRow.label = kColorChannelLabels[channel];
            channelRow.field = descriptor.field;
            channelRow.channel = channel;
            rows.push_back(std::move(channelRow));
        }
    }
    return rows;
}

std::vector<PropertyRow> buildLayoutPropertyRows(const ElementValues& properties) {
    std::vector<PropertyRow> rows;
    rows.reserve(16);

    PropertyRow header;
    header.kind = PropertyRowKind::Header;
    header.label = "Layout Sizing";
    rows.push_back(header);

    char buffer[128];
    appendSummary(rows, "Width", formatSizeValue(properties.widthSize));
    appendSummary(rows, "Height", formatSizeValue(properties.heightSize));

    std::snprintf(buffer, sizeof(buffer), "%.0f x %.0f", properties.minLayoutWidth, properties.minLayoutHeight);
    appendSummary(rows, "Min size", buffer);

    std::snprintf(buffer, sizeof(buffer), "%.0f x %.0f", properties.maxLayoutWidth, properties.maxLayoutHeight);
    appendSummary(rows, "Max size", buffer);

    header.label = "Alignment & Flow";
    rows.push_back(header);

    std::snprintf(buffer, sizeof(buffer), "grow %.1f, shrink %.1f", properties.flexGrow, properties.flexShrink);
    appendSummary(rows, "Flex", buffer);
    appendSummary(rows, "Main align", alignName(properties.mainAlign));
    appendSummary(rows, "Cross align", alignName(properties.crossAlign));
    appendSummary(rows, "Spacing", formatNumber(properties.spacing));
    appendSummary(rows, "Ignore layout", properties.ignoreLayout ? "yes" : "no");

    header.label = "Box Model";
    rows.push_back(header);

    std::snprintf(buffer, sizeof(buffer), "%.0f, %.0f   %.0f x %.0f", properties.frame.x, properties.frame.y,
                  properties.frame.width, properties.frame.height);
    appendSummary(rows, "Frame", buffer);

    std::snprintf(buffer, sizeof(buffer), "%.0f / %.0f / %.0f / %.0f", properties.margin.left, properties.margin.top,
                  properties.margin.right, properties.margin.bottom);
    appendSummary(rows, "Margin", buffer);

    std::snprintf(buffer, sizeof(buffer), "%.0f / %.0f / %.0f / %.0f", properties.padding.left, properties.padding.top,
                  properties.padding.right, properties.padding.bottom);
    appendSummary(rows, "Padding", buffer);

    return rows;
}

std::vector<PropertyRow> buildContentPropertyRows(const ElementValues& properties) {
    std::vector<PropertyRow> rows;
    rows.reserve(16);

    PropertyRow header;
    header.kind = PropertyRowKind::Header;

    if (properties.kind == ElementKind::Text || !properties.text.empty()) {
        header.label = "Text Content";
        rows.push_back(header);

        appendSummary(rows, "Text", properties.text.empty() ? "(empty)" : properties.text);
        appendSummary(rows, "Font family", properties.fontFamily.empty() ? "(default)" : properties.fontFamily);
        appendSummary(rows, "Font size", formatNumber(properties.fontSize));
        appendSummary(rows, "Weight", std::to_string(properties.fontWeight));
        appendSummary(rows, "Line height", formatNumber(properties.lineHeight));
        appendSummary(rows, "Wrap", properties.wrap ? "yes" : "no");
        if (properties.maxWidth > 0.0f) {
            appendSummary(rows, "Max width", formatNumber(properties.maxWidth));
        }
        std::string align = std::string(horizontalAlignName(properties.horizontalAlign)) + ", " +
                            verticalAlignName(properties.verticalAlign);
        appendSummary(rows, "Align", align);
    }

    if (properties.kind == ElementKind::Image || !properties.imageSource.empty()) {
        header.label = "Image Content";
        rows.push_back(header);

        appendSummary(rows, "Source", properties.imageSource.empty() ? "(none)" : properties.imageSource);
        appendSummary(rows, "Fit", imageFitName(properties.imageFit));
    }

    if (properties.kind == ElementKind::Svg || !properties.svgSource.empty()) {
        header.label = "SVG Content";
        rows.push_back(header);

        appendSummary(rows, "SVG Source", properties.svgSource.empty() ? "(none)" : properties.svgSource);
    }

    if (rows.empty()) {
        header.label = "Content";
        rows.push_back(header);
        appendSummary(rows, "Kind", elementKindName(properties.kind));
        appendSummary(rows, "Media / Text", "none");
    }

    return rows;
}

std::vector<PropertyRow> buildBehaviorPropertyRows(const ElementValues& properties) {
    std::vector<PropertyRow> rows;
    rows.reserve(16);

    PropertyRow header;
    header.kind = PropertyRowKind::Header;
    header.label = "Interaction & State";
    rows.push_back(header);

    appendSummary(rows, "Interactive", properties.interactive ? "yes" : "no");
    appendSummary(rows, "Disabled", properties.disabled ? "yes" : "no");
    appendSummary(rows, "Focusable", properties.focusable ? "yes" : "no");
    appendSummary(rows, "Cursor", cursorShapeName(properties.cursor));

    header.label = "Event Listeners";
    rows.push_back(header);

    appendSummary(rows, "onClick", properties.hasOnClick ? "bound" : "none");
    appendSummary(rows, "onPress", properties.hasOnPress ? "bound" : "none");
    appendSummary(rows, "onRelease", properties.hasOnRelease ? "bound" : "none");
    appendSummary(rows, "onHover", properties.hasOnHoverChanged ? "bound" : "none");
    appendSummary(rows, "onFocus", properties.hasOnFocusChanged ? "bound" : "none");
    appendSummary(rows, "onScroll", properties.hasOnScroll ? "bound" : "none");
    appendSummary(rows, "onDrag", properties.hasOnDrag ? "bound" : "none");
    appendSummary(rows, "onKey", properties.hasOnKeyEvent ? "bound" : "none");

    if (properties.hasStateColors) {
        header.label = "State Colors";
        rows.push_back(header);
        appendSummary(rows, "Hover", formatHex(properties.hoverColor));
        appendSummary(rows, "Pressed", formatHex(properties.pressedColor));
    }

    return rows;
}

std::vector<PropertyRow> buildPropertyRows(const ElementValues& properties,
                                           const DevtoolsPanelState* panelState) {
    const PropertiesTab tab = panelState != nullptr ? panelState->activePropertiesTab : PropertiesTab::Visual;
    switch (tab) {
    case PropertiesTab::Visual:
        return buildVisualPropertyRows(properties, panelState);
    case PropertiesTab::Layout:
        return buildLayoutPropertyRows(properties);
    case PropertiesTab::Content:
        return buildContentPropertyRows(properties);
    case PropertiesTab::Behavior:
        return buildBehaviorPropertyRows(properties);
    }
    return buildVisualPropertyRows(properties, panelState);
}

// The way back to the element's own value. It only exists while the field is written,
// so the row says what a debug session changed by itself.
void composeRevertButton(core::dsl::Ui& ui, const std::string& id, const std::string& elementId,
                         ElementField field, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id + ".revert")
        .size(theme.propertyRevertWidth, theme.elementRowHeight)
        .icon(theme.iconRevert)
        .fontSize(theme.elementFontSize)
        .lineHeight(theme.elementRowHeight)
        .color(theme.accent)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .onClick([clear = actions.properties.clearField, elementId, field] {
            if (clear) {
                clear(elementId, field);
            }
        })
        .build();
}

// The colour slot: the caret that says whether the channels are open, then the swatch.
// Both live in the element that takes the click, because the caret is the state of the
// swatch rather than a second button beside it.
void composeColorSwatch(core::dsl::Ui& ui, const std::string& id, const std::string& elementId,
                        ElementField field, const core::Color& color, bool open,
                        const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    // The swatch box ends where the sliders and the switch tracks of the neighbouring rows
    // end, which is the control inset inside the column, and the caret sits in front of it.
    const float inset = devtoolsControlTheme().metrics.spacing.control;
    const float slotWidth =
        theme.propertyIndicatorWidth + theme.metricGap + theme.propertySwatchSize + inset;
    core::Color swatch = color;
    swatch.a = 1.0f;
    ui.stack(id + ".swatch")
        .size(slotWidth, theme.elementRowHeight)
        .content([&] {
            ui.text(id + ".swatch.indicator")
                .size(theme.propertyIndicatorWidth, theme.elementRowHeight)
                .icon(open ? theme.iconDisclosureExpanded : theme.iconDisclosureCollapsed)
                .fontSize(theme.elementFontSize)
                .lineHeight(theme.elementRowHeight)
                .color(open ? theme.primaryText : theme.mutedText)
                .horizontalAlign(core::HorizontalAlign::Center)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
            ui.rect(id + ".swatch.box")
                .size(theme.propertySwatchSize, theme.propertySwatchSize)
                .position(theme.propertyIndicatorWidth + theme.metricGap,
                          (theme.elementRowHeight - theme.propertySwatchSize) * 0.5f)
                .color(swatch)
                .radius(2.0f)
                .border(1.0f, open ? theme.accent : theme.panelBorder)
                .transition(controlTransition())
                .animate(core::AnimProperty::Color | core::AnimProperty::Border)
                .build();
        })
        .onClick([toggle = actions.properties.toggleColorEditor, field, open] {
            if (toggle) {
                toggle(field, !open);
            }
        })
        .build();
}

// The control column of a row: one fixed box on every row, with the control aligned
// inside it, so a slider that fills the column, a switch and a colour slot all start and
// end on the same two lines instead of each sitting where its own width leaves it.
template <typename ComposeControl>
void composeControlSlot(core::dsl::Ui& ui,
                        const std::string& id,
                        float width,
                        core::Align side,
                        ComposeControl&& compose) {
    ui.stack(id)
        .size(std::max(24.0f, width), devtoolsTheme().elementRowHeight)
        .align(core::Align::CENTER, side)
        .content([&] { compose(); })
        .build();
}

void composeValueText(core::dsl::Ui& ui, const std::string& id, const PropertyRow& row) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.text(id + ".value")
        .fontFamily(theme.fontFamily)
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
    // A switch draws its track inset inside its own box by the component theme's control
    // spacing, so its box is that track plus both insets. The control column aligns that
    // box, and the sliders get the same inset at both ends of the column, which is what
    // puts a switch and a slider on the same two lines.
    const float controlInset = devtoolsControlTheme().metrics.spacing.control;
    const float editorWidth = std::max(
        24.0f,
        state.panel.width - theme.propertyLabelWidth - theme.propertyValueWidth - theme.propertyRevertWidth -
            theme.elementDetailsPadding * 2.0f);
    const float switchWidth = theme.propertySwitchTrackWidth + controlInset * 2.0f;
    const float sliderWidth = std::max(24.0f, editorWidth - controlInset * 2.0f);

    ui.text(id + ".label")
        .fontFamily(theme.fontFamily)
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
        const std::function<void()> copy = row.copyable && actions.properties.copyElementId
            ? std::function<void()>([copy = actions.properties.copyElementId, elementId] { copy(elementId); })
            : std::function<void()>{};
        ui.text(id + ".value")
            .fontFamily(theme.fontFamily)
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

    // Every row from here down is the same four columns: the label, the control column,
    // the value and the way back. The control column is one fixed box that the control is
    // aligned inside, so a slider that fills it, a switch and a colour slot all end on the
    // same line instead of each sitting where its own width leaves it.
    switch (fieldKind(row.field)) {
    case FieldKind::Color: {
        if (row.kind == PropertyRowKind::Channel) {
            // A channel of the colour its row above opened: dragged as one number.
            float hue = 0.0f;
            float saturation = 0.0f;
            float value = 0.0f;
            core::Color current{1.0f, 1.0f, 1.0f, 1.0f};
            if (state.panelState != nullptr) {
                current = propertyColor(*properties.properties, state.panelState->colorEditorField);
            }
            colorToHsv(current, hue, saturation, value);
            const float channels[4] = {hue / 360.0f, saturation, value, current.a};
            const float channelValue = channels[row.channel];

            composeControlSlot(ui, id + ".control", editorWidth, core::Align::CENTER, [&] {
                components::slider(ui, id + ".slider")
                    .size(sliderWidth, theme.elementRowHeight)
                    .value(channelValue)
                    .theme(devtoolsControlTheme())
                    .onChange([set = actions.properties.setValue, elementId, field = row.field,
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
                        set(elementId, field, fieldValueOf(colorFromHsv(h, s, v, alpha)));
                    })
                    .build();
            });
            ui.text(id + ".value")
                .fontFamily(theme.fontFamily)
                .size(theme.propertyValueWidth, theme.elementRowHeight)
                .text(formatNumber(row.channel == 0 ? channelValue * 360.0f : channelValue))
                .fontSize(theme.elementRowFontSize)
                .color(theme.metricValue)
                .horizontalAlign(core::HorizontalAlign::Right)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
            break;
        }
        composeControlSlot(ui, id + ".control", editorWidth, core::Align::END, [&] {
            if (properties.properties == nullptr || state.panelState == nullptr) {
                return;
            }
            composeColorSwatch(ui, id, elementId, row.field,
                               propertyColor(*properties.properties, row.field),
                               state.panelState->colorEditorOpen &&
                                   state.panelState->colorEditorField == row.field,
                               actions);
        });
        ui.text(id + ".value")
            .fontFamily(theme.fontFamily)
            .size(theme.propertyValueWidth, theme.elementRowHeight)
            .text(row.value)
            .fontSize(theme.elementRowFontSize)
            .color(theme.mutedText)
            .horizontalAlign(core::HorizontalAlign::Right)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
        break;
    }
    case FieldKind::Flag: {
        composeControlSlot(ui, id + ".control", editorWidth, core::Align::END, [&] {
            components::toggleSwitch(ui, id + ".switch")
                .size(switchWidth, theme.elementRowHeight)
                .checked(row.value == "on")
                .trackSize(theme.propertySwitchTrackWidth, theme.propertySwitchTrackHeight)
                .theme(devtoolsControlTheme())
                .onChange([set = actions.properties.setValue, elementId, field = row.field](bool value) {
                    if (set) {
                        set(elementId, field, fieldValueOf(value));
                    }
                })
                .build();
        });
        // A switch is its own value, so nothing is printed beside it; the column is
        // composed all the same, which is what keeps the rows in the same place.
        ui.stack(id + ".valueSpacer").width(theme.propertyValueWidth).height(theme.elementRowHeight).build();
        break;
    }
    case FieldKind::Number: {
        const PropertyDescriptor* descriptor = findDescriptor(row.field);
        const float minimum = descriptor != nullptr ? descriptor->minimum : 0.0f;
        const float maximum = descriptor != nullptr ? descriptor->maximum : 1.0f;
        const float range = maximum - minimum;
        const float current = properties.properties != nullptr
            ? propertyNumber(*properties.properties, row.field)
            : 0.0f;
        composeControlSlot(ui, id + ".control", editorWidth, core::Align::CENTER, [&] {
            components::slider(ui, id + ".slider")
                .size(sliderWidth, theme.elementRowHeight)
                .value(range > 0.0f ? (current - minimum) / range : 0.0f)
                .theme(devtoolsControlTheme())
                .onChange([set = actions.properties.setValue, elementId, field = row.field, minimum,
                           range](float normalized) {
                    if (set) {
                        set(elementId, field, fieldValueOf(minimum + normalized * range));
                    }
                })
                .build();
        });
        composeValueText(ui, id, row);
        break;
    }
    }

    if (hasRevert) {
        composeRevertButton(ui, id, elementId, row.field, actions);
    } else {
        // The way back is a column of its own, so every row reserves it: without the
        // placeholder the value column would move as the pointer moves around.
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
        .fontFamily(theme.fontFamily)
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
        .fontFamily(theme.fontFamily)
        .size(theme.propertyResetWidth, theme.elementRowHeight)
        .text("Reset")
        .fontSize(theme.elementRowFontSize)
        .color(theme.accent)
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .onClick([clear = actions.properties.clearFields] {
            if (clear) {
                clear();
            }
        })
        .build();
}

void composePropertySubTab(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                           bool selected, const std::function<void()>& onClick) {
    const DevtoolsTheme& theme = devtoolsTheme();
    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(theme.elementRowHeight)
        .content([&] {
            ui.rect(id + ".background")
                .fill()
                .ignoreLayout()
                .states(theme.transparent, theme.tabHover, theme.tabHover)
                .instantStates()
                .onClick(onClick)
                .build();
            ui.row(id + ".content")
                .width(core::SizeValue::wrapContent())
                .height(theme.elementRowHeight)
                .padding(theme.tabHorizontalPadding + 2.0f, 0.0f)
                .content([&] {
                    ui.text(id + ".label")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::wrapContent())
                        .height(theme.elementRowHeight)
                        .text(label)
                        .fontSize(theme.elementRowFontSize)
                        .color(selected ? theme.primaryText : theme.mutedText)
                        .horizontalAlign(core::HorizontalAlign::Center)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
            if (selected) {
                ui.rect(id + ".indicator")
                    .width(core::SizeValue::fill())
                    .height(theme.tabIndicatorHeight)
                    .margin(2.0f, 0.0f, 2.0f, 0.0f)
                    .y(theme.elementRowHeight - theme.tabIndicatorHeight)
                    .ignoreLayout()
                    .color(theme.accent)
                    .build();
            }
        })
        .build();
}

} // namespace

const std::vector<ElementField>& elementPropertyIds() {
    static const std::vector<ElementField> ids = [] {
        std::vector<ElementField> value;
        value.reserve(propertyDescriptors().size());
        for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
            value.push_back(descriptor.field);
        }
        return value;
    }();
    return ids;
}

std::vector<ElementField> elementPropertyIds(core::dsl::ElementKind kind) {
    std::vector<ElementField> value;
    for (const PropertyDescriptor& descriptor : propertyDescriptors()) {
        if ((descriptor.kinds & elementKindBit(kind)) != 0) {
            value.push_back(descriptor.field);
        }
    }
    return value;
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
                    .fontFamily(theme.fontFamily)
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
            const float subtabHeight = theme.elementRowHeight;
            const float footerHeight = theme.elementRowHeight;
            const float listHeight = std::max(0.0f, area.height - subtabHeight - footerHeight);
            const float scrollOffset =
                state.panelState != nullptr ? state.panelState->propertiesScrollOffset : 0.0f;
            const PropertiesTab activeTab =
                state.panelState != nullptr ? state.panelState->activePropertiesTab : PropertiesTab::Visual;

            ui.stack(id + ".subtabs")
                .position(0.0f, 0.0f)
                .size(area.width, subtabHeight)
                .content([&] {
                    ui.rect(id + ".subtabs.border")
                        .width(core::SizeValue::fill())
                        .height(1.0f)
                        .y(subtabHeight - 1.0f)
                        .ignoreLayout()
                        .color(theme.panelBorder)
                        .build();
                    ui.row(id + ".subtabs.list")
                        .position(padding, 0.0f)
                        .size(contentWidth, subtabHeight)
                        .gap(4.0f)
                        .content([&] {
                            const auto select = actions.properties.selectTab;
                            composePropertySubTab(ui, id + ".subtabs.visual", "Visual",
                                                  activeTab == PropertiesTab::Visual,
                                                  [select] { if (select) select(PropertiesTab::Visual); });
                            composePropertySubTab(ui, id + ".subtabs.layout", "Layout",
                                                  activeTab == PropertiesTab::Layout,
                                                  [select] { if (select) select(PropertiesTab::Layout); });
                            composePropertySubTab(ui, id + ".subtabs.content", "Content",
                                                  activeTab == PropertiesTab::Content,
                                                  [select] { if (select) select(PropertiesTab::Content); });
                            composePropertySubTab(ui, id + ".subtabs.behavior", "Behavior",
                                                  activeTab == PropertiesTab::Behavior,
                                                  [select] { if (select) select(PropertiesTab::Behavior); });
                        })
                        .build();
                })
                .build();

            // The list spans the whole area, so its scrollbar sits on the panel edge
            // instead of a padding away from it. The rows keep the padding and their
            // width, which is what the composition reserves for the scrollbar anyway.
            //
            // Everything inside the area is placed relative to it, which is what a
            // positioned stack expects of its children.
            components::virtualList(ui, id + ".list")
                .position(0.0f, subtabHeight)
                .size(area.width, listHeight)
                .itemCount(static_cast<std::int64_t>(rows.size()))
                .rowHeight(theme.elementRowHeight)
                .offset(scrollOffset)
                .onChange(actions.properties.setScrollOffset)
                .row([&](core::dsl::Ui& rowUi, const std::string& rowId, std::int64_t index, float width,
                         float height) {
                    (void)height;
                    if (index < 0 || index >= static_cast<std::int64_t>(rows.size())) {
                        return;
                    }
                    rowUi.row(rowId + ".row")
                        .position(padding, 0.0f)
                        .size(std::max(0.0f, width - padding * 2.0f), theme.elementRowHeight)
                        .gap(theme.propertyColumnGap)
                        .content([&] {
                            composePropertyRow(rowUi, rowId, rows[static_cast<std::size_t>(index)], elementId,
                                               properties, state, actions);
                        })
                        .build();
                })
                .build();

            ui.row(id + ".footer")
                .position(padding, subtabHeight + listHeight)
                .size(contentWidth, footerHeight)
                .content([&] {
                    composePropertyFooter(ui, id + ".footer.inner", contentWidth, properties, actions);
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
