#pragma once

#include "core/dsl.h"

#include <cstddef>
#include <string>
#include <vector>

namespace modules::devtools {

struct FramebufferImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    bool valid() const {
        return width > 0 && height > 0 && rgba.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

// Capture current viewport framebuffer or a specific region (in physical framebuffer pixels)
FramebufferImage captureViewportScreenshot(const core::Rect* region = nullptr);

// Capture screenshot cropped to a specific element by ID in the runtime
FramebufferImage captureElementScreenshot(const core::dsl::Runtime& runtime, const std::string& elementId, float dpiScale = 1.0f);

// Encode raw RGBA buffer to PNG in memory
std::vector<unsigned char> encodeRgbaToPng(const unsigned char* rgba, int width, int height);
std::vector<unsigned char> encodeImageToPng(const FramebufferImage& img);

// Base64 encoding
std::string encodeBase64(const unsigned char* data, std::size_t len);
std::string encodeBase64(const std::vector<unsigned char>& data);

// Helper to get Data URI for AI (e.g. "data:image/png;base64,...")
std::string encodePngDataUri(const FramebufferImage& img);

} // namespace modules::devtools
