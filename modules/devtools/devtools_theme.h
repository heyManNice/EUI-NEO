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
};

inline const DevtoolsTheme& devtoolsTheme() {
    static const DevtoolsTheme theme;
    return theme;
}

} // namespace modules::devtools
