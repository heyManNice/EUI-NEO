#include "eui_neo.h"

#include "modules/devtools/devtools.h"

namespace app {
namespace {

// Keeps the panel attached for the lifetime of the process. The session has to
// outlive app::shutdown(), which a file scope object does.
const modules::devtools::Session devtoolsSession;

const char* const kHint =
    "F12 toggles the panel. Drag the panel edge to resize it, and use the more "
    "menu to move it to another edge or into its own window.";

void composeCard(eui::Ui& ui, const std::string& id, const std::string& title, const std::string& body) {
    ui.column(id)
        .width(core::SizeValue::fill())
        .height(core::SizeValue::wrapContent())
        .padding(18.0f)
        .gap(8.0f)
        .content([&] {
            ui.text(id + ".title")
                .width(core::SizeValue::fill())
                .height(28.0f)
                .text(title)
                .fontSize(18.0f)
                .build();
            ui.text(id + ".body")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .text(body)
                .fontSize(14.0f)
                .lineHeight(20.0f)
                .build();
        })
        .build();
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("DevTools Viewer")
        .pageId("devtools_viewer")
        .clearColor({0.086f, 0.098f, 0.118f, 1.0f})
        .windowSize(1100, 720);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.column("root")
        .size(screen.width, screen.height)
        .padding(24.0f)
        .gap(16.0f)
        .content([&] {
            ui.text("title")
                .width(core::SizeValue::fill())
                .height(40.0f)
                .text("DevTools panel")
                .fontSize(28.0f)
                .build();
            ui.text("hint")
                .width(core::SizeValue::fill())
                .height(core::SizeValue::wrapContent())
                .text(kHint)
                .fontSize(15.0f)
                .lineHeight(22.0f)
                .build();
            composeCard(ui, "card.performance", "Performance tab",
                        "Reports the FPS, process CPU/GPU, render duration and the Runtime render "
                        "statistics of the application that hosts the panel.");
            composeCard(ui, "card.docking", "Docking",
                        "The panel can dock to the left, bottom or right edge, or live in its own "
                        "window. The page keeps the remaining area and is clipped to it.");
        })
        .build();
}

} // namespace app
