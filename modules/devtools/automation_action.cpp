#include "modules/devtools/automation_action.h"

#include <algorithm>

namespace modules::devtools {

McpActionResult clickElement(core::dsl::Runtime& runtime, const std::string& elementId, bool force) {
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

    if (el->disabled && !force) {
        res.success = false;
        res.message = "Element '" + elementId + "' is disabled. Use --force to override.";
        return res;
    }

    const double centerX = static_cast<double>(bounds.x + bounds.width * 0.5f);
    const double centerY = static_cast<double>(bounds.y + bounds.height * 0.5f);

    // Hit testing / occlusion check: check what element would receive a pointer event at this point
    auto hitChain = runtime.hitTestChain(static_cast<float>(centerX), static_cast<float>(centerY), 1.0f);
    std::string hitTarget;
    for (const auto& entry : hitChain) {
        if (entry.interactive && !entry.disabled) {
            hitTarget = entry.id;
            break;
        }
    }

    bool isOccluded = false;
    if (!hitTarget.empty() && hitTarget != elementId) {
        // Target is considered matching if it is the element itself, its hit target, or a descendant
        bool isSubOrHit = (hitTarget == elementId + ".hit") ||
                          (hitTarget.rfind(elementId + ".", 0) == 0) ||
                          (elementId.rfind(hitTarget + ".", 0) == 0);
        if (!isSubOrHit) {
            isOccluded = true;
            res.occludedBy = hitTarget;
        }
    }

    if (isOccluded && !force) {
        res.success = false;
        res.message = "Element '" + elementId + "' is occluded by '" + res.occludedBy + "'. Use --force to override.";
        return res;
    }

    // Direct invocation if onClick callback is bound
    if (el->onClick) {
        el->onClick();
        runtime.requestFullPaint();
        res.success = true;
        res.message = "Element '" + elementId + "' onClick callback invoked successfully";
        if (isOccluded) {
            res.message += " (warning: element was occluded by '" + res.occludedBy + "')";
        }
        return res;
    }

    // Otherwise, simulate pointer press and release at center coordinates
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
    if (isOccluded) {
        res.message += " (warning: element was occluded by '" + res.occludedBy + "')";
    }
    return res;
}

McpActionResult clickMark(core::dsl::Runtime& runtime, int markIndex, const std::vector<McpInteractiveElement>& marks, bool force) {
    for (const auto& mark : marks) {
        if (mark.markIndex == markIndex) {
            return clickElement(runtime, mark.id, force);
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
    runtime.setFocusedId(el->id);
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
        if (el == nullptr) {
            res.success = false;
            res.message = "Element '" + elementId + "' not found";
            return res;
        }
    } else {
        // No element specified: check currently focused element
        if (!runtime.focusedId().empty()) {
            el = runtime.findElement(runtime.focusedId());
            if (el == nullptr) {
                std::string hitCandidate = runtime.focusedId() + ".hit";
                el = runtime.findElement(hitCandidate);
            }
        }

        // If no element currently has focus, search for any active focusable / input / key-event element
        if (el == nullptr) {
            auto findInputRecursive = [](auto& self, const core::dsl::Element& element) -> core::dsl::Element* {
                if (!element.disabled && (element.onKeyEvent != nullptr || element.onTextInput != nullptr || element.focusable)) {
                    return const_cast<core::dsl::Element*>(&element);
                }
                for (const auto* child : element.orderedChildren) {
                    if (child != nullptr) {
                        core::dsl::Element* found = self(self, *child);
                        if (found != nullptr) return found;
                    }
                }
                return nullptr;
            };

            for (const auto* root : runtime.elementRoots()) {
                if (root != nullptr) {
                    el = findInputRecursive(findInputRecursive, *root);
                    if (el != nullptr) break;
                }
            }
        }

        if (el == nullptr) {
            res.success = false;
            res.message = "No focused or input element found in the page to receive key '" + keyName + "'";
            return res;
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

    core::KeyEvent keyRelease = keyPress;
    keyRelease.action = core::KeyAction::Release;

    core::dsl::Element* targetReceiver = el;
    bool dispatched = false;

    if (targetReceiver->onKeyEvent && !targetReceiver->disabled) {
        dispatched = targetReceiver->onKeyEvent(keyPress);
        targetReceiver->onKeyEvent(keyRelease);
    } else {
        // Try hit candidate
        std::string hitId = targetReceiver->id + ".hit";
        core::dsl::Element* hitEl = runtime.findElement(hitId);
        if (hitEl != nullptr && hitEl->onKeyEvent && !hitEl->disabled) {
            targetReceiver = hitEl;
            dispatched = hitEl->onKeyEvent(keyPress);
            hitEl->onKeyEvent(keyRelease);
        }
    }

    res.targetBounds = core::Rect{targetReceiver->frame.x, targetReceiver->frame.y, targetReceiver->frame.width, targetReceiver->frame.height};
    runtime.requestElementRefresh();
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Key '" + keyName + "' dispatched to '" + targetReceiver->id + "' (handled=" + (dispatched ? "true" : "false") + ")";
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

// The runtime picks the scroll receiver by hit testing the last pointer event, so the
// element a scroll reaches is the nearest scroll host under the pointer. The same rule
// decides here which element a scroll would reach.
static bool isScrollHost(const core::dsl::Element& element) {
    return !element.disabled && (!element.scrollStateId.empty() || static_cast<bool>(element.onScroll));
}

// One walk finds the target and, unwinding back out, the nearest scroll host above it.
static bool locateScrollHost(const core::dsl::Element& element,
                             const std::string& targetId,
                             const core::dsl::Element*& host) {
    const bool selfIsHost = isScrollHost(element);
    if (element.id == targetId) {
        if (selfIsHost) {
            host = &element;
        }
        return true;
    }
    for (const auto* child : element.orderedChildren) {
        if (child == nullptr) continue;
        if (locateScrollHost(*child, targetId, host)) {
            if (host == nullptr && selfIsHost) {
                host = &element;
            }
            return true;
        }
    }
    return false;
}

// Named in the failure message so a caller pointing at a wrapper learns what to target.
static std::string firstScrollHostBelow(const core::dsl::Element& element) {
    if (!element.disabled && (!element.scrollStateId.empty() || element.onScroll)) {
        return element.id;
    }
    for (const auto* child : element.orderedChildren) {
        if (child == nullptr) continue;
        const std::string found = firstScrollHostBelow(*child);
        if (!found.empty()) return found;
    }
    return {};
}

McpActionResult scrollElement(core::dsl::Runtime& runtime, const std::string& elementId, float deltaX, float deltaY) {
    McpActionResult res;

    // No target keeps the original meaning: scroll wherever the pointer already is.
    if (elementId.empty()) {
        core::ScrollEvent scrollEv;
        scrollEv.x = static_cast<double>(deltaX);
        scrollEv.y = static_cast<double>(deltaY);
        runtime.pushScrollEvent(scrollEv);
        runtime.requestFullPaint();

        res.success = true;
        res.message = "Scroll delta (" + std::to_string(deltaX) + ", " + std::to_string(deltaY) +
                      ") dispatched at the current pointer position (no target given)";
        return res;
    }

    core::dsl::Element* el = runtime.findElement(elementId);
    if (el == nullptr) {
        std::string hitCandidate = elementId + ".hit";
        el = runtime.findElement(hitCandidate);
        if (el == nullptr && elementId.size() > 5 && elementId.substr(elementId.size() - 5) == ".text") {
            hitCandidate = elementId.substr(0, elementId.size() - 5) + ".hit";
            el = runtime.findElement(hitCandidate);
        }
    }
    if (el == nullptr) {
        res.success = false;
        res.message = "Element '" + elementId + "' not found to scroll";
        return res;
    }

    const core::Rect bounds{el->frame.x, el->frame.y, el->frame.width, el->frame.height};
    res.targetBounds = bounds;

    if (el->disabled) {
        res.success = false;
        res.message = "Element '" + el->id + "' is disabled";
        return res;
    }

    const core::dsl::Element* host = nullptr;
    bool inTree = false;
    for (const auto* root : runtime.elementRoots()) {
        if (root != nullptr && locateScrollHost(*root, el->id, host)) {
            inTree = true;
            break;
        }
    }
    if (!inTree) {
        res.success = false;
        res.message = "Element '" + el->id + "' is not part of the composed tree";
        return res;
    }
    if (host == nullptr) {
        const std::string below = firstScrollHostBelow(*el);
        res.success = false;
        res.message = "Nothing scrollable at or above '" + el->id + "'" +
                      (below.empty() ? "" : "; it contains a scroll host, target '" + below + "' instead");
        return res;
    }

    // Move the pointer over the target so the runtime resolves the scroll to it.
    core::PointerEvent moveEv;
    moveEv.action = core::PointerAction::Move;
    moveEv.x = static_cast<double>(bounds.x + bounds.width * 0.5f);
    moveEv.y = static_cast<double>(bounds.y + bounds.height * 0.5f);
    runtime.pushPointerEvent(moveEv);

    core::ScrollEvent scrollEv;
    scrollEv.x = static_cast<double>(deltaX);
    scrollEv.y = static_cast<double>(deltaY);
    runtime.pushScrollEvent(scrollEv);
    runtime.requestFullPaint();

    res.success = true;
    res.message = "Scroll delta (" + std::to_string(deltaX) + ", " + std::to_string(deltaY) +
                  ") dispatched to '" + host->id + "' with the pointer over '" + el->id + "'";
    return res;
}

} // namespace modules::devtools
