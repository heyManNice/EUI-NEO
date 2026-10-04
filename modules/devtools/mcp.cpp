#include "modules/devtools/mcp.h"

#include "modules/devtools/host.h"
#include "modules/devtools/mcp_semantic.h"
#include "modules/devtools/mcp_server.h"
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
                                        .fontFamily(theme.fontFamily)
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

                            // Section 2: Incoming Request & Event Logs
                            contentUi.stack("mcp.sec2.hdr")
                                .margin(0.0f, 8.0f, 0.0f, 0.0f)
                                .width(core::SizeValue::fill())
                                .height(30.0f)
                                .content([&] {
                                    contentUi.rect("mcp.sec2.hdr.bg")
                                        .fill()
                                        .color(core::Color{theme.toolbarBackground.r, theme.toolbarBackground.g, theme.toolbarBackground.b, 0.35f})
                                        .build();

                                    contentUi.row("mcp.sec2.hdr.row")
                                        .fill()
                                        .padding(14.0f, 0.0f, 14.0f, 0.0f)
                                        .alignItems(core::Align::CENTER)
                                        .content([&] {
                                            contentUi.text("mcp.sec2.hdr.txt")
                                                .fontFamily(theme.fontFamily)
                                                .width(core::SizeValue::wrapContent())
                                                .height(28.0f)
                                                .text("MCP REQUEST LOGS")
                                                .fontSize(theme.captionFontSize)
                                                .fontWeight(700)
                                                .color(theme.metricLabel)
                                                .verticalAlign(core::VerticalAlign::Center)
                                                .build();

                                            contentUi.stack("mcp.sec2.spacer")
                                                .width(core::SizeValue::fill())
                                                .height(1.0f)
                                                .build();

                                            const auto setStatusMsg = actions.mcp.setStatusMessage;
                                            composeMcpActionButton(contentUi, "mcp.btn.clear_logs", "Clear Logs", false,
                                                [setStatusMsg] {
                                                    clearMcpRequestLogs();
                                                    if (setStatusMsg) {
                                                        setStatusMsg("Request logs cleared");
                                                    }
#if defined(EUI_TOOLING)
                                                    devtoolsHostInstance().requestCompose();
#endif
                                                });
                                        })
                                        .build();
                                })
                                .build();

                            // Request logs display
                            const auto logs = getMcpRequestLogs();
                            if (logs.empty()) {
                                contentUi.stack("mcp.logs.empty")
                                    .width(core::SizeValue::fill())
                                    .height(38.0f)
                                    .content([&] {
                                        contentUi.text("mcp.logs.empty.txt")
                                            .fontFamily(theme.fontFamily)
                                            .margin(14.0f, 0.0f, 14.0f, 0.0f)
                                            .width(core::SizeValue::fill())
                                            .height(38.0f)
                                            .text("No requests received yet. Waiting for MCP clients on port " + std::to_string(port) + "...")
                                            .fontSize(theme.elementRowFontSize)
                                            .color(theme.mutedText)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    })
                                    .build();
                            } else {
                                const core::Color dividerColor{theme.panelBorder.r, theme.panelBorder.g, theme.panelBorder.b, 0.18f};
                                const core::Color errorRed{0.95f, 0.35f, 0.35f, 1.0f};

                                // Show newest logs first
                                for (int i = static_cast<int>(logs.size()) - 1; i >= 0; --i) {
                                    const auto& item = logs[i];
                                    const std::string rowId = "mcp.log.row." + std::to_string(i);

                                    contentUi.stack(rowId)
                                        .width(core::SizeValue::fill())
                                        .height(theme.elementRowHeight + 2.0f)
                                        .content([&] {
                                            contentUi.rect(rowId + ".border")
                                                .position(0.0f, theme.elementRowHeight + 1.0f)
                                                .width(core::SizeValue::fill())
                                                .height(1.0f)
                                                .ignoreLayout()
                                                .color(dividerColor)
                                                .build();

                                            contentUi.row(rowId + ".content")
                                                .fill()
                                                .padding(14.0f, 0.0f, 14.0f, 0.0f)
                                                .alignItems(core::Align::CENTER)
                                                .content([&] {
                                                    // 1. Timestamp
                                                    contentUi.text(rowId + ".time")
                                                        .fontFamily(theme.fontFamily)
                                                        .width(72.0f)
                                                        .height(theme.elementRowHeight)
                                                        .text("[" + item.timestamp + "]")
                                                        .fontSize(theme.elementRowFontSize - 1.0f)
                                                        .color(theme.mutedText)
                                                        .verticalAlign(core::VerticalAlign::Center)
                                                        .build();

                                                    // 2. Method / Tool
                                                    contentUi.text(rowId + ".method")
                                                        .fontFamily(theme.fontFamily)
                                                        .width(220.0f)
                                                        .height(theme.elementRowHeight)
                                                        .text(item.method)
                                                        .fontSize(theme.elementRowFontSize)
                                                        .fontWeight(600)
                                                        .color(item.isError ? errorRed : theme.accent)
                                                        .verticalAlign(core::VerticalAlign::Center)
                                                        .build();

                                                    // 3. Details
                                                    contentUi.text(rowId + ".details")
                                                        .fontFamily(theme.fontFamily)
                                                        .width(core::SizeValue::fill())
                                                        .height(theme.elementRowHeight)
                                                        .text(item.details)
                                                        .fontSize(theme.elementRowFontSize)
                                                        .color(theme.primaryText)
                                                        .verticalAlign(core::VerticalAlign::Center)
                                                        .build();
                                                })
                                                .build();
                                        })
                                        .build();
                                }
                            }
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace modules::devtools
