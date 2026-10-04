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
      "\"name\":\"extract_element_tree\","
      "\"description\":\"Extract full recursive UI element hierarchy tree with geometries and IDs.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"maxDepth\":{\"type\":\"number\",\"description\":\"Maximum depth to traverse (default 32)\"}"
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
      "\"description\":\"Retrieve Set-of-Mark (SoM) indexed interactive elements (buttons, inputs, links) currently in viewport.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"onlyVisible\":{\"type\":\"boolean\",\"description\":\"Filter to only visible elements with non-zero bounds (default true)\"}"
        "}"
      "}"
    "},"
    "{"
      "\"name\":\"click_element\","
      "\"description\":\"Simulate user click action on target element by its string ID.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Target element ID\"}"
        "},"
        "\"required\":[\"elementId\"]"
      "}"
    "},"
    "{"
      "\"name\":\"click_mark\","
      "\"description\":\"Simulate user click action on target element by its Set-of-Mark index (from get_interactive_marks).\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"markIndex\":{\"type\":\"number\",\"description\":\"Mark index number (1-based)\"}"
        "},"
        "\"required\":[\"markIndex\"]"
      "}"
    "},"
    "{"
      "\"name\":\"input_text\","
      "\"description\":\"Inject text input into target element by ID or active focus.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Target element ID\"},"
          "\"text\":{\"type\":\"string\",\"description\":\"Text content to input\"}"
        "},"
        "\"required\":[\"elementId\",\"text\"]"
      "}"
    "},"
    "{"
      "\"name\":\"scroll_element\","
      "\"description\":\"Dispatch scroll event delta to target element or container.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Target container element ID\"},"
          "\"deltaX\":{\"type\":\"number\",\"description\":\"Horizontal scroll delta\"},"
          "\"deltaY\":{\"type\":\"number\",\"description\":\"Vertical scroll delta\"}"
        "},"
        "\"required\":[\"elementId\",\"deltaY\"]"
      "}"
    "},"
    "{"
      "\"name\":\"capture_viewport\","
      "\"description\":\"Capture screenshot of current entire application viewport, returns Base64 PNG image.\","
      "\"inputSchema\":{\"type\":\"object\"}"
    "},"
    "{"
      "\"name\":\"capture_element\","
      "\"description\":\"Capture cropped screenshot of a specific element region by ID, returns Base64 PNG image.\","
      "\"inputSchema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
          "\"elementId\":{\"type\":\"string\",\"description\":\"Element ID to capture\"}"
        "},"
        "\"required\":[\"elementId\"]"
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

    if (options.enableMcpServer) {
        startMcpServer(options.mcpPort);
    }
#else
    (void)options;
#endif
}

std::string handleMcpJsonRpcRequest(const std::string& requestJson, core::dsl::Runtime* explicitRuntime) {
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

        if (toolName == "extract_element_tree") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            int maxDepth = static_cast<int>(extractJsonNumber(arguments, "maxDepth", 32.0));
            std::string treeJson = extractElementTreeJson(*rt, maxDepth);
            return makeJsonRpcResponse(idStr, makeMcpTextContent(treeJson));
        }

        if (toolName == "get_element_details") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            if (elementId.empty()) {
                return makeJsonRpcError(idStr, -32602, "Missing elementId argument");
            }
            std::string detailsJson = extractElementDetailsJson(*rt, elementId);
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
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            auto res = clickElement(*rt, elementId);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "click_mark") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            int markIdx = static_cast<int>(extractJsonNumber(arguments, "markIndex", 0.0));
            auto marks = extractInteractiveElements(*rt, true);
            auto res = clickMark(*rt, markIdx, marks);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "input_text") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            std::string text = extractJsonString(arguments, "text");
            auto res = inputText(*rt, elementId, text);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "scroll_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            float deltaX = static_cast<float>(extractJsonNumber(arguments, "deltaX", 0.0));
            float deltaY = static_cast<float>(extractJsonNumber(arguments, "deltaY", 0.0));
            auto res = scrollElement(*rt, elementId, deltaX, deltaY);
            std::ostringstream ss;
            ss << "{\"success\":" << (res.success ? "true" : "false")
               << ",\"message\":\"" << escapeJson(res.message) << "\"}";
            return makeJsonRpcResponse(idStr, makeMcpTextContent(ss.str()));
        }

        if (toolName == "capture_viewport") {
            FramebufferImage img = captureViewportScreenshot(nullptr);
            if (!img.valid()) {
                return makeJsonRpcError(idStr, -32000, "Viewport screenshot capture failed or no framebuffer");
            }
            std::vector<uint8_t> pngBytes = encodeImageToPng(img);
            std::string base64Png = encodeBase64(pngBytes);
            return makeJsonRpcResponse(idStr, makeMcpImageContent(base64Png));
        }

        if (toolName == "capture_element") {
            if (rt == nullptr) {
                return makeJsonRpcError(idStr, -32000, "No active page runtime available");
            }
            std::string elementId = extractJsonString(arguments, "elementId");
            if (elementId.empty()) {
                elementId = extractJsonString(arguments, "id");
            }
            float currentDpi = devtoolsHostInstance().dpiScale();
            FramebufferImage img = captureElementScreenshot(*rt, elementId, currentDpi);
            if (!img.valid()) {
                return makeJsonRpcError(idStr, -32000, "Element region capture failed or element not visible");
            }
            std::vector<uint8_t> pngBytes = encodeImageToPng(img);
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

    s_serverRunning = true;
    s_serverPort = port;

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

} // namespace modules::devtools
