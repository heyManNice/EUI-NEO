#pragma once

#include "core/tooling/config.h"

#if EUI_TOOLING_ENABLED

#include "core/input/input_types.h"
#include "core/render/render_types.h"
#include "core/tooling/model.h"
#include "core/tooling/pass.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#endif

namespace core::dsl::runtime {

#if EUI_TOOLING_ENABLED

enum class ToolingInputKind {
    PointerPress,
    PointerRelease,
    PointerClick,
    Scroll,
    KeyDown,
    KeyUp,
    TextInput,
    FocusChange
};

struct ToolingInputRecord {
    double timestamp = 0.0;
    ToolingInputKind kind = ToolingInputKind::PointerPress;
    std::string targetId;
    std::string detail;
    float x = 0.0f;
    float y = 0.0f;
    PointerButton button = PointerButton::None;
    core::InputKey key = core::InputKey::Unknown;
    std::string text;
};

struct ToolingHitEntry {
    std::string id;
    core::dsl::ElementKind kind = core::dsl::ElementKind::Row;
    bool interactive = false;
    bool disabled = false;
    bool focusable = false;
    core::dsl::HitTestMode hitTestMode = core::dsl::HitTestMode::Layout;
    core::Rect frame;
};

// Everything a tool keeps on a runtime, in one place.
//
// A runtime owns it through a pointer that stays null until a tool first talks to the
// runtime, so an app (or a build) without tools carries one pointer and nothing else:
// no override table, no cached inspection path, no overlay primitive, no input filter.
// The runtime never asks whether tools are part of the build, it asks the seam whether
// one is attached (core/tooling/hooks.h).
struct ToolingState {
    // The element a tool previews, with the cached path to it. Element pointers only
    // live until the next compose, so the path is keyed by the compose generation.
    ElementMark hovered;
    std::uint64_t composeGeneration = 0;

    // What a tool does between a compose and the layout that follows it: the moment to put
    // back the values it replaced, before the layout reads them. The tool owns whatever it
    // remembers; the runtime owns the moment.
    std::function<void()> afterCompose;

    // How a tool draws on top of the page from inside the page render pass. The tool owns
    // whatever graphics objects it draws with, so nothing here has to be released with the
    // device; the pass only hands it the backend and the resolved geometry.
    std::function<void(const RenderPassContext&)> passRenderer;

    // The input a tool takes away from the page, and the queue a host pushes for a
    // runtime that has no window of its own.
    std::function<void(std::vector<PointerEvent>&, ScrollEvent&)> inputFilter;
    std::vector<PointerEvent> hostPointerEvents;
    ScrollEvent hostScrollEvent;

    // How the tool draws its own overlay. It runs inside the page render pass, so the
    // overlay becomes part of the cached frame the window blits.
    std::function<void(int, int, float, const Rect*)> overlayRenderer;

    // Live input stream history and event counters
    static constexpr std::size_t kMaxInputRecords = 50;
    std::vector<ToolingInputRecord> inputHistory;
    std::uint64_t inputEventCount = 0;

    void recordInput(ToolingInputRecord record) {
        inputEventCount++;
        if (inputHistory.size() >= kMaxInputRecords) {
            inputHistory.erase(inputHistory.begin());
        }
        inputHistory.push_back(std::move(record));
    }
};
#else
// No tooling in this build. The type stays so that a runtime has the same layout in
// every configuration: one pointer, and nothing behind it.
struct ToolingState {};
#endif

} // namespace core::dsl::runtime
