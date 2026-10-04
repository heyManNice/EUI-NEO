#include "modules/devtools/mcp_action.h"

#include <algorithm>

namespace modules::devtools {

McpActionResult clickElement(core::dsl::Runtime& runtime, const std::string& elementId) {
    McpActionResult res;
    core::dsl::Element* el = runtime.findElement(elementId);
    if (el == nullptr) {
        res.success = false;
        res.message = "Element '" + elementId + "' not found";
        return res;
    }

    const core::Rect bounds{el->frame.x, el->frame.y, el->frame.width, el->frame.height};
    res.targetBounds = bounds;

    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        res.success = false;
        res.message = "Element '" + elementId + "' has zero or negative dimensions";
        return res;
    }

    // Direct invocation if onClick callback is bound
    if (el->onClick) {
        el->onClick();
        runtime.requestFullPaint();
        res.success = true;
        res.message = "Element '" + elementId + "' onClick callback invoked successfully";
        return res;
    }

    // Otherwise, simulate pointer press and release at center coordinates
    const double centerX = static_cast<double>(bounds.x + bounds.width * 0.5f);
    const double centerY = static_cast<double>(bounds.y + bounds.height * 0.5f);

    core::PointerEvent moveEv;
    moveEv.action = core::PointerAction::Move;
    moveEv.x = centerX;
    moveEv.y = centerY;
    runtime.pushPointerEvent(moveEv);

    core::PointerEvent pressEv;
    pressEv.action = core::PointerAction::Press;
    pressEv.button = core::PointerButton::Left;
    pressEv.buttons = core::PointerButtons{core::PointerButton::Left};
    pressEv.x = centerX;
    pressEv.y = centerY;
    runtime.pushPointerEvent(pressEv);

    core::PointerEvent releaseEv;
    releaseEv.action = core::PointerAction::Release;
    releaseEv.button = core::PointerButton::Left;
    releaseEv.buttons = core::PointerButtons{};
    releaseEv.x = centerX;
    releaseEv.y = centerY;
    runtime.pushPointerEvent(releaseEv);

    runtime.requestFullPaint();
    res.success = true;
    res.message = "Pointer click dispatched to element '" + elementId + "' at (" +
                  std::to_string(static_cast<int>(centerX)) + ", " +
                  std::to_string(static_cast<int>(centerY)) + ")";
    return res;
}

McpActionResult clickMark(core::dsl::Runtime& runtime, int markIndex, const std::vector<McpInteractiveElement>& marks) {
    for (const auto& mark : marks) {
        if (mark.markIndex == markIndex) {
            return clickElement(runtime, mark.id);
        }
    }
    McpActionResult res;
    res.success = false;
    res.message = "Interactive mark #" + std::to_string(markIndex) + " not found";
    return res;
}

McpActionResult inputText(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& text) {
    McpActionResult res;
    core::dsl::Element* el = runtime.findElement(elementId);
    if (el == nullptr) {
        std::string hitCandidate = elementId + ".hit";
        el = runtime.findElement(hitCandidate);
        if (el == nullptr && elementId.size() > 5 && elementId.substr(elementId.size() - 5) == ".text") {
            hitCandidate = elementId.substr(0, elementId.size() - 5) + ".hit";
            el = runtime.findElement(hitCandidate);
        }
        if (el == nullptr) {
            res.success = false;
            res.message = "Element '" + elementId + "' not found";
            return res;
        }
    }

    const std::string targetId = el->id;
    const core::Rect bounds{el->frame.x, el->frame.y, el->frame.width, el->frame.height};
    res.targetBounds = bounds;

    // If it is a Text element without text input callback, directly update text content and refresh
    if (el->kind == core::dsl::ElementKind::Text && !el->onTextInput) {
        el->text = text;
        runtime.requestElementRefresh();
        runtime.requestFullPaint();
        res.success = true;
        res.message = "Element text updated to: " + text;
        return res;
    }

    core::TextInputEvent inputEv;
    inputEv.text = text;

    if (el->onPress) {
        core::PointerEvent pressEv;
        pressEv.action = core::PointerAction::Press;
        pressEv.button = core::PointerButton::Left;
        pressEv.x = bounds.x + bounds.width * 0.5;
        pressEv.y = bounds.y + bounds.height * 0.5;
        el->onPress(pressEv, bounds);
    }

    if (el->onTextInput && !el->disabled) {
        el->onTextInput(inputEv);
        runtime.requestElementRefresh();
        runtime.requestFullPaint();
        res.success = true;
        res.message = "Input text simulated on element '" + targetId + "'";
        return res;
    }

    runtime.requestElementRefresh();
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Input text simulated on element '" + targetId + "'";
    return res;
}

McpActionResult scrollElement(core::dsl::Runtime& runtime, const std::string& elementId, float deltaX, float deltaY) {
    McpActionResult res;
    core::dsl::Element* el = runtime.findElement(elementId);
    if (el != nullptr) {
        res.targetBounds = core::Rect{el->frame.x, el->frame.y, el->frame.width, el->frame.height};
    }

    core::ScrollEvent scrollEv;
    scrollEv.x = static_cast<double>(deltaX);
    scrollEv.y = static_cast<double>(deltaY);
    runtime.pushScrollEvent(scrollEv);
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Scroll event dispatched with delta (" + std::to_string(deltaX) + ", " + std::to_string(deltaY) + ")";
    return res;
}

} // namespace modules::devtools
