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
- 属性区（选中元素后固定在标签页底部）显示该元素的 Frame、Flags、盒模型与 `zIndex`，并可直接编辑视觉属性：颜色（就地展开 H/S/V/A 四个通道滑块）、透明度、圆角、边框、模糊、阴影和文字颜色（仅文本元素）。展示顺序与编辑器类型由面板侧一份属性元数据表驱动，运行时只负责读写，所以加一个属性＝表里加一行 + 运行时加一个 case。
- 编辑是**临时覆盖**：值写在页面元素之上，每次 compose 后重新应用，从不写回应用状态或源码。被覆盖的属性会标出来（数值用强调色、行尾出现还原按钮），页脚显示“已覆盖的元素数”和 `Reset` 一键还原；还原需要应用重新组合页面，元素才会回到自己的值。
- 文本内容与尺寸/间距类属性尚未开放：前者需要给面板接上键盘与文本输入，后者每次编辑都要让应用重组并重新布局。
- 在页面上拾取元素（点选）尚未实现。
- 输入过滤、内容区预留、独立窗口由 `app::detail::OverlayHost` 这个框架接缝完成，面板不依赖业务组件和核心流程的特例。

## 与框架文档的差异

- 热键：面板先于 `DslAppConfig::onKeyEvent` 拿到按键（走 `OverlayHost::handleHotkey`），所以 F12 / Ctrl+Shift+I 被面板消费时应用收不到。这是“面板优先”的取舍，与 `docs/事件.md` 里“按键未消费才交给 `onKeyEvent`”的传递顺序不同。
- 独立窗口：面板用 `openWindow` 的返回值（`DslWindowHandle`）管理生命周期，因此该入口的返回类型相对 main 从 `void` 改成了句柄（源码兼容、ABI 不兼容）。

## 测试

- `devtools_host`：面板状态机、热键、输入路由、缩放、停靠行为和元素树列表的单元测试，属于 `unit` 标签。
- `devtools_viewer`：手动验证面板在不同窗口后端和渲染后端下的视觉效果，需要 `-DEUI_BUILD_TEST_FIXTURES=ON`。
