#include "modules/devtools/mcp_vision.h"

#include "core/dsl_runtime.h"
#include "core/render/render_backend.h"
#include "modules/devtools/host.h"

#include <png.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>

namespace modules::devtools {

namespace {

struct PngMemoryBuffer {
    std::vector<unsigned char> data;
};

void pngMemoryWriteCallback(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* buf = static_cast<PngMemoryBuffer*>(png_get_io_ptr(png_ptr));
    buf->data.insert(buf->data.end(), data, data + length);
}

void pngMemoryFlushCallback(png_structp png_ptr) {
    (void)png_ptr;
}

} // namespace

FramebufferImage captureViewportScreenshot(const core::Rect* region) {
    core::render::RenderBackend* backend = core::render::activeRenderBackend();
    
    // If we have an active backend on this thread, read directly
    if (backend != nullptr) {
        const int fbW = backend->framebufferWidth();
        const int fbH = backend->framebufferHeight();
        if (fbW <= 0 || fbH <= 0) {
            return {};
        }

        int rx = 0;
        int ry = 0;
        int rw = fbW;
        int rh = fbH;

        if (region != nullptr) {
            rx = std::clamp(static_cast<int>(std::floor(region->x)), 0, fbW);
            ry = std::clamp(static_cast<int>(std::floor(region->y)), 0, fbH);
            rw = std::clamp(static_cast<int>(std::ceil(region->width)), 0, fbW - rx);
            rh = std::clamp(static_cast<int>(std::ceil(region->height)), 0, fbH - ry);
        }

        if (rw <= 0 || rh <= 0) {
            return {};
        }

        FramebufferImage img;
        img.width = rw;
        img.height = rh;
        img.rgba.resize(static_cast<std::size_t>(rw) * rh * 4u);

        if (!backend->readFramebufferPixels(rx, ry, rw, rh, img.rgba.data())) {
            return {};
        }
        return img;
    }

    // Otherwise, we are likely on the MCP worker thread:
    // Request render pass to capture framebuffer and wait briefly
    DevtoolsHost& host = devtoolsHostInstance();
    host.requestFramebufferCapture();

    int fullW = 0;
    int fullH = 0;
    std::vector<unsigned char> fullPixels;

    for (int i = 0; i < 30; ++i) {
        if (host.getCachedFramebuffer(fullW, fullH, fullPixels)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    if (fullW <= 0 || fullH <= 0 || fullPixels.empty()) {
        return {};
    }

    if (region == nullptr) {
        FramebufferImage img;
        img.width = fullW;
        img.height = fullH;
        img.rgba = std::move(fullPixels);
        return img;
    }

    int rx = std::clamp(static_cast<int>(std::floor(region->x)), 0, fullW);
    int ry = std::clamp(static_cast<int>(std::floor(region->y)), 0, fullH);
    int rw = std::clamp(static_cast<int>(std::ceil(region->width)), 0, fullW - rx);
    int rh = std::clamp(static_cast<int>(std::ceil(region->height)), 0, fullH - ry);

    if (rw <= 0 || rh <= 0) {
        return {};
    }

    FramebufferImage img;
    img.width = rw;
    img.height = rh;
    img.rgba.resize(static_cast<std::size_t>(rw) * rh * 4u);

    for (int row = 0; row < rh; ++row) {
        const std::size_t srcOffset = (static_cast<std::size_t>(ry + row) * fullW + rx) * 4u;
        const std::size_t dstOffset = (static_cast<std::size_t>(row) * rw) * 4u;
        std::memcpy(&img.rgba[dstOffset], &fullPixels[srcOffset], static_cast<std::size_t>(rw) * 4u);
    }

    return img;
}

FramebufferImage captureElementScreenshot(const core::dsl::Runtime& runtime, const std::string& elementId, float dpiScale) {
    const core::dsl::Element* el = runtime.findElement(elementId);
    if (el == nullptr) {
        return {};
    }
    const float scale = dpiScale > 0.0f ? dpiScale : 1.0f;
    const core::Rect physRect{
        el->frame.x * scale,
        el->frame.y * scale,
        el->frame.width * scale,
        el->frame.height * scale
    };
    return captureViewportScreenshot(&physRect);
}

std::vector<unsigned char> encodeRgbaToPng(const unsigned char* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return {};
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        return {};
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        return {};
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return {};
    }

    PngMemoryBuffer buffer;
    png_set_write_fn(png_ptr, &buffer, pngMemoryWriteCallback, pngMemoryFlushCallback);

    png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(width), static_cast<png_uint_32>(height),
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    std::vector<png_bytep> rowPointers(height);
    for (int y = 0; y < height; ++y) {
        rowPointers[y] = const_cast<png_bytep>(rgba + static_cast<std::size_t>(y) * width * 4u);
    }
    png_write_image(png_ptr, rowPointers.data());
    png_write_end(png_ptr, nullptr);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    return buffer.data;
}

std::vector<unsigned char> encodeImageToPng(const FramebufferImage& img) {
    if (!img.valid()) {
        return {};
    }
    return encodeRgbaToPng(img.rgba.data(), img.width, img.height);
}

std::string encodeBase64(const unsigned char* data, std::size_t len) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    if (len == 0 || data == nullptr) {
        return out;
    }
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
        const std::uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kTable[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kTable[triple & 0x3F] : '=');
    }
    return out;
}

std::string encodeBase64(const std::vector<unsigned char>& data) {
    return encodeBase64(data.data(), data.size());
}

std::string encodePngDataUri(const FramebufferImage& img) {
    const std::vector<unsigned char> pngBytes = encodeImageToPng(img);
    if (pngBytes.empty()) {
        return {};
    }
    return "data:image/png;base64," + encodeBase64(pngBytes);
}

} // namespace modules::devtools
