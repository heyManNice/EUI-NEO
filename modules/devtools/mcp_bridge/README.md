# EUI-NEO MCP Bridge

Zero-dependency Python 3 bridge connecting Model Context Protocol (MCP) clients (Cursor, Claude Desktop, VS Code Cline / Roo Code, Windsurf, Zed) to EUI-NEO applications.

---

## 🌟 Key Features

1. **Zero External Dependencies**: Built entirely using Python 3 standard library (`sys`, `json`, `urllib.request`, `socket`, `subprocess`). No `pip install` required.
2. **Dual-Mode Operation**:
   - **Stdio Mode**: Full standard JSON-RPC MCP server for Cursor, Claude Desktop, Cline, Roo Code.
   - **Single-Line CLI Mode**: Perfect for LLMs running in isolated terminal/shell commands (`python eui_mcp_bridge.py call <tool> [args]`), completely avoiding hand-crafted JSON-RPC.
3. **Random Port & Conflict-Free**: Automatically acquires an ephemeral random free port from the OS when launching applications, eliminating port collisions.
4. **Automatic Lifecycle Management**:
   - If the target application is not running, the bridge automatically launches it in clean, headless MCP mode (`--mcp-server --mcp-port=<port>`) without devtools panel obstruction.
   - When the AI session terminates, the spawned application is cleanly exited.
5. **Auto-Discovery & Direct HTTP**:
   - Discovers running instances via system temp discovery file (`eui_mcp_active.json`).
   - Query active port anytime with `--print-port`.
   - Direct HTTP POST is also available at `http://127.0.0.1:<port>/mcp`.
6. **Robust LLM Primitives (Chrome DevTools & Playwright aligned)**:
   - `take_snapshot`: Compact indentation-based accessibility text snapshot with short handles (`[ref=eN]`), role, label, and bounds. Token cost is ~1/4 of full JSON.
   - **Short Ref Handles**: Actions accept `e1`, `e2`, `#e5` directly instead of verbose IDs.
   - **`includeSnapshot` in actions**: Instant state feedback right in the action response.
   - **File-based Screenshots**: `capture_viewport` saves directly to disk (`--filePath`), never polluting context with 150KB base64 JSON unless explicitly asked.

---

## ⚡ Direct CLI Mode (For Agent Shell Commands)

Agents running one-off shell commands can invoke actions via clean single-word commands:

```bash
# 1. Take a text snapshot of the screen with short ref handles [ref=eN]:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py snapshot

# 2. Click an element using short handle:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py click e2
# Or with instant post-action snapshot:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py click e2 --includeSnapshot

# 3. Fill / replace text into an input field:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py fill e14 "Beijing"

# 4. Dispatch keyboard key:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py press Enter

# 5. Take screenshot directly to disk (saves tokens):
python modules/devtools/mcp_bridge/eui_mcp_bridge.py shot --out screen.png

# 6. Stop background application:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py stop

# 7. Check active port:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --print-port
```

---

## 🚀 Standard MCP Client Setup (Persistent Mode)

> **Note on Windows**: If `python` refers to the Windows Store placeholder (exit 9009), use the absolute path to your Python interpreter (e.g. `C:/Users/<Username>/miniconda3/python.exe`).

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

```bash
# Stdio proxy mode (auto-spawn or connect):
python modules/devtools/mcp_bridge/eui_mcp_bridge.py

# Query active port:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --print-port

# Connect to a specific port:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --port 8990

# Only connect to existing instance, never auto-spawn:
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --no-spawn

# Take compact interactive-only snapshot (no static text nodes):
python modules/devtools/mcp_bridge/eui_mcp_bridge.py snapshot --interactiveOnly
```

---

## 💡 Troubleshooting & Build Tips

- **Rebuilding while an app is running**:
  If an EUI-NEO application (such as `clock.exe`) is running in the background, rebuilds with `ninja` may fail when copying assets because Windows file locks prevent overwriting font/asset files. Run:
  ```bash
  python modules/devtools/mcp_bridge/eui_mcp_bridge.py stop
  ```
  before compiling to release file locks.

