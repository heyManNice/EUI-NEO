#pragma once

#include "eui/app.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace app {

template <typename WindowT>
class DslWindowManager {
public:
    using WindowPtr = std::unique_ptr<WindowT>;

    bool empty() const {
        return windows_.empty();
    }

    template <typename CreateFn>
    void createPending(const std::vector<DslWindowRequest>& requests, CreateFn&& createWindow) {
        for (const DslWindowRequest& request : requests) {
            if (!request.compose || closeRequested(request.handle)) {
                finishRequest(request);
                continue;
            }
            if (WindowPtr window = createWindow(request)) {
                if (request.handle.state_) {
                    request.handle.state_->phase = detail::DslWindowState::Phase::Open;
                }
                windows_.push_back(std::move(window));
            } else {
                finishRequest(request);
            }
        }
    }

    template <typename ClosedFn, typename DestroyFn>
    void pruneClosed(ClosedFn&& isClosed, DestroyFn&& destroyWindow) {
        windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [&](WindowPtr& window) {
            if (!window) {
                return true;
            }
            if (!closeRequested(window->content.request().handle) && !isClosed(*window)) {
                return false;
            }
            const DslWindowHandle handle = window->content.request().handle;
            const std::function<void()> onClosed = window->content.request().onClosed;
            destroyWindow(window);
            finishRequest(handle, onClosed);
            return true;
        }), windows_.end());
    }

    template <typename ClosedFn>
    WindowT* modalWindow(ClosedFn&& isClosed) {
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            WindowPtr& window = *it;
            if (window && !closeRequested(window->content.request().handle) &&
                window->content.request().modal && !isClosed(*window)) {
                return window.get();
            }
        }
        return nullptr;
    }

    template <typename Predicate>
    WindowT* find(Predicate&& predicate) {
        for (WindowPtr& window : windows_) {
            if (window && predicate(*window)) {
                return window.get();
            }
        }
        return nullptr;
    }

    bool anyAnimating() const {
        return anyAnimating([](const WindowT&) {
            return true;
        });
    }

    template <typename ActiveFn>
    bool anyAnimating(ActiveFn&& isActive) const {
        return std::any_of(windows_.begin(), windows_.end(), [&](const WindowPtr& window) {
            return window && !closeRequested(window->content.request().handle) &&
                   isActive(*window) && window->content.isAnimating();
        });
    }

    template <typename UpdateFn>
    void updateAll(UpdateFn&& updateWindow) {
        for (WindowPtr& window : windows_) {
            if (window && !closeRequested(window->content.request().handle)) {
                updateWindow(*window);
            }
        }
    }

    template <typename DestroyFn>
    void destroyAll(DestroyFn&& destroyWindow) {
        for (WindowPtr& window : windows_) {
            if (!window) {
                continue;
            }
            const DslWindowHandle handle = window->content.request().handle;
            const std::function<void()> onClosed = window->content.request().onClosed;
            destroyWindow(window);
            finishRequest(handle, onClosed);
        }
        windows_.clear();
    }

private:
    static bool closeRequested(const DslWindowHandle& handle) {
        return handle.state_ && handle.state_->closeRequested;
    }

    static void finishRequest(const DslWindowRequest& request) {
        finishRequest(request.handle, request.onClosed);
    }

    static void finishRequest(const DslWindowHandle& handle, const std::function<void()>& onClosed) {
        if (handle.state_) {
            handle.state_->phase = detail::DslWindowState::Phase::Closed;
        }
        if (onClosed) {
            onClosed();
        }
    }

    std::vector<WindowPtr> windows_;
};

} // namespace app
