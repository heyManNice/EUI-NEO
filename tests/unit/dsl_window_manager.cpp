#include "core/app/dsl_window_manager.h"
#include "eui/detail/dsl_app_impl.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config;
    return config;
}

void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

namespace {

struct TestWindow {
    struct Content {
        app::DslWindowRequest windowRequest;
        const app::DslWindowRequest& request() const { return windowRequest; }
        bool isAnimating() const { return false; }
    } content;
};

app::DslWindowHandle queueWindow(int& closedCount) {
    return app::openWindow(app::DslWindowConfig{}.onClosed([&] { ++closedCount; }),
                           [](eui::Ui&, const eui::Screen&) {});
}

} // namespace

int main() {
    app::DslWindowManager<TestWindow> windows;
    int closedCount = 0;
    int createdCount = 0;
    int destroyedCount = 0;
    const auto create = [&](const app::DslWindowRequest& request) {
        ++createdCount;
        auto window = std::make_unique<TestWindow>();
        window->content.windowRequest = request;
        return window;
    };
    const auto destroy = [&](std::unique_ptr<TestWindow>& window) {
        ++destroyedCount;
        window.reset();
    };

    app::DslWindowHandle pending = queueWindow(closedCount);
    pending.requestClose();
    windows.createPending(app::consumeWindowRequests(), create);
    assert(pending.isClosed());
    assert(!pending.isOpen());
    assert(createdCount == 0 && closedCount == 1);

    app::DslWindowHandle failed = queueWindow(closedCount);
    windows.createPending(app::consumeWindowRequests(), [&](const app::DslWindowRequest&) {
        return std::unique_ptr<TestWindow>{};
    });
    assert(failed.isClosed());
    assert(closedCount == 2);

    app::DslWindowHandle active = queueWindow(closedCount);
    windows.createPending(app::consumeWindowRequests(), create);
    assert(active.isOpen());
    assert(!windows.empty());
    active.requestClose();
    assert(windows.find([](const TestWindow&) { return true; }) != nullptr);
    int updates = 0;
    windows.updateAll([&](TestWindow&) { ++updates; });
    assert(updates == 0);
    windows.pruneClosed([](const TestWindow&) { return false; }, destroy);
    assert(active.isClosed());
    assert(windows.empty());
    assert(destroyedCount == 1 && closedCount == 3);

    app::DslWindowHandle native = queueWindow(closedCount);
    windows.createPending(app::consumeWindowRequests(), create);
    windows.pruneClosed([](const TestWindow&) { return true; }, destroy);
    assert(native.isClosed());
    assert(destroyedCount == 2 && closedCount == 4);

    app::DslWindowHandle remaining = queueWindow(closedCount);
    windows.createPending(app::consumeWindowRequests(), create);
    windows.destroyAll(destroy);
    assert(remaining.isClosed());
    assert(destroyedCount == 3 && closedCount == 5);

    std::weak_ptr<int> contentLifetime;
    bool contentReleasedBeforeClosed = false;
    app::DslWindowHandle lifetime;
    {
        auto content = std::make_shared<int>(1);
        contentLifetime = content;
        lifetime = app::openWindow(
            app::DslWindowConfig{}.onClosed([&] {
                contentReleasedBeforeClosed = contentLifetime.expired();
            }),
            [content](eui::Ui&, const eui::Screen&) {});
    }
    windows.createPending(app::consumeWindowRequests(), create);
    lifetime.requestClose();
    windows.pruneClosed([](const TestWindow&) { return false; }, destroy);
    assert(contentReleasedBeforeClosed);
    assert(lifetime.isClosed());
    return 0;
}
