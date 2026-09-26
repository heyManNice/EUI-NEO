#include "core/dsl_runtime.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// The element tree snapshot is a debug tool: it exists in Debug builds only and
// the tools that display it read it on demand.
#if defined(EUI_DEBUG_BUILD)

namespace {

void composePage(core::dsl::Runtime& runtime, bool withExtraChild) {
    runtime.compose("page", 400.0f, 300.0f, [withExtraChild](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
        ui.column("root")
            .size(screen.width, screen.height)
            .content([&] {
                ui.text("title").size(120.0f, 20.0f).text("Hello").build();
                ui.row("buttons")
                    .width(200.0f)
                    .height(32.0f)
                    .content([&] {
                        ui.rect("ok").size(64.0f, 32.0f).onClick([] {}).build();
                        ui.rect("cancel").size(64.0f, 32.0f).disabled(true).build();
                        if (withExtraChild) {
                            ui.rect("later").size(48.0f, 32.0f).build();
                        }
                    })
                    .build();
            })
            .build();
    });
}

} // namespace

int main() {
    core::dsl::Runtime runtime;
    composePage(runtime, false);

    const core::dsl::runtime::ElementTreeSnapshot tree = runtime.elementTree();
    assert(tree.revision > 0);
    assert(!tree.truncated);
    assert(tree.nodes.size() == 5);

    // Pre-order with depth. Element ids are scoped by the page (and by the scopes
    // a component pushes), never by their parent element, so a tree view nests by
    // depth instead of by id segments.
    assert(tree.nodes[0].id == "page.root");
    assert(tree.nodes[0].kind == core::dsl::ElementKind::Column);
    assert(tree.nodes[0].depth == 0);
    assert(tree.nodes[0].frame.width == 400.0f);
    assert(tree.nodes[0].frame.height == 300.0f);

    assert(tree.nodes[1].id == "page.title");
    assert(tree.nodes[1].kind == core::dsl::ElementKind::Text);
    assert(tree.nodes[1].depth == 1);
    assert(tree.nodes[1].text == "Hello");
    assert(tree.nodes[1].frame.width == 120.0f);

    assert(tree.nodes[2].id == "page.buttons");
    assert(tree.nodes[2].kind == core::dsl::ElementKind::Row);
    assert(tree.nodes[2].depth == 1);
    assert(tree.nodes[2].frame.height == 32.0f);

    assert(tree.nodes[3].id == "page.ok");
    assert(tree.nodes[3].depth == 2);
    assert(tree.nodes[3].interactive);
    assert(!tree.nodes[3].disabled);
    assert(tree.nodes[3].frame.x == 0.0f);
    assert(tree.nodes[3].frame.width == 64.0f);

    assert(tree.nodes[4].id == "page.cancel");
    assert(tree.nodes[4].depth == 2);
    assert(tree.nodes[4].disabled);
    assert(tree.nodes[4].frame.x == 64.0f);

    // The copy is bounded: a caller can ask for fewer nodes and learns it was cut.
    const core::dsl::runtime::ElementTreeSnapshot limited = runtime.elementTree(3);
    assert(limited.nodes.size() == 3);
    assert(limited.truncated);
    assert(limited.revision == tree.revision);

    // Recomposing the same tree leaves the revision alone, so a viewer knows its
    // copy is still current.
    composePage(runtime, false);
    assert(runtime.elementTree().revision == tree.revision);
    assert(runtime.elementTree().nodes.size() == 5);

    // A structural change moves the revision; the viewer refetches from it.
    composePage(runtime, true);
    const core::dsl::runtime::ElementTreeSnapshot grown = runtime.elementTree();
    assert(grown.revision > tree.revision);
    assert(grown.nodes.size() == 6);
    assert(grown.nodes[5].id == "page.later");
    assert(grown.nodes[5].depth == 2);

    runtime.shutdown(false);
    return 0;
}

#else

int main() {
    // Release builds carry neither the snapshot nor the tools that read it.
    core::dsl::Runtime runtime;
    runtime.shutdown(false);
    return 0;
}

#endif
