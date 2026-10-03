#include "modules/devtools/mcp_server.h"

#if defined(EUI_TOOLING)
#include "modules/devtools/host.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace modules::devtools {

namespace {

void parseArgument(const std::string& arg, McpLaunchOptions& options) {
    if (arg == "--devtools") {
        options.enableDevtools = true;
    } else if (arg == "--mcp-server") {
        options.enableMcpServer = true;
        options.enableDevtools = true; // MCP server mode automatically opens devtools workbench
    } else if (arg.rfind("--mcp-port=", 0) == 0) {
        std::string val = arg.substr(11);
        int p = std::atoi(val.c_str());
        if (p > 0 && p <= 65535) {
            options.mcpPort = static_cast<uint16_t>(p);
        }
    } else if (arg.rfind("--devtools-tab=", 0) == 0) {
        options.initialTab = arg.substr(15);
    }
}

} // namespace

McpLaunchOptions parseMcpCommandLine(int argc, const char* const* argv) {
    McpLaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr) {
            parseArgument(argv[i], options);
        }
    }
    return options;
}

McpLaunchOptions parseCurrentProcessCommandLine() {
    McpLaunchOptions options;
#if defined(_WIN32)
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvW != nullptr) {
        for (int i = 1; i < argc; ++i) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, NULL, 0, NULL, NULL);
            std::string str(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, &str[0], size_needed, NULL, NULL);
            if (!str.empty() && str.back() == '\0') {
                str.pop_back();
            }
            parseArgument(str, options);
        }
        LocalFree(argvW);
    }
#endif
    return options;
}

void applyMcpLaunchOptions(const McpLaunchOptions& options) {
#if defined(EUI_TOOLING)
    if (!options.enableDevtools && !options.enableMcpServer) {
        return;
    }

    DevtoolsHost& host = devtoolsHostInstance();
    DevtoolsTab tab = DevtoolsTab::Mcp;
    if (options.initialTab == "elements") {
        tab = DevtoolsTab::Elements;
    } else if (options.initialTab == "performance") {
        tab = DevtoolsTab::Performance;
    } else if (options.initialTab == "scale") {
        tab = DevtoolsTab::Scale;
    } else if (options.initialTab == "state") {
        tab = DevtoolsTab::State;
    } else if (options.initialTab == "input") {
        tab = DevtoolsTab::Input;
    } else {
        tab = DevtoolsTab::Mcp;
    }

    host.openDevtools(tab);
#else
    (void)options;
#endif
}

} // namespace modules::devtools
