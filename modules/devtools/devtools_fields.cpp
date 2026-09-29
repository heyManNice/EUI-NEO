#include "modules/devtools/devtools_fields.h"

#if defined(EUI_TOOLING)

namespace modules::devtools {

std::uint32_t ElementPatches::written(const std::string& id) const {
    const auto found = patches_.find(id);
    return found != patches_.end() ? found->second.mask : 0u;
}

const ElementPatch* ElementPatches::find(const std::string& id) const {
    const auto found = patches_.find(id);
    return found != patches_.end() ? &found->second : nullptr;
}

bool ElementPatches::set(const std::string& id, ElementField field, const FieldValue& value) {
    if (id.empty()) {
        return false;
    }
    return patches_[id].set(field, value);
}

void ElementPatches::clear(const std::string& id, ElementField field) {
    const auto found = patches_.find(id);
    if (found == patches_.end()) {
        return;
    }
    found->second.clear(field);
    if (found->second.mask == 0) {
        patches_.erase(found);
    }
}

void ElementPatches::clearElement(const std::string& id) {
    patches_.erase(id);
}

void ElementPatches::clearAll() {
    patches_.clear();
}

void ElementPatches::apply(core::dsl::Runtime& page) const {
    for (const auto& entry : patches_) {
        if (core::dsl::Element* element = page.findElement(entry.first)) {
            applyElementPatch(*element, entry.second);
        }
    }
}

ElementValues readElementValues(const core::dsl::Runtime& page, const std::string& id, std::uint32_t written) {
    ElementValues values;
    if (id.empty()) {
        return values;
    }
    const core::dsl::Element* element = page.findElement(id);
    if (element == nullptr) {
        return values;
    }

    values.active = true;
    values.id = element->id;
    values.kind = element->kind;
    values.frame = {element->frame.x, element->frame.y, element->frame.width, element->frame.height};
    values.margin = element->margin;
    values.padding = element->padding;
    values.borderWidth = element->border.width;
    values.zIndex = element->zIndex;
    values.clip = element->clip;
    values.interactive = element->interactive;
    values.disabled = element->disabled;
    values.text = truncateElementText(element->text, kElementValueTextLimit);

    // Layout
    values.widthSize = element->width;
    values.heightSize = element->height;
    values.minLayoutWidth = element->minLayoutWidth;
    values.minLayoutHeight = element->minLayoutHeight;
    values.maxLayoutWidth = element->maxLayoutWidth;
    values.maxLayoutHeight = element->maxLayoutHeight;
    values.flexGrow = element->flexGrow;
    values.flexShrink = element->flexShrink;
    values.mainAlign = element->mainAlign;
    values.crossAlign = element->crossAlign;
    values.spacing = element->spacing;
    values.ignoreLayout = element->ignoreLayout;

    // Typography & Content
    values.fontFamily = element->fontFamily;
    values.fontSize = element->fontSize;
    values.fontWeight = element->fontWeight;
    values.lineHeight = element->lineHeight;
    values.wrap = element->wrap;
    values.maxWidth = element->maxWidth;
    values.horizontalAlign = element->horizontalAlign;
    values.verticalAlign = element->verticalAlign;
    values.imageSource = truncateElementText(element->imageSource, kElementValueTextLimit);
    values.imageFit = element->imageFit;
    values.svgSource = truncateElementText(element->svgSource, kElementValueTextLimit);

    // Behavior & Input
    values.focusable = element->focusable;
    values.cursor = element->cursor;
    values.hasOnClick = static_cast<bool>(element->onClick);
    values.hasOnPress = static_cast<bool>(element->onPress);
    values.hasOnRelease = static_cast<bool>(element->onRelease);
    values.hasOnHoverChanged = static_cast<bool>(element->onHoverChanged);
    values.hasOnFocusChanged = static_cast<bool>(element->onFocusChanged);
    values.hasOnScroll = static_cast<bool>(element->onScroll);
    values.hasOnDrag = static_cast<bool>(element->onDrag);
    values.hasOnKeyEvent = static_cast<bool>(element->onKeyEvent);
    values.hasStateColors = element->hasStateColors;
    values.hoverColor = element->hoverColor;
    values.pressedColor = element->pressedColor;

    for (int index = 0; index < kElementFieldCount; ++index) {
        const ElementField field = static_cast<ElementField>(index);
        values.fields[static_cast<std::size_t>(index)] = readElementField(*element, field);
    }
    values.written = written;
    return values;
}

} // namespace modules::devtools

#endif
