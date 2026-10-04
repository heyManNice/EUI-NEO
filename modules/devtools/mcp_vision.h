#pragma once

#include "core/dsl.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace modules::devtools {

struct FramebufferImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    // Which frame this is, from the host cache it was read out of. A capture waits for a frame
    // newer than the one already cached, so two captures either side of an action can be told
    // apart by it; a capture made on the render thread reads the backend directly and leaves
    // this at zero, there being no cache to date the frame against.
    std::uint64_t generation = 0;

    bool valid() const {
        return width > 0 && height > 0 && rgba.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

// Capture current viewport framebuffer or a specific region (in physical framebuffer pixels).
// With drawMarks the Set-of-Mark overlay is asked for before the capture and taken down after it,
// so the frame that comes back carries a box and a mark index on every interactive element. The
// overlay is drawn by the page's render pass, which a capture read straight off the backend on the
// render thread does not go through, so that path returns an unmarked frame.
FramebufferImage captureViewportScreenshot(const ::core::Rect* region = nullptr, bool drawMarks = false);

// Capture screenshot cropped to a specific element by ID in the runtime
FramebufferImage captureElementScreenshot(const ::core::dsl::Runtime& runtime, const std::string& elementId, float dpiScale = 1.0f);

// Encode raw RGBA buffer to PNG in memory
std::vector<unsigned char> encodeRgbaToPng(const unsigned char* rgba, int width, int height);
std::vector<unsigned char> encodeImageToPng(const FramebufferImage& img);

// Base64 encoding
std::string encodeBase64(const unsigned char* data, std::size_t len);
std::string encodeBase64(const std::vector<unsigned char>& data);

// Helper to get Data URI for AI (e.g. "data:image/png;base64,...")
std::string encodePngDataUri(const FramebufferImage& img);

} // namespace modules::devtools
