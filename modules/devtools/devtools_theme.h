#pragma once

#include "core/dsl.h"

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
    float iconSize = 13.0f;
    float iconRadius = 5.0f;
    float menuIconSize = 14.0f;
    float tabFontSize = 14.0f;
    float tabHorizontalPadding = 14.0f;
    float tabIndicatorHeight = 2.0f;
    float menuWidth = 136.0f;
    float menuRowHeight = 23.0f;
    float menuPadding = 2.0f;
    float menuRowFontSize = 13.0f;
    float menuRowIconGap = 3.0f;
    float menuRowIconPadding = 5.0f;
    float sectionHeight = 28.0f;
    float sectionFontSize = 15.0f;
    float metricHeight = 24.0f;
    float metricFontSize = 14.0f;
    float metricPaddingHorizontal = 18.0f;
    float metricPaddingVertical = 16.0f;
    float metricGap = 4.0f;
    float menuShadowRadius = 14.0f;
    float menuShadowOffsetY = 5.0f;
    float indicatorInset = 4.0f;
    float elementRowHeight = 22.0f;
    float elementRowFontSize = 13.0f;
    float elementIndent = 12.0f;
    float elementDisclosureSize = 16.0f;
    float elementKindWidth = 22.0f;
    float elementFontSize = 10.0f;
    float elementDetailsHeight = 98.0f;
    float elementDetailsPadding = 10.0f;
    float elementDetailsLabelWidth = 56.0f;
    float propertyLabelWidth = 78.0f;
    float propertyValueWidth = 46.0f;
    float propertyRevertWidth = 20.0f;
    float propertySwatchWidth = 24.0f;
    float propertySwatchSize = 14.0f;
    float propertyResetWidth = 52.0f;
    float propertySwitchWidth = 40.0f;
    float propertySwitchTrackWidth = 32.0f;
    float propertySwitchTrackHeight = 18.0f;
    float propertyColumnGap = 6.0f;
    float propertiesHeight = 196.0f;

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
    unsigned int iconElementExpanded = 0xF0D7;
    unsigned int iconElementCollapsed = 0xF0DA;
    unsigned int iconElementLayout = 0xF0C9;
    unsigned int iconElementShape = 0xF0C8;
    unsigned int iconElementText = 0xF031;
    unsigned int iconElementImage = 0xF03E;
    unsigned int iconElementSvg = 0xF1C5;
    unsigned int iconElementShadertoy = 0xF085;
    unsigned int iconRevert = 0xF2EA;
};

inline const DevtoolsTheme& devtoolsTheme() {
    static const DevtoolsTheme theme;
    return theme;
}

} // namespace modules::devtools
