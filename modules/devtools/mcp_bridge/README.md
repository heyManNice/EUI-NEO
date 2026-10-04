# EUI-NEO MCP Bridge

Zero-dependency Python 3 bridge connecting Model Context Protocol (MCP) clients (Cursor, Claude Desktop, VS Code Cline / Roo Code, Windsurf, Zed) to EUI-NEO applications.

---

## 🌟 Key Features

1. **Zero External Dependencies**: Built entirely using Python 3 standard library (`sys`, `json`, `urllib.request`, `socket`, `subprocess`). No `pip install` required.
2. **Random Port & Conflict-Free**: Automatically acquires an ephemeral random free port from the OS when launching applications, eliminating port collisions.
3. **Automatic Lifecycle Management**:
   - If the target application is not running, the bridge automatically launches it in clean, headless MCP mode (`--mcp-server --mcp-port=<port>`) without devtools panel obstruction.
   - When the AI session terminates, the spawned application is cleanly exited.
4. **Auto-Discovery**: If an EUI-NEO application is already running, the bridge discovers it via the system temporary discovery file (`eui_mcp_active.json`) or probes the standard port.

---

## 🚀 Quick Setup

### 1. Cursor / VS Code (Cline / Roo Code)
Add to your project's `.cursor/mcp.json` or Cline MCP settings:

```json
{
  "mcpServers": {
    "eui-neo": {
      "command": "python",
      "args": [
        "${workspaceFolder}/modules/devtools/mcp_bridge/eui_mcp_bridge.py"
      ]
    }
  }
}
```

### 2. Claude Desktop
Add to your `claude_desktop_config.json` (located at `%APPDATA%\Claude\claude_desktop_config.json` on Windows):

```json
{
  "mcpServers": {
    "eui-neo": {
      "command": "python",
      "args": [
        "d:/MYDATA/Project/VsCode/EUI-NEO/modules/devtools/mcp_bridge/eui_mcp_bridge.py"
      ]
    }
  }
}
```

---

## 🛠️ Command-Line Options

You can test the bridge manually from the terminal:

```bash
# Auto-detect running app or allocate a random port and launch ./build/clock.exe:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py

# Connect to a specific already running port:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --port 8990

# Specify custom executable to launch:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --app ./build/my_app.exe

# Only connect to existing instance, never auto-spawn:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --no-spawn
```
