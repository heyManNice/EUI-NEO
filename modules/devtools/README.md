# DevTools 模块

`modules/devtools` 是 EUI-NEO 的内置开发面板。它在应用内容之上绘制自己的 Runtime，可以停靠在窗口的左侧、底部或右侧，也可以放到独立窗口；停靠时会从应用页面中预留出对应区域。

## 使用

应用保持一个 `Session` 存活即可接入，面板本身不需要应用代码参与：

```cpp
#include "eui_neo.h"

#include "modules/devtools/devtools.h"

namespace {
const modules::devtools::Session devtoolsSession;
}

namespace app {

void compose(eui::Ui& ui, const eui::Screen& screen) {
    // 正常声明页面即可。
}

} // namespace app
```

`Session` 需要在 `app::shutdown()` 之后销毁，文件作用域对象满足这个条件。接入后：

- `F12` 或 `Ctrl+Shift+I` 显示或隐藏面板；热键只在 `Press` 时触发，并被面板消费，不会再交给 `DslAppConfig::onKeyEvent`。
- 拖动面板内边可以调整面板大小，面板大小在最小值和窗口最小内容宽度之间收敛。
- 工具栏右侧的 more 菜单可以切换停靠位置或切换为独立窗口。

构建时用 `target_link_libraries(<app> PRIVATE eui::module_devtools)` 链接模块。

## Debug 与 Release

模块的面板实现只在 `Debug` 配置下编译：

- `Release` 配置下模块只提供 `modules::devtools::available()`，它返回 `false`；`Session` 是空类型，接入代码会被完整优化掉。
- `Debug` 配置下面板代码编译进模块库，只在应用创建 `Session` 时链接进来。
- 因此 DevTools 需要和 EUI-NEO 使用同一个构建配置。Release SDK 不包含面板实现，Release 构建的 SDK 无法为 Debug 应用提供面板。

## 能力边界

- 面板读取 `core::app/performance_snapshot.h` 中的性能快照，也就是窗口标题统计使用同一份数据。
- `Elements` 标签页列出应用页面的元素树，数据来自 `Runtime` 的只读元素树快照。应用层只在面板显示该标签页时拉取快照：结构变化立即拉取，纯帧变化（动画、滚动、悬停导致的坐标变化）按固定间隔节流。
- 树按行高虚拟化，折叠状态、选中元素和滚动位置都存在面板自己的 Runtime 状态里。节点默认收起，只有用户点开的节点会展开，深页面的树不会一打开就铺满整个列表，点击一行选中它。
- 鼠标在树上悬浮某一行时，会在页面上预览那个元素：页面 Runtime 在渲染收尾时按盒模型填充它的 margin / border / padding / content 四个区域（半透明纯填充、不描边，与浏览器 devtools 一致），变换与祖先裁剪都和元素本身一致，所以滚动容器、缩放和 clip 里的元素也能对齐；移出列表或离开 Elements 标签就撤掉。
- 选中（点击）只在面板内部生效，不会在页面上留下常驻高亮：页面上只会出现鼠标位置的悬浮预览。
- 属性区显示在标签页底部，高度可以拖动上边缘的分隔条调整（向上给属性更多空间，向下最多到可读的最小高度，不会把树挤没），高度会被记住并跨选中元素沿用。选中会在元素被删掉、页面重组后失效时自动收起属性区；没有选中元素时属性区完全不渲染，树独占整个标签页。
- 属性区显示该元素的 Frame、Flags、盒模型与 `zIndex`，并可直接编辑视觉属性：颜色（就地展开 H/S/V/A 四个通道滑块）、透明度、圆角、边框、模糊、阴影和文字颜色（仅文本元素）。展示顺序与编辑器类型由面板侧一份属性元数据表驱动，运行时只负责读写，所以加一个属性＝表里加一行 + 运行时加一个 case。
- 编辑是**临时覆盖**：值写在页面元素之上，每次 compose 后重新应用，从不写回应用状态或源码。被覆盖的属性会标出来（数值用强调色、行尾出现还原按钮），页脚显示“已覆盖的元素数”和 `Reset` 一键还原；还原需要应用重新组合页面，元素才会回到自己的值。
- 文本内容与尺寸/间距类属性尚未开放：前者需要给面板接上键盘与文本输入，后者每次编辑都要让应用重组并重新布局。
- 工具栏左侧的箭头开启拾取：开启后指针归面板所有，页面收到的是“指针已离开”，面板把指针位置交给应用层，由**页面自己的命中测试**回答（变换、祖先裁剪、绘制顺序都与画出来的一致），元素随指针预览（复用树上悬浮那套盒模型高亮）。点击时把元素作为选中交给树，这次点击不会传给页面；拾取一次后自动关闭，再点箭头或按 Esc 也可关闭。面板停靠在窗口里或独立成窗时都一样工作。
- 树会把新来的选中显示出来：展开藏住它的祖先、必要时滚到那一行，每个选中只做一次，之后手动折叠或滚动不会被抢回。
- 拾取不改变光标（面板不碰窗口光标），也不在页面上留常驻高亮：页面上只出现跟随指针的那一次预览。
- 输入过滤、内容区预留、独立窗口由 `app::detail::OverlayHost` 这个框架接缝完成，面板不依赖业务组件和核心流程的特例。
- 属性区的尺寸是主题度量（`devtools_theme.h` 的 `propertiesInitialFraction`、`propertiesMinimumHeight`、`propertiesMinimumTreeHeight`、`propertiesHandleHeight`），高度本身存在面板状态里，不写回应用。

## 标签页与计划

标签栏列出面板的全部标签：`Performance` 与 `Elements` 有内容，其余只是先占住名字，打开时会说明自己准备读什么。加一个计划中的面板，先在 `devtools_ui.cpp` 的标签表里加一行，再在这里补一条计划。

计划中的标签页（按优先级）。「读什么」写的是数据来源，最后两列说明代价：多数页只加一个只读的 Debug 钩子，不改变运行时行为。

| 标签页 | 读什么 | 用来回答 | 代价 |
| --- | --- | --- | --- |
| `State` | `InstanceStore` 的 15 张 id→instance 表（rects / layouts / scrollStates / sliderStates / timers / dirtyKeys / paintBounds / retainedLayers …） | 某个元素的滚动、滑块、计时器状态到底是什么；哪些实例已经 unseen 却没被回收 | 只读快照 |
| `Input` | 事件流与命中结果（`hitTestFocusable`、`focusedId_`、`hoverTargetCache`、被捕获的交互） | 这个事件被谁接走了——交互子元素会吞掉父元素的处理，是 DSL 里最难靠猜的一类问题 | 路由上报 |
| `Frames` | `requestUiUpdate` / `requestFrame` / 动画 / 惯性滚动，加上已有的 dirty rect 计数 | 为什么一直在重绘，这一帧为什么重绘 | 帧原因上报 |
| `Layout` | `Element::frame` 与 `LayoutInstance` | 测量值与实际 frame 的差、溢出父容器、被祖先裁掉 | 无（数据已在元素树上） |
| `Animations` | transition / easing / timer 实例与 `isAnimating()` | 是谁让这一帧动起来的，计时器为什么没触发 | 小 |
| `Resources` | 字体（默认 / 图标 / 回退，缺字）、图片（stb / libpng / nanosvg、远程就绪）、shadertoy | assets 丢失、字体回退没生效、纹理与字体内存增长 | 中等，要接资源缓存 |
| `Windows` | `DslWindowManager` 与 `AppRunner` | 多窗口与 modal 状态、tray 可用与被隐藏、每窗口 fps 与 dpi | 中等，面板目前只认识主窗口 |
| `Scale` | `dpiScale` / `pointerScale` / `uiScale()` | 逻辑单位与像素的换算，为什么在高分屏上偏了 | 展示很便宜；“强制缩放”需要应用提供钩子 |

另外两条计划：

- **应用自定义标签页**：由应用注册自己的标签页（音频、网络、业务状态这类领域页），模块保持自成，也不必为每个领域往框架加钩子。
- **标签栏放不下时**：标签行会裁掉超出部分（右侧按钮始终可点），窄停靠的紧凑模式下只能看到前几个标签，而且左侧的拾取箭头在紧凑模式下不显示——两个问题都计划用“收进 more 菜单 / 标签栏可横向滚动”一并解决。

## 与框架文档的差异

- 热键：面板先于 `DslAppConfig::onKeyEvent` 拿到按键（走 `OverlayHost::handleHotkey`），所以 F12 / Ctrl+Shift+I 被面板消费时应用收不到。这是“面板优先”的取舍，与 `docs/事件.md` 里“按键未消费才交给 `onKeyEvent`”的传递顺序不同。
- 独立窗口：面板用 `openWindow` 的返回值（`DslWindowHandle`）管理生命周期，因此该入口的返回类型相对 main 从 `void` 改成了句柄（源码兼容、ABI 不兼容）。

## 测试

- `devtools_host`：面板状态机、热键、输入路由、缩放、停靠行为和元素树列表的单元测试，属于 `unit` 标签。
- `devtools_viewer`：手动验证面板在不同窗口后端和渲染后端下的视觉效果，需要 `-DEUI_BUILD_TEST_FIXTURES=ON`。
