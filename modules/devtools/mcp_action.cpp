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

static core::InputKey parseKeyName(const std::string& name) {
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "enter" || lower == "return") return core::InputKey::Enter;
    if (lower == "backspace") return core::InputKey::Backspace;
    if (lower == "escape" || lower == "esc") return core::InputKey::Escape;
    if (lower == "tab") return core::InputKey::Tab;
    if (lower == "space") return core::InputKey::Space;
    if (lower == "delete" || lower == "del") return core::InputKey::Delete;
    if (lower == "home") return core::InputKey::Home;
    if (lower == "end") return core::InputKey::End;
    if (lower == "up") return core::InputKey::Up;
    if (lower == "down") return core::InputKey::Down;
    if (lower == "left") return core::InputKey::Left;
    if (lower == "right") return core::InputKey::Right;
    if (lower == "pageup") return core::InputKey::PageUp;
    if (lower == "pagedown") return core::InputKey::PageDown;
    if (lower == "a") return core::InputKey::A;
    if (lower == "c") return core::InputKey::C;
    if (lower == "v") return core::InputKey::V;
    if (lower == "x") return core::InputKey::X;
    if (lower == "z") return core::InputKey::Z;
    if (lower == "y") return core::InputKey::Y;
    return core::InputKey::Unknown;
}

McpActionResult focusElement(core::dsl::Runtime& runtime, const std::string& elementId) {
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
            res.message = "Element '" + elementId + "' not found to focus";
            return res;
        }
    }

    const core::Rect bounds{el->frame.x, el->frame.y, el->frame.width, el->frame.height};
    res.targetBounds = bounds;

    // Trigger focus by simulating click / pointer event or direct callback
    if (el->onPress) {
        core::PointerEvent pressEv;
        pressEv.action = core::PointerAction::Press;
        pressEv.button = core::PointerButton::Left;
        pressEv.buttons = core::PointerButtons{core::PointerButton::Left};
        pressEv.x = bounds.x + bounds.width * 0.5;
        pressEv.y = bounds.y + bounds.height * 0.5;
        el->onPress(pressEv, bounds);
    }
    if (el->onClick) {
        el->onClick();
    }
    if (el->onFocusChanged) {
        el->onFocusChanged(true);
    }
    runtime.requestElementRefresh();
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Element '" + el->id + "' focused successfully";
    return res;
}

McpActionResult pressKey(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& keyName, bool ctrl, bool shift, bool alt) {
    McpActionResult res;
    core::dsl::Element* el = nullptr;
    if (!elementId.empty()) {
        el = runtime.findElement(elementId);
        if (el == nullptr) {
            std::string hitCandidate = elementId + ".hit";
            el = runtime.findElement(hitCandidate);
        }
        if (el == nullptr && elementId.size() > 5 && elementId.substr(elementId.size() - 5) == ".text") {
            std::string hitCandidate = elementId.substr(0, elementId.size() - 5) + ".hit";
            el = runtime.findElement(hitCandidate);
        }
    }

    core::InputKey key = parseKeyName(keyName);
    if (key == core::InputKey::Unknown) {
        res.success = false;
        res.message = "Unsupported or unknown key: " + keyName;
        return res;
    }

    core::KeyEvent keyPress;
    keyPress.key = key;
    keyPress.action = core::KeyAction::Press;
    keyPress.modifiers.control = ctrl;
    keyPress.modifiers.shift = shift;
    keyPress.modifiers.alt = alt;

    bool dispatched = false;
    if (el != nullptr && el->onKeyEvent && !el->disabled) {
        dispatched = el->onKeyEvent(keyPress);
    }

    // If target has no direct onKeyEvent or wasn't specified, search for focused or input hit element
    if (!dispatched && el != nullptr) {
        // Try children or siblings
        std::string hitId = el->id + ".hit";
        core::dsl::Element* hitEl = runtime.findElement(hitId);
        if (hitEl != nullptr && hitEl->onKeyEvent && !hitEl->disabled) {
            dispatched = hitEl->onKeyEvent(keyPress);
        }
    }

    runtime.requestElementRefresh();
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Key '" + keyName + "' dispatched (handled=" + (dispatched ? "true" : "false") + ")";
    return res;
}

McpActionResult inputText(core::dsl::Runtime& runtime, const std::string& elementId, const std::string& text, const std::string& mode, bool clearFirst) {
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

    // Check if element supports text input
    bool hasTextInput = (el->onTextInput != nullptr);
    core::dsl::Element* inputReceiver = el;
    if (!hasTextInput) {
        // Check if there is a child or .hit element that has onTextInput
        std::string hitId = el->id + ".hit";
        core::dsl::Element* hitEl = runtime.findElement(hitId);
        if (hitEl != nullptr && hitEl->onTextInput) {
            inputReceiver = hitEl;
            hasTextInput = true;
        }
    }

    // If it is a pure Text element without text input callback
    if (el->kind == core::dsl::ElementKind::Text && !hasTextInput) {
        if (mode == "append" && !clearFirst) {
            el->text += text;
        } else {
            el->text = text;
        }
        runtime.requestElementRefresh();
        runtime.requestFullPaint();
        res.success = true;
        res.message = "Element text updated to: " + el->text;
        return res;
    }

    if (!hasTextInput) {
        // Strict check: if element has no onTextInput callback, return clear failure instead of fake success
        res.success = false;
        res.message = "Element '" + targetId + "' has no onTextInput handler and cannot receive text input";
        return res;
    }

    if (inputReceiver->disabled) {
        res.success = false;
        res.message = "Element '" + inputReceiver->id + "' is disabled";
        return res;
    }

    // Focus the receiver first
    if (inputReceiver->onPress) {
        core::PointerEvent pressEv;
        pressEv.action = core::PointerAction::Press;
        pressEv.button = core::PointerButton::Left;
        pressEv.x = bounds.x + bounds.width * 0.5;
        pressEv.y = bounds.y + bounds.height * 0.5;
        inputReceiver->onPress(pressEv, bounds);
    }
    if (inputReceiver->onFocusChanged) {
        inputReceiver->onFocusChanged(true);
    }

    // If mode is "replace" or clearFirst is requested, dispatch Ctrl+A then Backspace via onKeyEvent
    if (mode == "replace" || clearFirst) {
        if (inputReceiver->onKeyEvent) {
            core::KeyEvent selectAllEv;
            selectAllEv.key = core::InputKey::A;
            selectAllEv.action = core::KeyAction::Press;
            selectAllEv.modifiers.control = true;
            inputReceiver->onKeyEvent(selectAllEv);

            core::KeyEvent backspaceEv;
            backspaceEv.key = core::InputKey::Backspace;
            backspaceEv.action = core::KeyAction::Press;
            inputReceiver->onKeyEvent(backspaceEv);
        }
    }

    core::TextInputEvent inputEv;
    inputEv.text = text;
    inputReceiver->onTextInput(inputEv);

    runtime.requestElementRefresh();
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Input text ('" + text + "') applied on element '" + inputReceiver->id + "' with mode '" + mode + "'";
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
