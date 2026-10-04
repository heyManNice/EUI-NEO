# MCP Bridge

`modules/devtools/mcp_bridge` 是连接 Model Context Protocol (MCP) 客户端与 EUI-NEO 应用的零外部依赖 Python 桥接层。

它支持两种接入形态：

- 作为持久化的标准 stdio MCP server：供 Cursor、Claude Desktop、VS Code (Cline / Roo Code)、Windsurf 等原生工具调用。
- 作为终端单行命令 CLI：供 LLM 智能体在隔离的 shell 环境中通过单行命令直接执行界面动作，避免手工拼接 JSON-RPC。

## 特性

- 零第三方依赖：纯 Python 3 标准库（`sys`、`json`、`urllib.request`、`socket`、`subprocess`）实现，无需 `pip install`。
- 随机端口与零冲突：启动应用时由操作系统分配临时空闲端口，避免固定端口冲突。
- 自动生命周期管理：若目标应用未启动，桥接层自动以无头 MCP 模式（`--mcp-server --mcp-port=<port>`）拉起应用；会话结束时自动回收。
- 实例自动发现：通过临时文件记录运行中实例，亦可通过 `--print-port` 查询当前活跃端口。
- 紧凑文本快照与短句柄：基于无障碍树生成缩进文本快照，带短句柄（`[ref=eN]`），降低 token 消耗。
- 动作即时状态回传：支持 `--includeSnapshot` 参数，在执行点击或输入后立即返回最新界面快照，消除额外往返。
- 文件级截图落盘：支持直接将视口截图保存为本地文件，避免 base64 污染上下文。

## 命令行交互

终端环境或自动化脚本可直接通过子命令操作界面：

```sh
# 抓取界面文本快照（带 [ref=eN] 短句柄）
python modules/devtools/mcp_bridge/eui_mcp_bridge.py snapshot

# 紧凑模式快照（仅保留可交互元素）
python modules/devtools/mcp_bridge/eui_mcp_bridge.py snapshot --interactiveOnly

# 点击元素（支持短句柄 eN 或原始 id）
python modules/devtools/mcp_bridge/eui_mcp_bridge.py click e2

# 点击并在响应中附带最新快照
python modules/devtools/mcp_bridge/eui_mcp_bridge.py click e2 --includeSnapshot

# 向输入框填入文本（默认清空原有内容）
python modules/devtools/mcp_bridge/eui_mcp_bridge.py fill e14 "Beijing"

# 派发键盘按键（Enter、Backspace、Escape 等）
python modules/devtools/mcp_bridge/eui_mcp_bridge.py press Enter

# 截取视口图像并保存到文件
python modules/devtools/mcp_bridge/eui_mcp_bridge.py shot --out screen.png

# 停止后台运行的应用实例
python modules/devtools/mcp_bridge/eui_mcp_bridge.py stop

# 查看当前活跃端口
python modules/devtools/mcp_bridge/eui_mcp_bridge.py --print-port
```

## 客户端配置

### Cursor / VS Code (Cline / Roo Code)

在项目的 `.cursor/mcp.json` 或 Cline 设置中配置：

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

### Claude Desktop

在 `claude_desktop_config.json` 中配置：

```json
{
  "mcpServers": {
    "eui-neo": {
      "command": "python",
      "args": [
        "<PATH_TO_EUI_NEO>/modules/devtools/mcp_bridge/eui_mcp_bridge.py"
      ]
    }
  }
}
```

## 选项说明

- 默认无参运行：启动 stdio MCP 代理服务。
- `--app <path>`：指定自动拉起的目标应用可执行文件路径。
- `--port <port>`：连接指定端口（0 为自动分配或发现）。
- `--no-spawn`：仅连接已有实例，不自动启动新进程。
- `--print-port`：输出当前运行实例的端口后退出。
- `call <tool> [--arg value]`：调用任意 MCP 原生工具。

## 编译排查提示

当 EUI-NEO 应用在后台运行时，Ninja 构建重新复制静态字体或资源可能因 Windows 文件占用而失败。重新编译前可执行：

```sh
python modules/devtools/mcp_bridge/eui_mcp_bridge.py stop
```

释放应用占用的资源锁。
