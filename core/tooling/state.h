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
// Everything a tool keeps on a runtime, in one place.
//
// A runtime owns it through a pointer that stays null until a tool first talks to the
// runtime, so an app (or a build) without tools carries one pointer and nothing else:
// no override table, no cached inspection path, no overlay primitive, no input filter.
// The runtime never asks whether tools are part of the build, it asks the seam whether
// one is attached (core/tooling/hooks.h).
struct ToolingState {
    // Element values a tool wrote, keyed by element id. A compose builds every element
    // from the app's code again, so the seam re-applies these to the fresh tree before
    // layout runs.
    std::unordered_map<std::string, ElementPatch> patches;

    // The element a tool previews, with the cached path to it. Element pointers only
    // live until the next compose, so the path is keyed by the compose generation.
    ElementMark hovered;
    std::uint64_t composeGeneration = 0;

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
};
#else
// No tooling in this build. The type stays so that a runtime has the same layout in
// every configuration: one pointer, and nothing behind it.
struct ToolingState {};
#endif

} // namespace core::dsl::runtime
