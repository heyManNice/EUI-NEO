#include "modules/devtools/mcp_server.h"

#if defined(EUI_TOOLING)
#include "modules/devtools/host.h"
#include "modules/devtools/mcp_action.h"
#include "modules/devtools/mcp_semantic.h"
#include "modules/devtools/mcp_vision.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace modules::devtools {

namespace {

std::mutex s_logsMutex;
std::vector<McpRequestLogEntry> s_requestLogs;
constexpr std::size_t kMaxLogs = 100;
std::atomic<uint64_t> s_uiRevision{1};

// Resolves element target: supports raw element ID, Set-of-Mark handle (e.g. "e5", "#e5", "5"), or standard mark index
std::string resolveTarget(core::dsl::Runtime* rt, const std::string& target) {
    if (target.empty() || rt == nullptr) return target;

    // Check if target matches e<N> or #e<N> or pure digits <N>
    std::string handle = target;
    if (!handle.empty() && handle[0] == '#') handle = handle.substr(1);
    if (!handle.empty() && (handle[0] == 'e' || handle[0] == 'E')) handle = handle.substr(1);

    bool allDigits = !handle.empty();
    for (char c : handle) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            allDigits = false;
            break;
        }
    }

    if (allDigits) {
        int idx = std::atoi(handle.c_str());
        if (idx > 0) {
            auto marks = extractInteractiveElements(*rt, true);
            for (const auto& m : marks) {
                if (m.markIndex == idx) {
                    return m.id;
                }
            }
        }
    }

    return target;
}

void syncUiFrameAfterAction(core::dsl::Runtime* rt) {
    s_uiRevision.fetch_add(1, std::memory_order_relaxed);
    if (rt != nullptr) {
        rt->requestElementRefresh();
        rt->requestFullPaint();
    }
#if defined(EUI_TOOLING)
    devtoolsHostInstance().requestCompose();
#endif
    // Wait briefly (~35ms) to give the render thread a complete frame cycle to compose and draw
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

void appendRequestLog(const std::string& method, const std::string& details, bool isError = false) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif
    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tmBuf);

    std::lock_guard<std::mutex> lock(s_logsMutex);
    McpRequestLogEntry entry;
    entry.timestamp = timeStr;
    entry.method = method;
    entry.details = details;
    entry.isError = isError;
    s_requestLogs.push_back(std::move(entry));
    if (s_requestLogs.size() > kMaxLogs) {
        s_requestLogs.erase(s_requestLogs.begin(), s_requestLogs.begin() + (s_requestLogs.size() - kMaxLogs));
    }
#if defined(EUI_TOOLING)
    devtoolsHostInstance().requestCompose();
#endif
}

void parseArgument(const std::string& arg, McpLaunchOptions& options) {
    if (arg == "--devtools") {
        options.enableDevtools = true;
    } else if (arg == "--mcp-server") {
        options.enableMcpServer = true;
    } else if (arg.rfind("--mcp-port=", 0) == 0) {
        std::string val = arg.substr(11);
        int p = std::atoi(val.c_str());
        if (p >= 0 && p <= 65535) {
            options.mcpPort = static_cast<uint16_t>(p);
        }
    } else if (arg.rfind("--devtools-tab=", 0) == 0) {
        options.initialTab = arg.substr(15);
    }
}

// Minimal JSON escape
std::string escapeJson(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if (static_cast<unsigned char>(c) < 0x20) {
            ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            ss << c;
        }
    }
    return ss.str();
}

// Minimal JSON parser helpers
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(pattern, pos)) != std::string::npos) {
        // Ensure this pattern is a key (preceded by { or , or whitespace) and followed by ':'
        bool validPre = (pos == 0) || (json[pos - 1] == '{' || json[pos - 1] == ',' ||
                                      json[pos - 1] == ' ' || json[pos - 1] == '\t' ||
                                      json[pos - 1] == '\r' || json[pos - 1] == '\n');
        std::size_t afterKey = pos + pattern.length();
        while (afterKey < json.length() && (json[afterKey] == ' ' || json[afterKey] == '\t' || json[afterKey] == '\r' || json[afterKey] == '\n')) {
            afterKey++;
        }
        if (validPre && afterKey < json.length() && json[afterKey] == ':') {
            afterKey++; // skip ':'
            while (afterKey < json.length() && (json[afterKey] == ' ' || json[afterKey] == '\t' || json[afterKey] == '\r' || json[afterKey] == '\n')) {
                afterKey++;
            }
            if (afterKey >= json.length() || json[afterKey] != '"') return {};
            afterKey++; // skip opening '"'
            std::string result;
            bool escape = false;
            while (afterKey < json.length()) {
                char c = json[afterKey++];
                if (escape) {
                    if (c == '"') result += '"';
                    else if (c == '\\') result += '\\';
                    else if (c == 'n') result += '\n';
                    else if (c == 'r') result += '\r';
                    else if (c == 't') result += '\t';
                    else result += c;
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    return result;
                } else {
                    result += c;
                }
            }
            return result;
        }
        pos += pattern.length();
    }
    return {};
}

double extractJsonNumber(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.length()) return defaultVal;
    std::size_t start = pos;
    if (json[pos] == '-' || json[pos] == '+') pos++;
    while (pos < json.length() && (std::isdigit(static_cast<unsigned char>(json[pos])) || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '-')) {
        pos++;
    }
    if (pos == start) return defaultVal;
    return std::atof(json.substr(start, pos - start).c_str());
}

bool extractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") return true;
    if (pos + 5 <= json.length() && json.substr(pos, 5) == "false") return false;
    return defaultVal;
}

std::string extractJsonObject(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = json.find(pattern);
    if (pos == std::string::npos) return {};
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '{') return {};
    int depth = 0;
    std::size_t start = pos;
    bool inStr = false;
    bool esc = false;
    for (std::size_t i = pos; i < json.length(); ++i) {
        char c = json[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') {
            inStr = !inStr;
            continue;
        }
        if (!inStr) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    return json.substr(start, i - start + 1);
                }
            }
        }
    }
    return {};
}

// ElementField by string name
bool parseElementField(const std::string& name, ElementField& outField) {
    if (name == "Color" || name == "color") { outField = ElementField::Color; return true; }
    if (name == "Opacity" || name == "opacity") { outField = ElementField::Opacity; return true; }
    if (name == "Radius" || name == "radius") { outField = ElementField::Radius; return true; }
    if (name == "BorderWidth" || name == "borderWidth" || name == "border_width") { outField = ElementField::BorderWidth; return true; }
    if (name == "BorderColor" || name == "borderColor" || name == "border_color") { outField = ElementField::BorderColor; return true; }
    if (name == "Blur" || name == "blur") { outField = ElementField::Blur; return true; }
    if (name == "ShadowEnabled" || name == "shadowEnabled") { outField = ElementField::ShadowEnabled; return true; }
    if (name == "ShadowColor" || name == "shadowColor") { outField = ElementField::ShadowColor; return true; }
    if (name == "ShadowBlur" || name == "shadowBlur") { outField = ElementField::ShadowBlur; return true; }
    if (name == "ShadowOffsetX" || name == "shadowOffsetX") { outField = ElementField::ShadowOffsetX; return true; }
    if (name == "ShadowOffsetY" || name == "shadowOffsetY") { outField = ElementField::ShadowOffsetY; return true; }
    if (name == "ShadowSpread" || name == "shadowSpread") { outField = ElementField::ShadowSpread; return true; }
    if (name == "ShadowInset" || name == "shadowInset") { outField = ElementField::ShadowInset; return true; }
    if (name == "GradientEnabled" || name == "gradientEnabled") { outField = ElementField::GradientEnabled; return true; }
    if (name == "GradientStart" || name == "gradientStart") { outField = ElementField::GradientStart; return true; }
    if (name == "GradientEnd" || name == "gradientEnd") { outField = ElementField::GradientEnd; return true; }
    if (name == "TextColor" || name == "textColor") { outField = ElementField::TextColor; return true; }
    return false;
}

// Parse hex color string "#RRGGBB" or "#RRGGBBAA"
core::Color parseHexColor(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    unsigned int val = 0;
    std::stringstream ss;
    ss << std::hex << h;
    ss >> val;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    if (h.length() == 8) {
        r = ((val >> 24) & 0xFF) / 255.0f;
        g = ((val >> 16) & 0xFF) / 255.0f;
        b = ((val >> 8) & 0xFF) / 255.0f;
        a = (val & 0xFF) / 255.0f;
    } else if (h.length() == 6) {
        r = ((val >> 16) & 0xFF) / 255.0f;
        g = ((val >> 8) & 0xFF) / 255.0f;
        b = (val & 0xFF) / 255.0f;
    }
    return {r, g, b, a};
}

std::string makeJsonRpcResponse(const std::string& id, const std::string& resultJson) {
    std::ostringstream ss;
    ss << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id) << ",\"result\":" << resultJson << "}";
    return ss.str();
}

std::string makeJsonRpcError(const std::string& id, int code, const std::string& message) {
    std::ostringstream ss;
    ss << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id)
       << ",\"error\":{\"code\":" << code << ",\"message\":\"" << escapeJson(message) << "\"}}";
    return ss.str();
}

std::string makeMcpTextContent(const std::string& text) {
    std::ostringstream ss;
    ss << "{\"content\":[{\"type\":\"text\",\"text\":\"" << escapeJson(text) << "\"}]}";
    return ss.str();
}

std::string makeMcpImageContent(const std::string& base64Png, const std::string& mimeType = "image/png") {
    std::ostringstream ss;
    ss << "{\"content\":[{\"type\":\"image\",\"data\":\"" << base64Png << "\",\"mimeType\":\"" << mimeType << "\"}]}";
    return ss.str();
}

// Tool definitions for tools/list
const char* kMcpToolsListJson =
"{"
  "\"tools\":["
    "{"
      "\"name\":\"describe_screen\","
      "\"description\":\"Returns a concise markdown summary of the screen for LLM reasoning (compact table of marks, element IDs, kinds, semantic labels, contexts, and capability flags). Much smaller token cost than full element tree.\","
      "\"inputSchema\":{\"type\":\"object\"}"
    "},"
    "{"
      "\"name\":\"extract_element_tree\","
      "\"description\":\"Extract full recursive UI element hierarchy tree with geometries and IDs.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"maxDepth\":{\"type\":\"number\",\"description\":\"Maximum depth to traverse (default 32)\"},"
          "\"compact\":{\"type\":\"boolean\",\"description\":\"Omit empty children/flags and format frame as concise x,y,w,h string (default false)\"},"
          "\"interactiveOnly\":{\"type\":\"boolean\",\"description\":\"Filter out branches with no interactive descendants (default false)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"get_element_details\","
      "\"description\":\"Retrieve detailed property values (color, border, bounds, flex, text, etc.) for a specific element ID.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Target element identifier\"}"
        "},"
        "\"required\":[\"elementId\"]"
      "}"
    "},"
    "{"
      "\"name\":\"get_interactive_marks\","
      "\"description\":\"Retrieve Set-of-Mark (SoM) indexed interactive elements currently in viewport, with enriched semantics (element text, container contextText, spatially nearestText, and capabilities: clickable, textInput, focusable).\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"onlyVisible\":{\"type\":\"boolean\",\"description\":\"Filter to only visible elements with non-zero bounds (default true)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"take_snapshot\","
      "\"description\":\"Take an indentation-based accessibility text snapshot of the page with unique short handles [ref=eN] (e.g. e1, e2). Use these handles directly in click_element, input_text, etc. Preferred over heavy JSON element tree.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"interactiveOnly\":{\"type\":\"boolean\",\"description\":\"Only list interactive/actionable elements (default true)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"click_element\","
      "\"description\":\"Simulate user click action on target element by ID or short handle (e.g. 'e2' or 'clock.city.add.hit').\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"target\":{\"type\":\"string\",\"description\":\"Target element ID or ref handle (e.g. 'e2' or 'clock.button')\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"},"
          "\"includeSnapshot\":{\"type\":\"boolean\",\"description\":\"Include updated text snapshot in response (default false)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"click_mark\","
      "\"description\":\"Simulate user click action on target element by its Set-of-Mark index (from get_interactive_marks or describe_screen).\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"markIndex\":{\"type\":\"number\",\"description\":\"Mark index number (1-based)\"},"
          "\"includeSnapshot\":{\"type\":\"boolean\",\"description\":\"Include updated text snapshot in response (default false)\"}"
        "},"
        "\"required\":[\"markIndex\"]"
      "}"
    "},"
    "{"
      "\"name\":\"input_text\","
      "\"description\":\"Inject text into target element by ID, ref handle ('e2'), or active focus. Supports replace mode (default) to replace existing content cleanly, or append mode.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"target\":{\"type\":\"string\",\"description\":\"Target element ID or ref handle (e.g. 'e5' or 'clock.search.input.hit')\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"},"
          "\"text\":{\"type\":\"string\",\"description\":\"Text content to input\"},"
          "\"mode\":{\"type\":\"string\",\"enum\":[\"replace\",\"append\"],\"description\":\"'replace' (default) clears text first; 'append' adds to end\"},"
          "\"clearFirst\":{\"type\":\"boolean\",\"description\":\"Explicitly clear input before typing (default false)\"},"
          "\"includeSnapshot\":{\"type\":\"boolean\",\"description\":\"Include updated text snapshot in response (default false)\"}"
        "},"
        "\"required\":[\"text\"]"
      "}"
    "},"
    "{"
      "\"name\":\"press_key\","
      "\"description\":\"Dispatch a keyboard key event (e.g. Enter, Backspace, Escape, Tab, Up, Down, Left, Right).\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"key\":{\"type\":\"string\",\"description\":\"Key name (e.g. Enter, Backspace, Escape, Tab, Up, Down, A, C, V, Z)\"},"
          "\"target\":{\"type\":\"string\",\"description\":\"Optional target element ID or ref handle (defaults to focused element)\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"},"
          "\"ctrl\":{\"type\":\"boolean\",\"description\":\"Ctrl modifier flag (default false)\"},"
          "\"shift\":{\"type\":\"boolean\",\"description\":\"Shift modifier flag (default false)\"},"
          "\"alt\":{\"type\":\"boolean\",\"description\":\"Alt modifier flag (default false)\"},"
          "\"includeSnapshot\":{\"type\":\"boolean\",\"description\":\"Include updated text snapshot in response (default false)\"}"
        "},"
        "\"required\":[\"key\"]"
      "}"
    "},"
    "{"
      "\"name\":\"focus_element\","
      "\"description\":\"Set keyboard focus on target element by ID or ref handle.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"target\":{\"type\":\"string\",\"description\":\"Target element ID or ref handle (e.g. 'e5')\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"scroll_element\","
      "\"description\":\"Dispatch scroll event delta to target element or container.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"target\":{\"type\":\"string\",\"description\":\"Target container element ID or ref handle\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"},"
          "\"deltaX\":{\"type\":\"number\",\"description\":\"Horizontal scroll delta\"},"
          "\"deltaY\":{\"type\":\"number\",\"description\":\"Vertical scroll delta\"}"
        "},"
        "\"required\":[\"deltaY\"]"
      "}"
    "},"
    "{"
      "\"name\":\"capture_viewport\","
      "\"description\":\"Capture screenshot of current entire application viewport. If filePath is provided, saves to disk and returns file path (saving tokens). Otherwise returns Base64 PNG.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"filePath\":{\"type\":\"string\",\"description\":\"File path on disk to save PNG image to (recommended to avoid base64 token cost)\"},"
          "\"drawMarks\":{\"type\":\"boolean\",\"description\":\"Overlay Set-of-Mark (#eN) labels directly onto the screenshot image (default false)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"capture_element\","
      "\"description\":\"Capture cropped screenshot of a specific element region by ID or ref handle. If filePath is provided, saves to disk.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"target\":{\"type\":\"string\",\"description\":\"Element ID or ref handle to capture\"},"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Alternative name for target\"},"
          "\"filePath\":{\"type\":\"string\",\"description\":\"File path on disk to save PNG image to\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"modify_element_property\","
      "\"description\":\"Hot-override an element style property in live runtime (Color, Opacity, Radius, BorderWidth, TextColor, etc.).\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Target element ID\"},"
          "\"field\":{\"type\":\"string\",\"description\":\"Property field name (e.g. Color, Radius, TextColor, Opacity)\"},"
          "\"value\":{\"description\":\"New property value (number, boolean, or hex color string #RRGGBB)\"}"
        "},"
        "\"required\":[\"elementId\",\"field\",\"value\"]"
      "}"
    "}"
  "]"
"}";

} // namespace

McpLaunchOptions parseMcpCommandLine(int argc, const char* const* argv) {
    McpLaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) continue;
        std::string arg = argv[i];
        if (arg == "--mcp-port" && i + 1 < argc && argv[i + 1] != nullptr) {
            int p = std::atoi(argv[++i]);
            if (p >= 0 && p <= 65535) {
                options.mcpPort = static_cast<uint16_t>(p);
            }
        } else {
            parseArgument(arg, options);
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
            if (str == "--mcp-port" && i + 1 < argc && argvW[i + 1] != nullptr) {
                int next_size = WideCharToMultiByte(CP_UTF8, 0, argvW[i + 1], -1, NULL, 0, NULL, NULL);
                std::string nextStr(next_size, 0);
                WideCharToMultiByte(CP_UTF8, 0, argvW[i + 1], -1, &nextStr[0], next_size, NULL, NULL);
                if (!nextStr.empty() && nextStr.back() == '\0') nextStr.pop_back();
                int p = std::atoi(nextStr.c_str());
                if (p >= 0 && p <= 65535) {
                    options.mcpPort = static_cast<uint16_t>(p);
                    ++i;
                    continue;
                }
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

    if (options.enableDevtools) {
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
    }

    if (options.enableMcpServer) {
        startMcpServer(options.mcpPort);
    }
#else
    (void)options;
#endif
}

static std::string handleMcpJsonRpcRequestInternal(const std::string& requestJson, core::dsl::Runtime* explicitRuntime) {
#if !defined(EUI_TOOLING)
    return makeJsonRpcError("null", -32601, "EUI DevTools tooling is disabled");
#else
    // Extract ID (number or string)
    std::string idStr = "null";
    std::size_t idPos = 0;
    while ((idPos = requestJson.find("\"id\"", idPos)) != std::string::npos) {
        bool validPre = (idPos == 0) || (requestJson[idPos - 1] == '{' || requestJson[idPos - 1] == ',' ||
                                          requestJson[idPos - 1] == ' ' || requestJson[idPos - 1] == '\t' ||
                                          requestJson[idPos - 1] == '\r' || requestJson[idPos - 1] == '\n');
        std::size_t afterKey = idPos + 4;
        while (afterKey < requestJson.length() && (requestJson[afterKey] == ' ' || requestJson[afterKey] == '\t' || requestJson[afterKey] == '\r' || requestJson[afterKey] == '\n')) {
            afterKey++;
        }
        if (validPre && afterKey < requestJson.length() && requestJson[afterKey] == ':') {
            afterKey++;
            while (afterKey < requestJson.length() && (requestJson[afterKey] == ' ' || requestJson[afterKey] == '\t' || requestJson[afterKey] == '\r' || requestJson[afterKey] == '\n')) {
                afterKey++;
            }
            if (afterKey < requestJson.length()) {
                if (requestJson[afterKey] == '"') {
                    idStr = "\"" + extractJsonString(requestJson, "id") + "\"";
                } else {
                    std::size_t endPos = requestJson.find_first_of(",}\n\r", afterKey);
                    if (endPos != std::string::npos) {
                        idStr = requestJson.substr(afterKey, endPos - afterKey);
                    }
                }
            }
            break;
        }
        idPos += 4;
    }

    std::string method = extractJsonString(requestJson, "method");
    if (method.empty()) {
        return makeJsonRpcError(idStr, -32600, "Invalid Request: missing method");
    }

    // MCP initialization / ping
    if (method == "initialize") {
        std::string initResult =
            "{\"protocolVersion\":\"2024-11-05\","
            "\"capabilities\":{\"tools\":{}},"
            "\"serverInfo\":{\"name\":\"eui-mcp-server\",\"version\":\"1.0.0\"}}";
        return makeJsonRpcResponse(idStr, initResult);
    }
    if (method == "notifications/initialized") {
        return ""; // Notification, no response
    }
    if (method == "ping") {
        return makeJsonRpcResponse(idStr, "{}");
    }

    // tools/list
    if (method == "tools/list") {
        return makeJsonRpcResponse(idStr, kMcpToolsListJson);
    }

    // tools/call
    if (method == "tools/call") {
        std::string params = extractJsonObject(requestJson, "params");
        std::string toolName = extractJsonString(params, "name");
        std::string arguments = extractJsonObject(params, "arguments");

        core::dsl::Runtime* rt = explicitRuntime;
        if (rt == nullptr) {
            rt = devtoolsHostInstance().pageRuntime();
        }

        if (toolName == "describe_screen") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string desc = describeScreen(*rt);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(desc));
        }

        if (toolName == "take_snapshot") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            bool interactiveOnly = extractJsonBool(arguments, "interactiveOnly", true);
            std::string snap = takeSnapshot(*rt, interactiveOnly);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(snap));
        }

        if (toolName == "extract_element_tree") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            int maxDepth = static_cast<int>(extractJsonNumber(arguments, "maxDepth", 32.0));
            bool compact = extractJsonBool(arguments, "compact", false);
            bool interactiveOnly = extractJsonBool(arguments, "interactiveOnly", false);
            std::string treeJson = extractElementTreeJson(*rt, maxDepth, compact, interactiveOnly);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(treeJson));
        }

        if (toolName == "get_element_details") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            if (target.empty()) {
                return makeJsonRpcError(idStr, -32602, "Missing target or elementId argument");
            }
            std::string realId = resolveTarget(rt, target);
            std::string detailsJson = extractElementDetailsJson(*rt, realId);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(detailsJson));
        }

        if (toolName == "get_interactive_marks") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            bool onlyVisible = extractJsonBool(arguments, "onlyVisible", true);
            auto marks = extractInteractiveElements(*rt, onlyVisible);
            std::string marksJson = formatInteractiveElementsJson(marks);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(marksJson));
        }

        if (toolName == "click_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            auto res = clickElement(*rt, realId);
            syncUiFrameAfterAction(rt);
            bool incSnap = extractJsonBool(arguments, "includeSnapshot", false);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"";
            if (incSnap) {
                ss << ",\"snapshot\":\"" << escapeJson(takeSnapshot(*rt, true)) << "\"";
            }
            ss << "}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "click_mark") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            int markIdx = static_cast<int>(extractJsonNumber(arguments, "markIndex", 0.0));
            auto marks = extractInteractiveElements(*rt, true);
            auto res = clickMark(*rt, markIdx, marks);
            syncUiFrameAfterAction(rt);
            bool incSnap = extractJsonBool(arguments, "includeSnapshot", false);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"";
            if (incSnap) {
                ss << ",\"snapshot\":\"" << escapeJson(takeSnapshot(*rt, true)) << "\"";
            }
            ss << "}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "input_text") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            std::string text = extractJsonString(arguments, "text");
            std::string mode = extractJsonString(arguments, "mode");
            if (mode.empty()) {
                mode = "replace"; // Default to replace mode to avoid duplicate concatenated text
            }
            bool clearFirst = extractJsonBool(arguments, "clearFirst", false);
            auto res = inputText(*rt, realId, text, mode, clearFirst);
            syncUiFrameAfterAction(rt);
            bool incSnap = extractJsonBool(arguments, "includeSnapshot", false);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"";
            if (incSnap) {
                ss << ",\"snapshot\":\"" << escapeJson(takeSnapshot(*rt, true)) << "\"";
            }
            ss << "}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "press_key") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string keyName = extractJsonString(arguments, "key");
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            bool ctrl = extractJsonBool(arguments, "ctrl", false);
            bool shift = extractJsonBool(arguments, "shift", false);
            bool alt = extractJsonBool(arguments, "alt", false);
            auto res = pressKey(*rt, realId, keyName, ctrl, shift, alt);
            syncUiFrameAfterAction(rt);
            bool incSnap = extractJsonBool(arguments, "includeSnapshot", false);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"";
            if (incSnap) {
                ss << ",\"snapshot\":\"" << escapeJson(takeSnapshot(*rt, true)) << "\"";
            }
            ss << "}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "focus_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            auto res = focusElement(*rt, realId);
            syncUiFrameAfterAction(rt);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "scroll_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            float deltaX = static_cast<float>(extractJsonNumber(arguments, "deltaX", 0.0));
            float deltaY = static_cast<float>(extractJsonNumber(arguments, "deltaY", 0.0));
            auto res = scrollElement(*rt, realId, deltaX, deltaY);
            syncUiFrameAfterAction(rt);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"revision\":" << s_uiRevision.load()
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "capture_viewport") {
            FramebufferImage img = captureViewportScreenshot(nullptr);
            if (!img.valid()) {
                return makeJsonRpcError(idStr, -32000, "Viewport screenshot capture failed or no framebuffer");
            }
            std::vector<uint8_t> pngBytes = encodeImageToPng(img);
            std::string filePath = extractJsonString(arguments, "filePath");
            if (!filePath.empty()) {
                std::ofstream ofs(filePath, std::ios::binary);
                if (ofs.is_open()) {
                    ofs.write(reinterpret_cast<const char*>(pngBytes.data()), pngBytes.size());
                    std::ostringstream ss;
                    ss << "{\"success\":true,\"savedToFile\":\"" << escapeJson(filePath) << "\",\"bytes\":" << pngBytes.size() << "}";
                    return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
                }
            }
            std::string base64Png = encodeBase64(pngBytes);
            return makeJsonRpcResponse(idStr, makeMcpImageContent(base64Png));
        }

        if (toolName == "capture_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string target = extractJsonString(arguments, "target");
            if (target.empty()) target = extractJsonString(arguments, "elementId");
            if (target.empty()) target = extractJsonString(arguments, "id");
            std::string realId = resolveTarget(rt, target);
            float currentDpi = devtoolsHostInstance().dpiScale();
            FramebufferImage img = captureElementScreenshot(*rt, realId, currentDpi);
            if (!img.valid()) {
                return makeJsonRpcError(idStr, -32000, "Element region capture failed or element not visible");
            }
            std::vector<uint8_t> pngBytes = encodeImageToPng(img);
            std::string filePath = extractJsonString(arguments, "filePath");
            if (!filePath.empty()) {
                std::ofstream ofs(filePath, std::ios::binary);
                if (ofs.is_open()) {
                    ofs.write(reinterpret_cast<const char*>(pngBytes.data()), pngBytes.size());
                    std::ostringstream ss;
                    ss << "{\"success\":true,\"savedToFile\":\"" << escapeJson(filePath) << "\",\"bytes\":" << pngBytes.size() << "}";
                    return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
                }
            }
            std::string base64Png = encodeBase64(pngBytes);
            return makeJsonRpcResponse(idStr, makeMcpImageContent(base64Png));
        }

        if (toolName == "modify_element_property") {
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            std::string fieldName = extractJsonString(arguments, "field");
            if (fieldName.empty()) {
                fieldName = extractJsonString(arguments, "property");
            }
            ElementField field;
            if (!parseElementField(fieldName, field)) {
                return makeJsonRpcError(idStr, -32602, "Unknown or unsupported element property field: " + fieldName);
            }

            FieldValue val;
            FieldKind kind = fieldKind(field);
            if (kind == FieldKind::Number) {
                val = fieldValueOf(static_cast<float>(extractJsonNumber(arguments, "value", 0.0)));
            } else if (kind == FieldKind::Flag) {
                val = fieldValueOf(extractJsonBool(arguments, "value", false));
            } else if (kind == FieldKind::Color) {
                std::string hexStr = extractJsonString(arguments, "value");
                val = fieldValueOf(parseHexColor(hexStr));
            }

            bool ok = devtoolsHostInstance().modifyElementProperty(elementId, field, val);
            std::ostringstream ss;
            ss << "{\"success\":" << (ok ? "true" : "false")
               << ",\"elementId\":\"" << escapeJson(elementId) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        return makeJsonRpcError(idStr, -32601, "Unknown tool: " + toolName);
    }

    return makeJsonRpcError(idStr, -32601, "Method not found: " + method);
#endif
}

std::string handleMcpJsonRpcRequest(const std::string& requestJson, core::dsl::Runtime* explicitRuntime) {
    std::string method = extractJsonString(requestJson, "method");
    std::string toolName;
    std::string details;
    if (method == "tools/call") {
        std::string params = extractJsonObject(requestJson, "params");
        toolName = extractJsonString(params, "name");
        std::string args = extractJsonObject(params, "arguments");
        std::string minified;
        bool inStr = false;
        bool esc = false;
        for (char c : args) {
            if (esc) {
                minified += c;
                esc = false;
                continue;
            }
            if (c == '\\') {
                esc = true;
                minified += c;
                continue;
            }
            if (c == '"') {
                inStr = !inStr;
                minified += c;
                continue;
            }
            if (inStr) {
                minified += c;
            } else {
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    minified += c;
                }
            }
        }
        if (minified.empty() || minified == "{}" || minified == "null") {
            details = "";
        } else {
            if (minified.length() > 64) {
                minified = minified.substr(0, 61) + "...";
            }
            details = minified;
        }
    } else {
        details = "";
    }

    std::string response = handleMcpJsonRpcRequestInternal(requestJson, explicitRuntime);

    bool isErr = response.find("\"error\"") != std::string::npos;
    std::string logMethod = (method == "tools/call" && !toolName.empty())
        ? ("tools/call: " + toolName)
        : (method.empty() ? "unknown" : method);

    appendRequestLog(logMethod, details, isErr);
    return response;
}

// -----------------------------------------------------------------------------
// TCP HTTP Server for MCP Protocol
// -----------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
inline int closeSocket(socket_t s) { return closesocket(s); }
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
inline int closeSocket(socket_t s) { return close(s); }
#endif

std::atomic<bool> s_serverRunning{false};
std::atomic<uint16_t> s_serverPort{8990};
std::thread s_serverThread;
socket_t s_listenSocket = kInvalidSocket;

void handleClientConnection(socket_t clientSock) {
    std::string rawRequest;
    char buffer[4096];
    int bytesRead = 0;

    // Read HTTP request header & body
    while ((bytesRead = recv(clientSock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        rawRequest.append(buffer, bytesRead);
        if (rawRequest.find("\r\n\r\n") != std::string::npos) {
            // Check Content-Length to see if body is complete
            std::size_t clPos = rawRequest.find("Content-Length:");
            if (clPos == std::string::npos) {
                clPos = rawRequest.find("content-length:");
            }
            if (clPos != std::string::npos) {
                std::size_t clEnd = rawRequest.find("\r\n", clPos);
                int length = std::atoi(rawRequest.substr(clPos + 15, clEnd - (clPos + 15)).c_str());
                std::size_t headerEnd = rawRequest.find("\r\n\r\n") + 4;
                if (rawRequest.length() >= headerEnd + length) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    if (rawRequest.empty()) {
        closeSocket(clientSock);
        return;
    }

    // Extract HTTP method and body
    std::string jsonBody;
    std::size_t headerEnd = rawRequest.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        jsonBody = rawRequest.substr(headerEnd + 4);
    } else {
        jsonBody = rawRequest;
    }

    // Handle OPTIONS CORS preflight
    if (rawRequest.rfind("OPTIONS ", 0) == 0) {
        std::string httpResp =
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Content-Length: 0\r\n\r\n";
        send(clientSock, httpResp.c_str(), static_cast<int>(httpResp.length()), 0);
        closeSocket(clientSock);
        return;
    }

    std::string rpcResponse = handleMcpJsonRpcRequest(jsonBody);

    std::ostringstream httpOut;
    httpOut << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: application/json; charset=utf-8\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Content-Length: " << rpcResponse.length() << "\r\n"
            << "\r\n"
            << rpcResponse;

    std::string respStr = httpOut.str();
    send(clientSock, respStr.c_str(), static_cast<int>(respStr.length()), 0);
    closeSocket(clientSock);
}

void serverWorker(uint16_t port) {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        s_serverRunning = false;
        return;
    }
#endif

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == kInvalidSocket) {
        s_serverRunning = false;
#if defined(_WIN32)
        WSACleanup();
#endif
        return;
    }

    int opt = 1;
#if defined(_WIN32)
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(s_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        closeSocket(s_listenSocket);
        s_listenSocket = kInvalidSocket;
        s_serverRunning = false;
#if defined(_WIN32)
        WSACleanup();
#endif
        return;
    }

    if (listen(s_listenSocket, 10) < 0) {
        closeSocket(s_listenSocket);
        s_listenSocket = kInvalidSocket;
        s_serverRunning = false;
#if defined(_WIN32)
        WSACleanup();
#endif
        return;
    }

    sockaddr_in boundAddr{};
    int boundLen = sizeof(boundAddr);
    if (getsockname(s_listenSocket, (sockaddr*)&boundAddr,
#if defined(_WIN32)
                    &boundLen
#else
                    (socklen_t*)&boundLen
#endif
        ) == 0) {
        port = ntohs(boundAddr.sin_port);
    }

    s_serverRunning = true;
    s_serverPort = port;

    std::string discoveryPath;
    try {
        discoveryPath = (std::filesystem::temp_directory_path() / "eui_mcp_active.json").string();
        std::ofstream ofs(discoveryPath);
        if (ofs.is_open()) {
            ofs << "{\"port\":" << port
                << ",\"pid\":" <<
#if defined(_WIN32)
                GetCurrentProcessId()
#else
                getpid()
#endif
                << "}\n";
        }
    } catch (...) {}

    while (s_serverRunning) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(s_listenSocket, &readFds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms select timeout for clean shutdown check

        int activity = select(static_cast<int>(s_listenSocket + 1), &readFds, nullptr, nullptr, &tv);
        if (activity < 0) {
            break;
        }
        if (activity > 0 && FD_ISSET(s_listenSocket, &readFds)) {
            sockaddr_in clientAddr{};
            int clientLen = sizeof(clientAddr);
            socket_t clientSock = accept(s_listenSocket, (sockaddr*)&clientAddr,
#if defined(_WIN32)
                                         &clientLen
#else
                                         (socklen_t*)&clientLen
#endif
            );
            if (clientSock != kInvalidSocket) {
                // Process request in lightweight worker thread
                std::thread(handleClientConnection, clientSock).detach();
            }
        }
    }

    if (!discoveryPath.empty()) {
        try { std::filesystem::remove(discoveryPath); } catch (...) {}
    }

    if (s_listenSocket != kInvalidSocket) {
        closeSocket(s_listenSocket);
        s_listenSocket = kInvalidSocket;
    }
#if defined(_WIN32)
    WSACleanup();
#endif
}

} // namespace

bool startMcpServer(uint16_t port) {
    if (s_serverRunning) {
        if (s_serverPort == port) {
            return true;
        }
        stopMcpServer();
    }
    s_serverPort = port;
    s_serverRunning = false;
    if (s_serverThread.joinable()) {
        s_serverThread.join();
    }
    s_serverThread = std::thread(serverWorker, port);
    
    // Wait briefly for server startup
    for (int i = 0; i < 20; ++i) {
        if (s_serverRunning) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return s_serverRunning;
}

void stopMcpServer() {
    if (!s_serverRunning && !s_serverThread.joinable()) {
        return;
    }
    s_serverRunning = false;
    if (s_listenSocket != kInvalidSocket) {
        closeSocket(s_listenSocket);
        s_listenSocket = kInvalidSocket;
    }
    if (s_serverThread.joinable()) {
        s_serverThread.join();
    }
}

bool isMcpServerRunning() {
    return s_serverRunning;
}

uint16_t currentMcpServerPort() {
    return s_serverPort;
}

std::vector<McpRequestLogEntry> getMcpRequestLogs() {
    std::lock_guard<std::mutex> lock(s_logsMutex);
    return s_requestLogs;
}

void clearMcpRequestLogs() {
    std::lock_guard<std::mutex> lock(s_logsMutex);
    s_requestLogs.clear();
}

} // namespace modules::devtools
