#include "modules/devtools/devtools_preview.h"

#include "core/render/render_backend.h"
#include "core/runtime/runtime_geometry.h"

#include <algorithm>

namespace modules::devtools {

PreviewBand previewBand(const core::Rect& outer, const core::Rect& inner) {
    PreviewBand band;
    const float outerRight = outer.x + outer.width;
    const float outerBottom = outer.y + outer.height;
    float top = inner.y - outer.y;
    if (top < 0.0f) {
        top = 0.0f;
    } else if (top > outer.height) {
        top = outer.height;
    }
    float bottom = outerBottom - (inner.y + inner.height);
    if (bottom < 0.0f) {
        bottom = 0.0f;
    } else if (bottom > outer.height - top) {
        bottom = outer.height - top;
    }
    const float middle = outer.height - top - bottom;
    float left = inner.x - outer.x;
    if (left < 0.0f) {
        left = 0.0f;
    }
    float right = outerRight - (inner.x + inner.width);
    if (right < 0.0f) {
        right = 0.0f;
    }
    if (top > 0.0f) {
        band.rects[band.count++] = {outer.x, outer.y, outer.width, top};
    }
    if (bottom > 0.0f) {
        band.rects[band.count++] = {outer.x, outer.y + top + middle, outer.width, bottom};
    }
    if (middle > 0.0f) {
        if (left > 0.0f) {
            band.rects[band.count++] = {outer.x, outer.y + top, left, middle};
        }
        if (right > 0.0f) {
            band.rects[band.count++] = {outerRight - right, outer.y + top, right, middle};
        }
    }
    return band;
}

void drawBoxPreview(const core::dsl::runtime::ElementBox& box,
                    const core::dsl::runtime::RenderPassContext& pass,
                    const BoxPreviewPalette& palette,
                    core::RoundedRectPrimitive& primitive) {
    if (!box.active || pass.backend == nullptr) {
        return;
    }

    // The clip of the element's ancestors, applied the way page content applies it: a
    // preview inside a scrolled or clipped container has to be cut by the same rectangle
    // the element itself is cut by, or it would show outside its own container.
    if (box.hasScissor) {
        pass.clipTo(box.scissor);
    } else {
        pass.clipToNothing();
    }

    const float dpiScale = pass.dpiScale;
    const core::Rect frame = core::dsl::toPixelRect(box.frame, dpiScale);
    const auto insetBox = [dpiScale](const core::Rect& rect, const core::EdgeInsets& insets) {
        const float left = core::dsl::toPixels(insets.left, dpiScale);
        const float top = core::dsl::toPixels(insets.top, dpiScale);
        const float right = core::dsl::toPixels(insets.right, dpiScale);
        const float bottom = core::dsl::toPixels(insets.bottom, dpiScale);
        return core::Rect{rect.x + left,
                          rect.y + top,
                          std::max(0.0f, rect.width - left - right),
                          std::max(0.0f, rect.height - top - bottom)};
    };
    const auto expandedBox = [dpiScale](const core::Rect& rect, const core::EdgeInsets& insets) {
        const float left = core::dsl::toPixels(insets.left, dpiScale);
        const float top = core::dsl::toPixels(insets.top, dpiScale);
        const float right = core::dsl::toPixels(insets.right, dpiScale);
        const float bottom = core::dsl::toPixels(insets.bottom, dpiScale);
        return core::Rect{rect.x - left, rect.y - top, rect.width + left + right, rect.height + top + bottom};
    };

    // The border is painted inside the box and padding is measured from the box edge, so
    // the padding band reaches inward from whichever of the two is wider.
    const core::EdgeInsets border =
        core::EdgeInsets::all(std::max(0.0f, core::dsl::toPixels(box.borderWidth, dpiScale)));
    const core::EdgeInsets contentInset{
        std::max(border.left, core::dsl::toPixels(box.padding.left, dpiScale)),
        std::max(border.top, core::dsl::toPixels(box.padding.top, dpiScale)),
        std::max(border.right, core::dsl::toPixels(box.padding.right, dpiScale)),
        std::max(border.bottom, core::dsl::toPixels(box.padding.bottom, dpiScale))
    };
    const core::Rect borderBox = insetBox(frame, border);
    const core::Rect contentBox = insetBox(frame, contentInset);

    // No stroke anywhere: the browser look is translucent fills only. The box stays in the
    // element's own space, like a page rect: the primitive matrix carries the transform,
    // so the wash lands where the element is drawn even inside a transformed ancestor.
    const auto paint = [&](const core::Rect& rect, const core::Color& fill) {
        primitive.setBounds(rect.x, rect.y, rect.width, rect.height);
        primitive.setColor(fill);
        primitive.setGradient({});
        primitive.setBorder({});
        primitive.setShadow({});
        primitive.setCornerRadius(0.0f);
        primitive.setBlur(0.0f);
        primitive.setOpacity(1.0f);
        primitive.setTransformMatrix(core::dsl::combinedPrimitiveMatrix(box.transform, rect, core::Transform{}));
        ++core::render::currentRenderFrameStats().rectDraws;
        primitive.render(pass.windowWidth, pass.windowHeight);
    };
    const auto paintBand = [&](const core::Rect& outer, const core::Rect& inner, const core::Color& fill) {
        const PreviewBand band = previewBand(outer, inner);
        for (int index = 0; index < band.count; ++index) {
            paint(band.rects[index], fill);
        }
    };

    // Outermost first. The regions are disjoint, so no colour ever blends twice.
    paintBand(expandedBox(frame, box.margin), frame, palette.margin);
    paintBand(frame, borderBox, palette.border);
    paintBand(borderBox, contentBox, palette.padding);
    if (contentBox.width > 0.0f && contentBox.height > 0.0f) {
        paint(contentBox, palette.content);
    }

    // Leave the backend scissor to the pass's next draw, like page content does.
    pass.clipToNothing();
}

} // namespace modules::devtools
