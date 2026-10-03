#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#if defined(EUI_TOOLING)

#include "modules/devtools/mcp_action.h"
#include "modules/devtools/mcp_semantic.h"
#include "modules/devtools/mcp_server.h"
#include "modules/devtools/mcp_vision.h"

int main() {
    std::cout << "[TEST] Running devtools_mcp unit tests..." << std::endl;

    // 1. Test CLI Launch Options Parsing
    {
        const char* argv[] = {"app.exe", "--mcp-server", "--mcp-port=9999", "--devtools-tab=mcp"};
        modules::devtools::McpLaunchOptions options = modules::devtools::parseMcpCommandLine(4, argv);
        assert(options.enableMcpServer == true);
        assert(options.enableDevtools == true);
        assert(options.mcpPort == 9999);
        assert(options.initialTab == "mcp");
        std::cout << "[PASS] CLI launch options parsed successfully" << std::endl;
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
            }
        }
        assert(foundBtn);

        std::string jsonMarks = modules::devtools::formatInteractiveElementsJson(marks);
        assert(jsonMarks.find("test_page.btn_click") != std::string::npos);
        assert(jsonMarks.find("markIndex") != std::string::npos);
        std::cout << "[PASS] Interactive elements and SoM marks extraction passed" << std::endl;
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

    std::cout << "[ALL PASSED] DevTools MCP unit tests finished successfully!" << std::endl;
    return 0;
}

#else

int main() {
    return 0;
}

#endif
