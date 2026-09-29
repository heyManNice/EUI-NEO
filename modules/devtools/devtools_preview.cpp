#include "modules/devtools/devtools_preview.h"

#include "core/render/render_backend.h"
#include "core/render/text.h"
#include "core/runtime/runtime_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

// The one pixel ring just inside a frame, as disjoint rectangles.
PreviewBand outlineBand(const core::Rect& frame) {
    constexpr float kThickness = 1.0f;
    const core::Rect inner{frame.x + kThickness,
                           frame.y + kThickness,
                           std::max(0.0f, frame.width - kThickness * 2.0f),
                           std::max(0.0f, frame.height - kThickness * 2.0f)};
    return previewBand(frame, inner);
}

core::Color elementBoundsColor(int depth) {
    constexpr std::size_t kCount = sizeof(kElementBoundsColors) / sizeof(kElementBoundsColors[0]);
    return kElementBoundsColors[static_cast<std::size_t>(std::max(0, depth)) % kCount];
}

void drawElementBounds(const std::vector<ElementBounds>& bounds,
                       const core::dsl::runtime::RenderPassContext& pass,
                       core::RoundedRectPrimitive& primitive) {
    if (pass.backend == nullptr) {
        return;
    }

    // These rings are page geometry, not one element's box, so the clip they need is the
    // pass's own and not the clip some element left behind.
    pass.clipToNothing();

    const auto paint = [&](const core::Rect& rect, const core::Color& color) {
        primitive.setBounds(rect.x, rect.y, rect.width, rect.height);
        primitive.setColor(color);
        primitive.setGradient({});
        primitive.setBorder({});
        primitive.setShadow({});
        primitive.setCornerRadius(0.0f);
        primitive.setBlur(0.0f);
        primitive.setOpacity(1.0f);
        primitive.setTransformMatrix(core::dsl::combinedPrimitiveMatrix(core::dsl::RenderTransform{}, rect,
                                                                        core::Transform{}));
        ++core::render::currentRenderFrameStats().rectDraws;
        primitive.render(pass.windowWidth, pass.windowHeight);
    };

    for (const ElementBounds& element : bounds) {
        const core::Rect pixel = core::dsl::toPixelRect(element.frame, pass.dpiScale);
        if (pixel.width <= 0.0f || pixel.height <= 0.0f) {
            continue;
        }
        const core::Color color = elementBoundsColor(element.depth);
        const PreviewBand ring = outlineBand(pixel);
        for (int index = 0; index < ring.count; ++index) {
            paint(ring.rects[index], color);
        }
    }

    // Leave the backend scissor to the pass's next draw, like page content does.
    pass.clipToNothing();
}

void drawBoxPreview(const core::dsl::runtime::ElementBox& box,
                    const core::dsl::runtime::RenderPassContext& pass,
                    const BoxPreviewPalette& palette,
                    core::RoundedRectPrimitive& primitive,
                    core::TextPrimitive* textPrimitive) {
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

    // Leave the backend scissor for the floating badge and subsequent draws.
    pass.clipToNothing();

    // Floating coordinate badge at the element's bottom-left corner.
    if (textPrimitive != nullptr) {
        char label[128];
        if (std::floor(box.frame.width) == box.frame.width &&
            std::floor(box.frame.height) == box.frame.height &&
            std::floor(box.frame.x) == box.frame.x &&
            std::floor(box.frame.y) == box.frame.y) {
            std::snprintf(label, sizeof(label), "%.0f × %.0f  (x: %.0f, y: %.0f)",
                          box.frame.width, box.frame.height, box.frame.x, box.frame.y);
        } else {
            std::snprintf(label, sizeof(label), "%.1f × %.1f  (x: %.1f, y: %.1f)",
                          box.frame.width, box.frame.height, box.frame.x, box.frame.y);
        }

        const float fontSize = 11.0f * dpiScale;
        const float textWidth = core::TextPrimitive::measureTextWidth(label, {}, fontSize, 500);
        const float lineHeight = fontSize * 1.25f;
        const float padX = 7.0f * dpiScale;
        const float padY = 3.5f * dpiScale;
        const float badgeWidth = textWidth + padX * 2.0f;
        const float badgeHeight = lineHeight + padY * 2.0f;

        const core::Rect screenBox = box.transform.active
            ? core::dsl::applyRenderTransform(frame, box.transform)
            : frame;
        const float gap = 4.0f * dpiScale;
        float badgeX = screenBox.x;
        float badgeY = screenBox.y + screenBox.height + gap;

        // If overflowing the bottom of the window, flip to the top of the element.
        if (badgeY + badgeHeight > static_cast<float>(pass.windowHeight) - gap) {
            badgeY = screenBox.y - badgeHeight - gap;
        }

        badgeX = std::clamp(badgeX, gap, std::max(gap, static_cast<float>(pass.windowWidth) - badgeWidth - gap));
        badgeY = std::clamp(badgeY, gap, std::max(gap, static_cast<float>(pass.windowHeight) - badgeHeight - gap));

        // Background pill
        primitive.setBounds(badgeX, badgeY, badgeWidth, badgeHeight);
        primitive.setColor(core::Color{0.10f, 0.10f, 0.13f, 0.92f});
        primitive.setCornerRadius(3.5f * dpiScale);
        primitive.setBorder(core::Border{1.0f * dpiScale, core::Color{0.32f, 0.35f, 0.42f, 0.70f}});
        primitive.setShadow(core::Shadow{true, {0.0f, 2.0f * dpiScale}, 6.0f * dpiScale, 0.0f, {0.0f, 0.0f, 0.0f, 0.35f}, false});
        primitive.setGradient({});
        primitive.setBlur(0.0f);
        primitive.setOpacity(1.0f);
        primitive.setTransformMatrix(core::TransformMatrix{});
        ++core::render::currentRenderFrameStats().rectDraws;
        primitive.render(pass.windowWidth, pass.windowHeight);

        // Badge text
        textPrimitive->setPosition(badgeX + padX, badgeY + padY);
        textPrimitive->setText(label);
        textPrimitive->setFontSize(fontSize);
        textPrimitive->setFontWeight(500);
        textPrimitive->setColor(core::Color{0.96f, 0.96f, 0.98f, 1.0f});
        textPrimitive->setMaxWidth(0.0f);
        textPrimitive->setWrap(false);
        textPrimitive->setHorizontalAlign(core::HorizontalAlign::Left);
        textPrimitive->setVerticalAlign(core::VerticalAlign::Top);
        textPrimitive->setLineHeight(lineHeight);
        textPrimitive->setTransformMatrix(core::TransformMatrix{});
        textPrimitive->prepare();
        ++core::render::currentRenderFrameStats().textDraws;
        textPrimitive->render(pass.windowWidth, pass.windowHeight);
    }
}

} // namespace modules::devtools
