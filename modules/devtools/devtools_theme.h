#pragma once

#include "components/theme.h"
#include "core/dsl.h"

#include <filesystem>

namespace modules::devtools {

// The panel keeps its own dark palette instead of following the page theme, so
// it reads the same above a light or a dark page, like browser developer tools.
// Values are prepared once and shared by every panel composition.
struct DevtoolsTheme {
    core::Color panelBackground{0.125f, 0.145f, 0.176f, 1.0f};
    core::Color toolbarBackground{0.161f, 0.184f, 0.220f, 1.0f};
    core::Color menuBackground{0.188f, 0.216f, 0.255f, 1.0f};
    core::Color panelBorder{0.349f, 0.396f, 0.455f, 1.0f};
    core::Color dismissSurface{0.0f, 0.0f, 0.0f, 0.0f};
    core::Color accent{0.400f, 0.663f, 0.969f, 1.0f};
    core::Color sectionLabel{0.569f, 0.757f, 1.0f, 1.0f};
    core::Color metricLabel{0.667f, 0.718f, 0.776f, 1.0f};
    core::Color metricValue{0.925f, 0.953f, 0.980f, 1.0f};
    core::Color primaryText{0.863f, 0.906f, 0.961f, 1.0f};
    core::Color mutedText{0.612f, 0.663f, 0.722f, 1.0f};
    core::Color icon{0.784f, 0.835f, 0.894f, 1.0f};
    core::Color transparent{0.0f, 0.0f, 0.0f, 0.0f};
    core::Color toolbarHover{0.250f, 0.310f, 0.390f, 1.0f};
    core::Color tabHover{0.230f, 0.280f, 0.340f, 1.0f};
    core::Color menuRowHover{0.270f, 0.350f, 0.450f, 1.0f};
    core::Color menuRowSelected{0.200f, 0.340f, 0.520f, 1.0f};
    core::Color menuRowSelectedText{0.863f, 0.922f, 1.0f, 1.0f};
    core::Color menuShadow{0.0f, 0.0f, 0.0f, 0.30f};
    core::Color elementRowHover{0.208f, 0.251f, 0.310f, 1.0f};
    core::Color elementRowSelected{0.176f, 0.286f, 0.435f, 1.0f};
    core::Color elementKindText{0.549f, 0.639f, 0.749f, 1.0f};
    core::Color elementDisabledText{0.478f, 0.510f, 0.553f, 1.0f};
    core::Color detailsBackground{0.141f, 0.165f, 0.196f, 1.0f};

    float toolbarHeight = 31.0f;
    float toolbarPadding = 8.0f;
    float toolbarCompactPadding = 4.0f;
    float compactWidth = 360.0f;
    float iconButtonSize = 24.0f;
    float iconSize = 14.0f;
    float iconRadius = 5.0f;
    float menuIconSize = 15.0f;
    float tabFontSize = 16.0f;
    // Ten tabs have to fit the strip of a docked panel, so the padding stays tight.
    float tabHorizontalPadding = 8.0f;
    float tabIndicatorHeight = 2.0f;
    float menuWidth = 152.0f;
    float menuRowHeight = 26.0f;
    float menuPadding = 2.0f;
    float menuRowFontSize = 15.0f;
    float menuRowIconGap = 4.0f;
    float menuRowIconPadding = 6.0f;
    float sectionHeight = 32.0f;
    float sectionFontSize = 17.0f;
    float metricHeight = 28.0f;
    float metricFontSize = 16.0f;
    float metricValueFontSize = 24.0f;
    float captionFontSize = 13.0f;
    float metricPaddingHorizontal = 18.0f;
    float metricPaddingVertical = 16.0f;
    float metricGap = 4.0f;
    float menuShadowRadius = 14.0f;
    float menuShadowOffsetY = 5.0f;
    float indicatorInset = 4.0f;
    float elementRowHeight = 22.0f;
    float elementRowFontSize = 15.0f;
    // The row of view options above the element tree: shared with the Scale toolbar height
    // so both tab operation bars maintain an identical height across DevTools.
    float elementOptionsHeight = 34.0f;
    float elementOptionGap = 0.0f;
    float elementOptionBoxSize = iconSize;
    float elementIndent = 12.0f;
    float elementDisclosureSize = 16.0f;
    float elementKindWidth = 24.0f;
    float elementFontSize = 13.0f;
    float elementDetailsHeight = 108.0f;
    float elementDetailsPadding = 10.0f;
    float elementDetailsLabelWidth = 60.0f;
    float propertyLabelWidth = 116.0f;
    // Wide enough for a `#RRGGBB` value at elementRowFontSize, which is the longest value
    // a row prints.
    float propertyValueWidth = 70.0f;
    float propertyRevertWidth = 20.0f;
    // The caret a colour row shows while its channels are open, and the swatch beside it.
    float propertyIndicatorWidth = 16.0f;
    float propertySwatchSize = 14.0f;
    float propertyResetWidth = 52.0f;
    // The switch draws its track inside its own box: the row derives the box from the
    // track plus both insets, so both are what the component theme uses.
    float propertySwitchTrackWidth = 32.0f;
    float propertySwitchTrackHeight = 18.0f;
    float propertyColumnGap = 6.0f;
    // The property area starts at this share of the space the tab has, and the divider
    // moves it between the minimum height and what the tree can spare.
    float propertiesInitialFraction = 0.52f;
    float propertiesMinimumHeight = 66.0f;
    float propertiesMinimumTreeHeight = 66.0f;
    float propertiesHandleHeight = 6.0f;
    core::Color propertiesHandleHover{0.250f, 0.310f, 0.390f, 1.0f};

    // Font Awesome 7 Free-Solid codepoints, the icon font EUI-NEO already ships.
    unsigned int iconSelectElement = 0xF245;
    unsigned int iconDeviceViewport = 0xF108;
    unsigned int iconSettings = 0xF013;
    unsigned int iconMore = 0xF142;
    unsigned int iconClose = 0xF00D;
    unsigned int iconDockFloating = 0xF2D2;
    unsigned int iconDockLeft = 0xF060;
    unsigned int iconDockDown = 0xF063;
    unsigned int iconDockRight = 0xF061;
    // The caret a disclosure control shows: the tree rows that hide a subtree, and the
    // colour row whose channels are open.
    unsigned int iconDisclosureExpanded = 0xF0D7;
    unsigned int iconDisclosureCollapsed = 0xF0DA;
    unsigned int iconElementStack = 0xF5FD;     // fa-layer-group
    unsigned int iconElementRow = 0xF0DB;       // fa-table-columns
    unsigned int iconElementColumn = 0xF0C9;    // fa-bars
    unsigned int iconElementFlow = 0xF149;      // fa-arrow-turn-down
    unsigned int iconElementRect = 0xF0C8;      // fa-square
    unsigned int iconElementPolygon = 0xF5EE;   // fa-draw-polygon
    unsigned int iconElementText = 0xF031;      // fa-font
    unsigned int iconElementImage = 0xF03E;     // fa-image
    unsigned int iconElementSvg = 0xF55B;       // fa-bezier-curve
    unsigned int iconElementShadertoy = 0xF0D0; // fa-wand-magic-sparkles
    unsigned int iconElementLayout = 0xF0C9;
    unsigned int iconElementShape = 0xF0C8;
    unsigned int iconRevert = 0xF2EA;

    // Font family for DevTools UI text, isolated from host application styling.
    // TODO(devtools): When EUI core adds a generic "SystemUI" alias in resolveFontPath(),
    // this candidate path probe can be replaced with a single "SystemUI" identifier.
    std::string fontFamily;
};

inline const DevtoolsTheme& devtoolsTheme() {
    static const DevtoolsTheme theme = [] {
        DevtoolsTheme t;
        // Probe common system UI fonts on the host OS so DevTools remains clear and
        // immune to any custom artistic font overrides set by the user application.
#if defined(_WIN32)
        const char* const candidates[] = {
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/arial.ttf"
        };
#elif defined(__APPLE__)
        const char* const candidates[] = {
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf"
        };
#else
        const char* const candidates[] = {
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
        };
#endif
        for (const char* path : candidates) {
            std::error_code error;
            if (std::filesystem::exists(path, error)) {
                t.fontFamily = path;
                break;
            }
        }
        return t;
    }();
    return theme;
}

// Components (sliders, switches, checkboxes) carry a theme of their own, so the panel hands
// them its palette instead of the default light one.
inline const components::theme::ThemeColorTokens& devtoolsControlTheme() {
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

inline unsigned int elementKindIcon(core::dsl::ElementKind kind) {
    const DevtoolsTheme& theme = devtoolsTheme();
    switch (kind) {
    case core::dsl::ElementKind::Stack:
        return theme.iconElementStack;
    case core::dsl::ElementKind::Row:
        return theme.iconElementRow;
    case core::dsl::ElementKind::Column:
        return theme.iconElementColumn;
    case core::dsl::ElementKind::Flow:
        return theme.iconElementFlow;
    case core::dsl::ElementKind::Rect:
        return theme.iconElementRect;
    case core::dsl::ElementKind::Polygon:
        return theme.iconElementPolygon;
    case core::dsl::ElementKind::Text:
        return theme.iconElementText;
    case core::dsl::ElementKind::Image:
        return theme.iconElementImage;
    case core::dsl::ElementKind::Svg:
        return theme.iconElementSvg;
    case core::dsl::ElementKind::Shadertoy:
        return theme.iconElementShadertoy;
    }
    return theme.iconElementShape;
}

} // namespace modules::devtools
