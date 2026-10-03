#include "modules/devtools/mcp.h"

#include "modules/devtools/mcp_semantic.h"
#include "modules/devtools/theme.h"
#include "components/scrollview.h"

#include <algorithm>
#include <string>

namespace modules::devtools {

namespace {

void composeMcpActionButton(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                            bool active, const std::function<void()>& onClick) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color bg = active
        ? theme.accent
        : core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.70f};
    const core::Color border = active
        ? theme.accent
        : core::Color{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.60f};
    const core::Color textCol = active ? core::Color{1.0f, 1.0f, 1.0f, 1.0f} : theme.primaryText;

    ui.stack(id)
        .width(core::SizeValue::wrapContent())
        .height(24.0f)
        .content([&] {
            auto bgRect = ui.rect(id + ".bg");
            bgRect.fill()
                .ignoreLayout()
                .color(bg)
                .border(1.0f, border)
                .radius(3.0f)
                .states(bg, theme.elementRowHover, theme.elementRowHover);
            if (onClick) {
                bgRect.onClick(onClick);
            }
            bgRect.build();

            ui.text(id + ".lbl")
                .fontFamily(theme.fontFamily)
                .margin(10.0f, 0.0f, 10.0f, 0.0f)
                .width(core::SizeValue::wrapContent())
                .height(24.0f)
                .text(label)
                .fontSize(theme.captionFontSize)
                .fontWeight(active ? 600 : 500)
                .color(textCol)
                .verticalAlign(core::VerticalAlign::Center)
                .build();
        })
        .onClick(onClick)
        .build();
}

void composeMcpInfoRow(core::dsl::Ui& ui, const std::string& id, const std::string& label,
                       const std::string& value, const core::Color& valueColor) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.18f};

    ui.stack(id)
        .width(core::SizeValue::fill())
        .height(theme.elementRowHeight)
        .content([&] {
            ui.rect(id + ".border")
                .position(0.0f, theme.elementRowHeight - 1.0f)
                .width(core::SizeValue::fill())
                .height(1.0f)
                .ignoreLayout()
                .color(dividerColor)
                .build();

            ui.row(id + ".row")
                .fill()
                .padding(14.0f, 0.0f, 14.0f, 0.0f)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text(id + ".lbl")
                        .fontFamily(theme.fontFamily)
                        .width(180.0f)
                        .height(theme.elementRowHeight)
                        .text(label)
                        .fontSize(theme.elementRowFontSize)
                        .color(theme.metricLabel)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();

                    ui.text(id + ".val")
                        .fontFamily(theme.fontFamily)
                        .width(core::SizeValue::fill())
                        .height(theme.elementRowHeight)
                        .text(value)
                        .fontSize(theme.elementRowFontSize)
                        .fontWeight(500)
                        .color(valueColor)
                        .verticalAlign(core::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace

void composeMcpTab(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions) {
    const DevtoolsTheme& theme = devtoolsTheme();
    const float contentHeight = std::max(0.0f, state.panel.height - theme.toolbarHeight - (state.detached ? 0.0f : 1.0f));

    const bool isRunning = state.panelState != nullptr ? state.panelState->mcpServerRunning : false;
    const uint16_t port = state.panelState != nullptr ? state.panelState->mcpPort : 8990;
    const float scrollOffset = state.panelState != nullptr ? state.panelState->mcpScrollOffset : 0.0f;
    const std::string statusMsg = state.panelState != nullptr ? state.panelState->mcpStatusMessage : "MCP Bridge Standby";

    const core::Color badgeBg = isRunning
        ? core::Color{0.18f, 0.55f, 0.34f, 0.18f}
        : core::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.15f};
    const core::Color badgeBorder = isRunning
        ? core::Color{0.18f, 0.85f, 0.45f, 0.60f}
        : core::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.50f};
    const core::Color badgeText = isRunning
        ? core::Color{0.25f, 0.90f, 0.55f, 1.0f}
        : theme.accent;
    const std::string badgeLabel = isRunning ? "RUNNING" : "STANDBY";

    ui.column("mcp.main")
        .size(state.panel.width, contentHeight)
        .content([&] {
            // Header Bar
            ui.stack("mcp.header")
                .width(core::SizeValue::fill())
                .height(34.0f)
                .content([&] {
                    ui.rect("mcp.header.bg")
                        .fill()
                        .ignoreLayout()
                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.50f})
                        .build();
                    ui.rect("mcp.header.border")
                        .position(0.0f, 33.0f)
                        .width(core::SizeValue::fill())
                        .height(1.0f)
                        .ignoreLayout()
                        .color(theme.panelBorder)
                        .build();
                    ui.row("mcp.header.row")
                        .fill()
                        .padding(12.0f, 0.0f, 12.0f, 0.0f)
                        .alignItems(core::Align::CENTER)
                        .content([&] {
                            ui.text("mcp.title")
                                .fontFamily(theme.fontFamily)
                                .width(core::SizeValue::wrapContent())
                                .height(32.0f)
                                .text("MODEL CONTEXT PROTOCOL (MCP)")
                                .fontSize(theme.captionFontSize)
                                .fontWeight(700)
                                .color(theme.metricValue)
                                .verticalAlign(core::VerticalAlign::Center)
                                .build();

                            ui.rect("mcp.badge.bg")
                                .margin(10.0f, 0.0f, 0.0f, 0.0f)
                                .size(72.0f, 18.0f)
                                .color(badgeBg)
                                .radius(3.0f)
                                .border(1.0f, badgeBorder)
                                .build();

                            ui.text("mcp.badge.txt")
                                .position(-72.0f, 0.0f)
                                .width(72.0f)
                                .height(18.0f)
                                .text(badgeLabel)
                                .fontSize(theme.captionFontSize - 1.0f)
                                .fontWeight(600)
                                .color(badgeText)
                                .horizontalAlign(core::HorizontalAlign::Center)
                                .verticalAlign(core::VerticalAlign::Center)
                                .build();

                            ui.stack("mcp.header.spacer")
                                .width(core::SizeValue::fill())
                                .height(1.0f)
                                .build();

                            // Control buttons in header
                            const auto toggleServer = actions.mcp.setServerRunning;
                            if (isRunning) {
                                composeMcpActionButton(ui, "mcp.btn.stop", "Stop Server", false,
                                    [toggleServer] { if (toggleServer) toggleServer(false); });
                            } else {
                                composeMcpActionButton(ui, "mcp.btn.start", "Start Server", true,
                                    [toggleServer] { if (toggleServer) toggleServer(true); });
                            }
                        })
                        .build();
                })
                .build();

            // Workbench Scrollable Content
            const float bodyHeight = std::max(0.0f, contentHeight - 34.0f);
            components::scrollView(ui, "mcp.scroll")
                .size(state.panel.width, bodyHeight)
                .offset(scrollOffset)
                .scrollbarGap(0.0f)
                .onChange(actions.mcp.setScrollOffset)
                .content([&](core::dsl::Ui& contentUi, float contentWidth, float) {
                    contentUi.column("mcp.content")
                        .width(contentWidth)
                        .height(core::SizeValue::wrapContent())
                        .content([&] {
                            // Section 1: Server Configuration & Status
                            contentUi.stack("mcp.sec1.hdr")
                                .width(core::SizeValue::fill())
                                .height(28.0f)
                                .content([&] {
                                    contentUi.rect("mcp.sec1.hdr.bg")
                                        .fill()
                                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.35f})
                                        .build();
                                    contentUi.text("mcp.sec1.hdr.txt")
                                        .margin(14.0f, 0.0f, 0.0f, 0.0f)
                                        .width(core::SizeValue::fill())
                                        .height(28.0f)
                                        .text("BRIDGE STATUS & CONFIGURATION")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(700)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                })
                                .build();

                            composeMcpInfoRow(contentUi, "mcp.info.status", "Service Status",
                                              isRunning ? "Active (Listening for AI Agent commands)" : "Standby (Local engine ready)",
                                              isRunning ? core::Color{0.25f, 0.90f, 0.55f, 1.0f} : theme.metricValue);

                            composeMcpInfoRow(contentUi, "mcp.info.port", "Listening Port",
                                              std::to_string(port) + " (HTTP / SSE / JSON-RPC)",
                                              theme.accent);

                            composeMcpInfoRow(contentUi, "mcp.info.log", "Recent Message",
                                              statusMsg,
                                              theme.primaryText);

                            // Section 2: Agent Tools Test & Verification
                            contentUi.stack("mcp.sec2.hdr")
                                .margin(0.0f, 8.0f, 0.0f, 0.0f)
                                .width(core::SizeValue::fill())
                                .height(28.0f)
                                .content([&] {
                                    contentUi.rect("mcp.sec2.hdr.bg")
                                        .fill()
                                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.35f})
                                        .build();
                                    contentUi.text("mcp.sec2.hdr.txt")
                                        .margin(14.0f, 0.0f, 0.0f, 0.0f)
                                        .width(core::SizeValue::fill())
                                        .height(28.0f)
                                        .text("AGENT PROTOCOL TOOLS & ENGINE VERIFICATION")
                                        .fontSize(theme.captionFontSize)
                                        .fontWeight(700)
                                        .color(theme.metricLabel)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();
                                })
                                .build();

                            // Action Buttons Row
                            contentUi.row("mcp.actions.row")
                                .width(core::SizeValue::fill())
                                .height(40.0f)
                                .padding(14.0f, 8.0f, 14.0f, 8.0f)
                                .gap(10.0f)
                                .alignItems(core::Align::CENTER)
                                .content([&] {
                                    composeMcpActionButton(contentUi, "mcp.tool.capture", "Test Viewport PNG Capture", false,
                                        actions.mcp.triggerTestCapture);

                                    composeMcpActionButton(contentUi, "mcp.tool.semantic", "Extract SoM Semantic Marks", false,
                                        actions.mcp.triggerTestSemantic);
                                })
                                .build();

                            // Section 3: Supported Capabilities Summary
                            composeMcpInfoRow(contentUi, "mcp.cap.tree", "tool: extract_element_tree",
                                              "Recursive tree snapshot of live elements with bounding boxes", theme.metricValue);
                            composeMcpInfoRow(contentUi, "mcp.cap.details", "tool: get_element_details",
                                              "Detailed property values & inspection attributes for element ID", theme.metricValue);
                            composeMcpInfoRow(contentUi, "mcp.cap.click", "tool: click_element / click_mark",
                                              "Direct callback trigger & pointer move/press/release automation", theme.metricValue);
                            composeMcpInfoRow(contentUi, "mcp.cap.input", "tool: input_text",
                                              "Text simulation and active element text injection", theme.metricValue);
                            composeMcpInfoRow(contentUi, "mcp.cap.vision", "tool: capture_viewport / capture_element",
                                              "Hardware framebuffer readback with PNG Base64 compression", theme.metricValue);
                            composeMcpInfoRow(contentUi, "mcp.cap.som", "tool: get_interactive_marks",
                                              "Set-of-Mark visual/semantic interaction index numbering", theme.metricValue);
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
