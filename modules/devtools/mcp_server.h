#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

} // namespace modules::devtools
