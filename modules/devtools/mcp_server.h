#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core::dsl {
class Runtime;
}

namespace modules::devtools {

struct McpLaunchOptions {
    bool enableDevtools = false;
    bool enableMcpServer = false;
    uint16_t mcpPort = 8990;
    std::string initialTab = "mcp";
};

// Parses command-line arguments (e.g. from GetCommandLineW or argc/argv)
// Supported flags:
//   --devtools                  Open DevTools automatically on start
//   --mcp-server                Enable MCP server
//   --mcp-port=<port>           Set MCP server listening port (default 8990)
//   --devtools-tab=<elements|performance|scale|state|input|mcp>
McpLaunchOptions parseMcpCommandLine(int argc, const char* const* argv);

// Parses command line on Windows directly from system command line string
McpLaunchOptions parseCurrentProcessCommandLine();

// Applies launch options to the DevTools host session
void applyMcpLaunchOptions(const McpLaunchOptions& options);

// Server lifecycle management
bool startMcpServer(uint16_t port = 8990);
void stopMcpServer();
bool isMcpServerRunning();
uint16_t currentMcpServerPort();

// Direct MCP JSON-RPC protocol request execution
// Handles methods:
//   - "tools/list"
//   - "tools/call" (get_element_tree, get_element_details, get_interactive_marks,
//                   click_element, click_mark, input_text, scroll, capture_viewport,
//                   capture_element, modify_property)
std::string handleMcpJsonRpcRequest(const std::string& requestJson, core::dsl::Runtime* explicitRuntime = nullptr);

} // namespace modules::devtools
