#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#if defined(EUI_TOOLING)

#include "modules/devtools/mcp.h"
#include "modules/devtools/mcp_action.h"
#include "modules/devtools/mcp_semantic.h"
#include "modules/devtools/mcp_server.h"
#include "modules/devtools/mcp_vision.h"

int main() {
    std::cout << "[TEST] Running devtools_mcp unit tests..." << std::endl;

    // 1. Test CLI Launch Options Parsing
    {
        const char* argv[] = {"app.exe", "--mcp-server", "--devtools", "--mcp-port=9999", "--devtools-tab=mcp"};
        modules::devtools::McpLaunchOptions options = modules::devtools::parseMcpCommandLine(5, argv);
        assert(options.enableMcpServer == true);
        assert(options.enableDevtools == true);
        assert(options.mcpPort == 9999);
        assert(options.initialTab == "mcp");

        // Verify headless MCP server mode (without --devtools) keeps devtools UI closed
        const char* headlessArgv[] = {"app.exe", "--mcp-server", "--mcp-port=8888"};
        modules::devtools::McpLaunchOptions headlessOpts = modules::devtools::parseMcpCommandLine(3, headlessArgv);
        assert(headlessOpts.enableMcpServer == true);
        assert(headlessOpts.enableDevtools == false);
        assert(headlessOpts.mcpPort == 8888);
        std::cout << "[PASS] CLI launch options parsed successfully (including headless MCP mode)" << std::endl;
    }

    // 2. Test DSL Runtime compose & Semantic Tree Extraction
    core::dsl::Runtime runtime;
    runtime.compose("test_page", 800.0f, 600.0f, [](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        ui.column("root")
            .size(screen.width, screen.height)
            .content([&] {
                ui.text("title_txt")
                    .text("Hello MCP")
                    .size(200.0f, 30.0f)
                    .build();

                ui.rect("btn_click")
                    .size(100.0f, 40.0f)
                    .interactive(true)
                    .build();

                ui.rect("btn_disabled")
                    .size(100.0f, 40.0f)
                    .interactive(true)
                    .disabled(true)
                    .build();
            })
            .build();
    });

    // Extract interactive elements (SoM marks)
    {
        auto marks = modules::devtools::extractInteractiveElements(runtime, false);
        assert(!marks.empty());
        bool foundBtn = false;
        for (const auto& item : marks) {
            if (item.id == "test_page.btn_click") {
                foundBtn = true;
                assert(!item.disabled);
                assert(item.markIndex >= 1);
                assert(item.nearestText == "Hello MCP");
            }
        }
        assert(foundBtn);

        std::string jsonMarks = modules::devtools::formatInteractiveElementsJson(marks);
        assert(jsonMarks.find("test_page.btn_click") != std::string::npos);
        assert(jsonMarks.find("markIndex") != std::string::npos);
        assert(jsonMarks.find("contextText") != std::string::npos);
        assert(jsonMarks.find("nearestText") != std::string::npos);
        assert(jsonMarks.find("Hello MCP") != std::string::npos);
        std::cout << "[PASS] Interactive elements, semantic context, and SoM marks extraction passed" << std::endl;
    }

    // Extract element tree JSON
    {
        std::string treeJson = modules::devtools::extractElementTreeJson(runtime, 10);
        assert(treeJson.find("\"roots\"") != std::string::npos);
        assert(treeJson.find("title_txt") != std::string::npos);
        assert(treeJson.find("Hello MCP") != std::string::npos);
        std::cout << "[PASS] Full element hierarchy JSON extraction passed" << std::endl;
    }

    // Extract element details JSON
    {
        std::string detailsJson = modules::devtools::extractElementDetailsJson(runtime, "test_page.btn_click");
        assert(detailsJson.find("\"id\":\"test_page.btn_click\"") != std::string::npos);
        assert(detailsJson.find("\"interactive\":true") != std::string::npos);
        assert(detailsJson.find("\"fields\"") != std::string::npos);
        std::cout << "[PASS] Element details JSON extraction passed" << std::endl;
    }

    // 3. Test McpAction dispatch
    {
        // Click element
        auto clickRes = modules::devtools::clickElement(runtime, "test_page.btn_click");
        assert(clickRes.success == true);
        assert(clickRes.targetBounds.width == 100.0f);
        assert(clickRes.targetBounds.height == 40.0f);

        // Click non-existent
        auto failClick = modules::devtools::clickElement(runtime, "missing_elem");
        assert(failClick.success == false);

        // Input text into text element
        auto textRes = modules::devtools::inputText(runtime, "test_page.title_txt", "Updated MCP Text");
        assert(textRes.success == true);
        core::dsl::Element* el = runtime.findElement("test_page.title_txt");
        assert(el != nullptr && el->text == "Updated MCP Text");

        // Scroll dispatch
        auto scrollRes = modules::devtools::scrollElement(runtime, "test_page.root", 0.0f, -50.0f);
        assert(scrollRes.success == true);
        std::cout << "[PASS] MCP actions (click, text input, scroll) passed" << std::endl;
    }

    // 4. Test MCP Vision PNG & Base64 Encoding
    {
        // 2x2 test RGBA image
        const std::vector<uint8_t> rgba = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 0, 255
        };
        std::vector<uint8_t> pngBytes = modules::devtools::encodeRgbaToPng(rgba.data(), 2, 2);
        assert(!pngBytes.empty());
        // PNG header signature check
        assert(pngBytes.size() > 8);
        assert(pngBytes[0] == 0x89 && pngBytes[1] == 'P' && pngBytes[2] == 'N' && pngBytes[3] == 'G');

        std::string base64 = modules::devtools::encodeBase64(pngBytes);
        assert(!base64.empty());

        modules::devtools::FramebufferImage fbImg;
        fbImg.width = 2;
        fbImg.height = 2;
        fbImg.rgba = rgba;
        std::string dataUri = modules::devtools::encodePngDataUri(fbImg);
        assert(dataUri.rfind("data:image/png;base64,", 0) == 0);
        std::cout << "[PASS] MCP vision memory PNG encoding and Base64 format passed" << std::endl;
    }

    // 5. Test MCP JSON-RPC Protocol Handler
    {
        // Ping
        std::string pingReq = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";
        std::string pingResp = modules::devtools::handleMcpJsonRpcRequest(pingReq, &runtime);
        assert(pingResp.find("\"result\":{}") != std::string::npos);

        // Initialize
        std::string initReq = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\",\"params\":{}}";
        std::string initResp = modules::devtools::handleMcpJsonRpcRequest(initReq, &runtime);
        assert(initResp.find("\"serverInfo\"") != std::string::npos);
        assert(initResp.find("eui-mcp-server") != std::string::npos);

        // tools/list
        std::string listReq = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}";
        std::string listResp = modules::devtools::handleMcpJsonRpcRequest(listReq, &runtime);
        assert(listResp.find("\"tools\"") != std::string::npos);
        assert(listResp.find("extract_element_tree") != std::string::npos);
        assert(listResp.find("get_element_details") != std::string::npos);
        assert(listResp.find("get_interactive_marks") != std::string::npos);
        assert(listResp.find("click_element") != std::string::npos);
        assert(listResp.find("input_text") != std::string::npos);
        assert(listResp.find("modify_element_property") != std::string::npos);

        // tools/call: extract_element_tree
        std::string callTreeReq = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"extract_element_tree\",\"arguments\":{}}}";
        std::string callTreeResp = modules::devtools::handleMcpJsonRpcRequest(callTreeReq, &runtime);
        assert(callTreeResp.find("\"content\"") != std::string::npos);
        assert(callTreeResp.find("roots") != std::string::npos);

        // tools/call: click_element
        std::string callClickReq = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"click_element\",\"arguments\":{\"elementId\":\"test_page.btn_click\"}}}";
        std::string callClickResp = modules::devtools::handleMcpJsonRpcRequest(callClickReq, &runtime);
        assert(callClickResp.find("success") != std::string::npos && callClickResp.find("true") != std::string::npos);

        // tools/call: input_text with Unicode \uXXXX escape (e.g. \u4e1c\u4eac for "东京")
        std::string callUnicodeInput = "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"input_text\",\"arguments\":{\"target\":\"test_page.title_txt\",\"text\":\"\\u4e1c\\u4eac\"}}}";
        std::string callUnicodeResp = modules::devtools::handleMcpJsonRpcRequest(callUnicodeInput, &runtime);
        assert(callUnicodeResp.find("success") != std::string::npos && callUnicodeResp.find("true") != std::string::npos);
        core::dsl::Element* el = runtime.findElement("test_page.title_txt");
        assert(el != nullptr && el->text == "东京");

        // tools/call: click_element with unquoted numeric target (e.g. "target": 1)
        std::string callNumericTarget = "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"click_element\",\"arguments\":{\"target\":1}}}";
        std::string callNumericResp = modules::devtools::handleMcpJsonRpcRequest(callNumericTarget, &runtime);
        assert(callNumericResp.find("success") != std::string::npos && callNumericResp.find("true") != std::string::npos);

        // tools/call: press_key without target fallback to focused element
        modules::devtools::focusElement(runtime, "test_page.btn_click");
        std::string callPressKey = "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"press_key\",\"arguments\":{\"key\":\"Enter\"}}}";
        std::string callPressResp = modules::devtools::handleMcpJsonRpcRequest(callPressKey, &runtime);
        assert(callPressResp.find("success") != std::string::npos && callPressResp.find("true") != std::string::npos);

        // Verify takeSnapshot interactiveOnly filtering
        std::string fullSnap = modules::devtools::takeSnapshot(runtime, false);
        std::string interactiveSnap = modules::devtools::takeSnapshot(runtime, true);
        assert(fullSnap.find("- text \"东京\"") != std::string::npos);
        assert(interactiveSnap.find("- text \"东京\"") == std::string::npos);

        std::cout << "[PASS] MCP JSON-RPC protocol methods (ping, init, tools/list, unicode \\uXXXX, numeric handle, press_key focus) passed" << std::endl;
    }

    // 6. Test MCP Server Start & Stop Lifecycle
    {
        bool started = modules::devtools::startMcpServer(18991);
        assert(started);
        assert(modules::devtools::isMcpServerRunning());
        assert(modules::devtools::currentMcpServerPort() == 18991);
        modules::devtools::stopMcpServer();
        assert(!modules::devtools::isMcpServerRunning());
        std::cout << "[PASS] MCP Server socket lifecycle (start, port check, stop) passed" << std::endl;
    }

    // 7. Test MCP Request Logs & Clear Logs Action
    {
        auto logs = modules::devtools::getMcpRequestLogs();
        assert(!logs.empty());
        modules::devtools::clearMcpRequestLogs();
        assert(modules::devtools::getMcpRequestLogs().empty());
        std::cout << "[PASS] MCP Request logs recording and clear passed" << std::endl;
    }

    // 8. Test MCP Clear Logs button callback trigger
    {
        core::dsl::Runtime panelRuntime;
        bool statusMsgSet = false;
        panelRuntime.compose("panel", 800.0f, 600.0f, [&statusMsgSet](core::dsl::Ui& ui, const core::dsl::Screen&) {
            modules::devtools::DevtoolsUiState uiState;
            uiState.panel.width = 800.0f;
            uiState.panel.height = 600.0f;
            modules::devtools::DevtoolsPanelState pState;
            uiState.panelState = &pState;

            modules::devtools::DevtoolsUiActions uiActions;
            uiActions.mcp.setStatusMessage = [&statusMsgSet](const std::string&) {
                statusMsgSet = true;
            };

            modules::devtools::composeMcpTab(ui, uiState, uiActions);
        });

        // Trigger onClick on the Clear Logs button directly
        core::dsl::Element* clearBtn = panelRuntime.findElement("panel.mcp.btn.clear_logs.bg");
        assert(clearBtn != nullptr);
        assert(static_cast<bool>(clearBtn->onClick));
        clearBtn->onClick();
        assert(statusMsgSet == true);
        std::cout << "[PASS] Clear logs button callback safely executed without SIGSEGV" << std::endl;
    }

    std::cout << "[ALL PASSED] DevTools MCP unit tests finished successfully!" << std::endl;
    return 0;
}

#else

int main() {
    return 0;
}

#endif
