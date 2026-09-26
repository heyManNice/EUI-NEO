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
- 树按行高虚拟化，折叠状态、选中元素和滚动位置都存在面板自己的 Runtime 状态里。选中某个元素后点击详情里的 id 会复制到剪贴板。
- 高亮选中元素、在页面上拾取元素尚未实现。
- 输入过滤、内容区预留、独立窗口由 `app::detail::OverlayHost` 这个框架接缝完成，面板不依赖业务组件和核心流程的特例。

## 测试

- `devtools_host`：面板状态机、热键、输入路由、缩放、停靠行为和元素树列表的单元测试，属于 `unit` 标签。
- `devtools_viewer`：手动验证面板在不同窗口后端和渲染后端下的视觉效果，需要 `-DEUI_BUILD_TEST_FIXTURES=ON`。
