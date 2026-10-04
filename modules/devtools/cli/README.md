# Automation CLI & MCP

`modules/devtools/cli` 是连�?Model Context Protocol (MCP) 客户端与 EUI-NEO 应用的零外部依赖 Python 桥接层�?
它支持两种接入形态：

- 作为持久化的标准 stdio MCP server：供 Cursor、Claude Desktop、VS Code (Cline / Roo Code)、Windsurf 等原生工具调用�?- 作为终端单行命令 CLI：供 LLM 智能体在隔离�?shell 环境中通过单行命令直接执行界面动作，避免手工拼�?JSON-RPC�?
## 特�?
- 零第三方依赖：纯 Python 3 标准库（`sys`、`json`、`urllib.request`、`socket`、`subprocess`）实现，无需 `pip install`�?- 随机端口与零冲突：启动应用时由操作系统分配临时空闲端口，避免固定端口冲突�?- 自动生命周期管理：若目标应用未启动，桥接层自动以无头 MCP 模式（`--mcp-server --mcp-port=<port>`）拉起应用；作为 stdio 会话拉起的实例在会话结束时自动回收，CLI 拉起的实例则保留在后台�?- 实例自动发现与自愈重连：通过临时文件记录运行中实例，亦可通过 `--print-port` 查询当前活跃端口；标�?stdio 代理在转发失败时会重新读取该文件并切到新实例，因�?停止 �?编译 �?重新运行"不会打断已连接的客户端�?- 紧凑文本快照与短句柄：基于无障碍树生成缩进文本快照，带短句柄（`[ref=eN]`），降低 token 消耗�?- 动作变更摘要：每个改变页面的动作在响应中�?`diff`（新增、移除、状态与编号变化），无需为确认结果再取一次快照�?- 句柄漂移防护：短句柄是元素在当前集合中的位置，若某句柄在取快照后已指向别的元素，动作会被拒绝并给出新旧元素名，避免静默点错�?- 文件级截图落盘：支持直接将视口截图保存为本地文件，避�?base64 污染上下文�?
## 命令行交�?
终端环境或自动化脚本可直接通过子命令操作界面：

```sh
# 抓取界面文本快照（带 [ref=eN] 短句柄）
python modules/devtools/cli/eui_cli.py snapshot

# 紧凑模式快照（仅保留可交互元素）
python modules/devtools/cli/eui_cli.py snapshot --interactiveOnly

# 点击元素（支持短句柄 eN 或原�?id�?python modules/devtools/cli/eui_cli.py click e2

# 点击并在响应中附带最新快�?python modules/devtools/cli/eui_cli.py click e2 --includeSnapshot

# 向输入框填入文本（默认清空原有内容）
python modules/devtools/cli/eui_cli.py fill e14 "Beijing"

# 派发键盘按键（Enter、Backspace、Escape 等）
python modules/devtools/cli/eui_cli.py press Enter

# 截取视口图像并保存到文件
python modules/devtools/cli/eui_cli.py shot --out screen.png

# 停止后台运行的应用实�?python modules/devtools/cli/eui_cli.py stop

# 停止并重新拉起（编译完跑这条，直接切到新二进制）
python modules/devtools/cli/eui_cli.py restart

# 查询当前应答的进程与构建（可交互元素表用 describe_screen�?python modules/devtools/cli/eui_cli.py call app_status
python modules/devtools/cli/eui_cli.py call describe_screen

# 截图并把 Set-of-Mark 编号烧进图像（filePath 由应用进程写入，需绝对路径�?python modules/devtools/cli/eui_cli.py call capture_viewport \
    --filePath /abs/path/annotated.png --drawMarks

# 查看当前活跃端口
python modules/devtools/cli/eui_cli.py --print-port
```

`click`、`fill`、`press`、`focus_element`、`scroll_element` 的响应都�?`diff` 字段，说明本次动作改�?了什么（新增/移除的元素、disabled / selected / 文本变化，以及元素编号的位移）。没�?`diff` 即表示本�?动作没有改变任何可交互元素的状态�?
## 客户端配�?
### Cursor / VS Code (Cline / Roo Code)

在项目的 `.cursor/mcp.json` �?Cline 设置中配置：

```json
{
  "mcpServers": {
    "eui-neo": {
      "command": "python",
      "args": [
        "${workspaceFolder}/modules/devtools/cli/eui_cli.py"
      ]
    }
  }
}
```

### Claude Desktop

�?`claude_desktop_config.json` 中配置：

```json
{
  "mcpServers": {
    "eui-neo": {
      "command": "python",
      "args": [
        "<PATH_TO_EUI_NEO>/modules/devtools/cli/eui_cli.py"
      ]
    }
  }
}
```

> Windows 上若 `python` 命中 Microsoft Store 占位符（调用后无输出并以非零码退出），请把上面的
> `"command"` 换成 Python 解释器的绝对路径，例�?`"C:/Python312/python.exe"`�?
已运行的 stdio 会话执行的是启动时加载的脚本�?*桥接层自身的改动需要重启该 MCP server 才会生效**�?在此之前可用上文�?CLI 子命令验证�?
## 选项说明

- 默认无参运行：启�?stdio MCP 代理服务�?- `--app <path>`：指定自动拉起的目标应用可执行文件路径�?- `--port <port>`：连接指定端口（0 为自动分配或发现）�?- `--no-spawn`：仅连接已有实例，不自动启动新进程�?- `--print-port`：输出当前运行实例的端口后退出�?- `restart`：停止当前实例并重新拉起，用于切换编译产物�?- `call <tool> [--arg value]`：调用任�?MCP 原生工具�?
## 编译排查提示

�?EUI-NEO 应用在后台运行时，Ninja 构建重新复制静态字体或资源可能�?Windows 文件占用而失败。重新编译前可执行：

```sh
python modules/devtools/cli/eui_cli.py stop
```

释放应用占用的资源锁。编译完成后，`restart` 可一步完�?停止旧实�?�?拉起新构�?�?
```sh
python modules/devtools/cli/eui_cli.py restart
```

拉起后用 `call app_status` 确认新进程的 `exePath` �?`pid`，避免对着旧实例调试�?
