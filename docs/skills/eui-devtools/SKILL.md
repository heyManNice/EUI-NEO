---
name: eui-devtools
description: Guide and runbook for AI agents to inspect, automate, debug, and test EUI-NEO desktop applications using the DevTools MCP bridge and single-line CLI. Use when the user asks to debug the UI, click buttons, enter text, inspect the element tree, take screenshots, or enable DevTools in a new or existing EUI project.
---

# EUI DevTools & MCP Automation Guide

本指南用于指�?AI 智能体如何为 EUI-NEO 应用配置、启动、测试与调试界面�?
## 目标与核心能�?
通过 `modules/devtools` �?`cli`，智能体可对运行中的 EUI-NEO 应用获得全套感知与控制能力：

- **屏幕摘要**：`describe_screen` 给出可交互元素表（编号、元�?ID、语义标签、上下文、能力标志），是**定位控件的首�?*，token 开销也最低；需要几何信息时�?`get_interactive_marks`（含边界）�?- **界面快照**：`take_snapshot` 输出缩进文本无障碍树，带 `[ref=eN]` 短句柄，适合阅读页面文案与层级结构�?- **交互动作**：点击、输入文本、派发按键、滚轮滚动、设置焦点�?*动作响应自带变更摘要（`diff`�?*，不必为确认结果再取一次快照�?- **视觉回读**：视口截图落盘，避免 base64 占用上下文；`drawMarks` 可把 Set-of-Mark 编号直接烧进截图�?- **生命周期**：`app_status` 报告正在应答的进程与构建，`restart` 一条命令完�?停止 �?拉起新版"�?- **状态闭�?*：操作后附带即时快照，消除异步竞态�?
## 运行前提检�?
在尝试与应用交互前，执行快速检查：

```sh
# 检查当前是否有运行中的实例以及活跃端口
python modules/devtools/cli/eui_cli.py --print-port
```

若输�?`0` 或未发现实例，说明需要先启动或由 bridge 自动拉起目标应用�?
确认**当前应答的是哪一个构�?*（尤其在你刚编译完之后）�?
```sh
python modules/devtools/cli/eui_cli.py call app_status
```

返回 `exePath`、`pid`、`port`、`uiRevision`、`frameSequence`，以�?`framebuffer` / `logical` / `dpiScale` 三个坐标信息。屏幕与源码对不上时，第一个该问的就是"我连的是不是刚编译的那个二进�?�?
## 项目启用 DevTools

若目标项目尚未包�?DevTools 面板或无法通过 MCP 连接，按以下步骤启用�?
### 1. CMakeLists.txt 链接模块

在应用的 `CMakeLists.txt` 中添�?`eui::module_devtools` 链接�?
```cmake
target_link_libraries(my_app PRIVATE
    eui::neo
    eui::module_devtools
)
```

DevTools 面板�?`EUI_TOOLING` 开关控制（Debug 构建默认自动开启）。Release 构建若需保留调试能力，需配置�?
```sh
cmake -S . -B build -DEUI_TOOLING=ON
```

### 2. 源码中声明静�?Session

在应用的顶层源文件（�?`main.cpp` �?`app.cpp`）的全局命名空间中声明一个文件作用域�?`Session`�?
```cpp
#include "eui_neo.h"
#include "modules/devtools/devtools.h"

namespace {
// 保持 Session 在进程生命周期内存活，程序启动时自动挂载接缝，无需改动任何业务 UI 逻辑
static modules::devtools::Session s_devtoolsSession;
} // namespace

namespace app {
void compose(eui::Ui& ui, const eui::Screen& screen) {
    // 正常业务页面声明
}
} // namespace app
```

### 3. 编译应用

编译前若有旧实例在后台运行，先停止旧实例以释放字体与静态资源文件锁�?
```sh
# 停止并重新拉起（编译前先 stop，编译后再跑这条即可一步到位）
python modules/devtools/cli/eui_cli.py stop
cmake --build build --target my_app
python modules/devtools/cli/eui_cli.py restart
```

## AI 操作工作流（单行 CLI�?
在终端环境中，优先使用单�?CLI 命令，无需拼接复杂�?JSON-RPC�?
### 1. 先看屏幕：定位用 describe_screen，读文案�?take_snapshot

```sh
# 首选：可交互元素表，带编号、语义标签、上下文�?disabled/textInput/focusable 等能力标�?python modules/devtools/cli/eui_cli.py call describe_screen

# 需要几何信息（边界坐标）时：JSON 形式，含 bounds 与能力标�?python modules/devtools/cli/eui_cli.py call get_interactive_marks

# 需要页面层级与文案时：缩进文本快照
python modules/devtools/cli/eui_cli.py snapshot

# 更精简，只保留可交互控�?python modules/devtools/cli/eui_cli.py snapshot --interactiveOnly
```

三者的编号是同一套：截图上的 `e11`、`describe_screen` 表格里的 `#11`、快照里�?`[ref=e11]` 指的是同一个元素，可以交叉验证�?
### 2. 执行交互操作

```sh
# 点击按钮（支持短句柄 eN 或原�?id�?python modules/devtools/cli/eui_cli.py click e2

# 点击并在单次返回中获取操作后的最新快�?python modules/devtools/cli/eui_cli.py click e2 --includeSnapshot

# 向输入框输入文本（默认清空原文本并输入新值）
python modules/devtools/cli/eui_cli.py fill e14 "北京"

# 追加文本而不清空
python modules/devtools/cli/eui_cli.py fill e14 "追加内容" --append

# 发送键盘按�?python modules/devtools/cli/eui_cli.py press Enter

# 滚动指定元素或视口（deltaY > 0 向下滚动�?python modules/devtools/cli/eui_cli.py call scroll_element --target e1 --deltaY 120
```

**每个会改变页面的动作，响应里都带 `diff`**�?
```json
{"success": true, "revision": 6, "message": "...",
 "diff": {"added":   ["clock.city.card.beijing.bg"],
          "removed": ["clock.city.card.paris.bg"],
          "changed": [{"id": "clock.city.add.hit",
                       "disabled": {"from": true, "to": false},
                       "text": {"from": "Already Added", "to": "Add City +"}},
                      {"id": "clock.search.input.hit", "ref": {"from": 14, "to": 12}}]}}
```

- **没有 `diff` 字段 = 什么都没变�?* 这本身就是答案，不要为此再取快照�?- `ref` 字段报告的是**该元素的编号位移**，即你手里那个句柄是否已经失效�?- `diff` 只列出变化项，因此对一�?看起来没反应"的点击，它能直接区分"没生�?�?生效了但视觉上不明显"�?
### 3. 截取视口图像验证

```sh
# 保存截图到指定文件路径（避免 base64 占用大量 token�?python modules/devtools/cli/eui_cli.py shot --out build/verify.png

# �?Set-of-Mark 编号烧进截图，便于对照编号与元素
python modules/devtools/cli/eui_cli.py call capture_viewport \
    --filePath D:/abs/path/build/annotated.png --drawMarks
```

两点注意�?
- `filePath` �?*应用进程**写入，路径以应用�?*工作目录**为基准，因此请传**绝对路径**。传相对路径会被拒绝并明确报错（不会静默退�?base64）。CLI �?`shot --out` 会自动把路径解析为绝对路径，用它是安全的�?- `drawMarks` 依赖页面的渲染流程，因此只在�?host 抓帧的路径上生效；正常情况下都能用�?
### 4. 生命周期管理

```sh
# 停止后台运行的应用实�?python modules/devtools/cli/eui_cli.py stop

# 停止并重新拉起（编译完跑这条，直接切到新二进制）
python modules/devtools/cli/eui_cli.py restart

# 已启动的情况下，查询当前应答的进程与构建
python modules/devtools/cli/eui_cli.py call app_status
```

测试流程完成后，务必停止应用以释放系统资源与构建文件锁（或直�?`restart`）�?
## 短句柄（`[ref=eN]`）的有效�?
`eN` 是元素在**当前可交互元素集合中的位�?*，不是稳定标识。任何增删元素的动作都会让后续元素的编号整体前移或后移。例如删掉列表中第三个城市后，第四个城市的删除按钮会�?`e13` 变成 `e11`�?
服务端为此做了两层保护，遇到时不必困惑：

1. **漂移即报�?*：若某个 `eN` 现在指向的元素与取快照时不同，动作会被拒绝，并同时给出新旧元素名�?
   ```
   Ref e11 now names 'clock.city.card.paris.remove.hit', not 'clock.city.card.beijing.remove.hit'
   as it did when the snapshot was taken. Take a new snapshot before acting.
   ```

   按提示重新取快照即可。这不是故障，而是拦下一�?点到了另一个元素却报告成功"�?
2. **动作响应里的 `ref` 位移**：`diff.changed[].ref` 直接告诉你哪些编号变了、变成了什么，通常据此就能算出新编号，不必再取快照�?
因此稳定的做法是�?*在会增删元素的操作之后，重新取一次快�?*，或直接读上一条动作的 `diff`�?
## 坐标�?
同一块屏幕有两套坐标，混用会导致定位偏差�?
| 空间 | 来源 | 尺寸（示例） |
|:--|:--|:--|
| 逻辑坐标 | `get_interactive_marks` �?`bounds`、快照与截图标注里的元素�?| 1066 × 720 |
| 物理坐标 | 截图 PNG 的像素尺�?| 1600 × 1080 |

换算比例就是 `dpiScale`（上例为 1.5）。三个值都可以�?`app_status` 读到�?
```json
{"framebuffer": {"width": 1600, "height": 1080},
 "logical":     {"width": 1066, "height": 720},
 "dpiScale": 1.5}
```

## MCP 客户端持久化集成

若用户希望在 IDE 中直接使用图形化 MCP 工具（如 Cursor、VS Code Cline）：

在工作区 `.cursor/mcp.json` �?VS Code MCP 配置文件中添加：

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

配置后重启或刷新 MCP 连接，即可在 IDE 对话框内直接调用原生 MCP 工具�?
两点须知�?
- 持久化的 stdio 桥接进程�?*启动�?*解析目标端口。应用被 `stop` 之后重启会拿到新端口，桥接层会在转发失败时自动重读实例发现文件并重连，因此常规的"停止 �?编译 �?重新运行"不会再打断连接�?- 但桥�?*脚本本身的改�?*不会热加载：已运行的会话仍执行旧代码。改�?`eui_cli.py` 后需要重启该 MCP server（在客户端设置里关闭再开�?eui-neo）才会生效；在此之前请改用上面的 CLI 路径验证，CLI 每次调用都会重新读取脚本�?
## 常见问题与排�?
- **MCP 工具调用一直挂起、不返回**：桥接进程持有的端口已失效，且（旧版本）失败响应未回显请�?`id`，导致客户端无法匹配响应而永久等待。请重启�?MCP server，或改用 CLI 路径（每次调用独立进程，不受影响）。当前版本已回显 `id` 并在失败时自动重连�?- **提示 `python` 执行失败或退出码 9009（Windows 环境�?*：系�?`python` 命中 Windows Store 占位符。改�?Python 绝对路径（例�?`C:/Python312/python.exe`），配置客户端时同样不要照抄 `"command": "python"`�?- **编译时提示字体或资源文件无法写入**：后台有运行中的应用实例锁定了文件。执�?`stop` �?`restart` 后再编译�?- **动作被拒绝并提示 `Ref eN now names ...`**：句柄已因元素增删而漂移，按提示重新取快照；若只是想拿到新编号，读上一条动作响应的 `diff.changed[].ref` 即可�?- **动作返回成功但界面上看不出变�?*：先看响应里有没�?`diff`。没�?`diff` 就是确实什么都没变（例如点击了已选中项、或点击只更新了内部状态）；有 `diff` 则变化项写得很明确�?- **截图里标注不出编�?*：`drawMarks` 需要走页面渲染流程抓帧，请确认请求的是 `capture_viewport` 且应用处于正常渲染状态�?- **点击后快照状态未及时更新**：动作调度默认自动等待两帧渲染与重组完成；若需更长业务延迟，可�?CLI 间加短暂停顿或配�?`--includeSnapshot` 验证�?- **截图像是"上一�?的状�?*：抓帧会等待一个比请求更新的帧，正常情况下不会发生；若仍怀疑，连续抓两次并比对 `frameGeneration`（返回值中递增即表示是新帧）�?
