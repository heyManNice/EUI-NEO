#include "modules/devtools/devtools_fields.h"

#if defined(EUI_DEBUG_BUILD)

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
    for (int index = 0; index < kElementFieldCount; ++index) {
        const ElementField field = static_cast<ElementField>(index);
        values.fields[static_cast<std::size_t>(index)] = readElementField(*element, field);
    }
    values.written = written;
    return values;
}

} // namespace modules::devtools

#endif
