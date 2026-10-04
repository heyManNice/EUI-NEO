#pragma once

namespace core::dsl {

inline bool Runtime::initialize() {
    return true;
}

inline bool Runtime::initialize(core::window::Handle window) {
    core::window::installInputCallbacks(window);
    return true;
}

template <typename ComposeFn>
inline void Runtime::compose(const std::string& pageId, float logicalWidth, float logicalHeight, ComposeFn&& composeFn) {
    composeViewport(pageId, {0.0f, 0.0f, logicalWidth, logicalHeight}, false, std::forward<ComposeFn>(composeFn));
}

template <typename ComposeFn>
inline void Runtime::compose(const std::string& pageId, const Rect& viewport, ComposeFn&& composeFn) {
    composeViewport(pageId, viewport, true, std::forward<ComposeFn>(composeFn));
}

template <typename ComposeFn>
inline void Runtime::composeViewport(const std::string& pageId, const Rect& viewport, bool clipViewport, ComposeFn&& composeFn) {
    const std::vector<runtime::ElementSnapshot> previousStructure = elementStructure_;
    // A compose builds every element again, so a tool that remembered element pointers
    // has to forget them first.
    tooling::beforeCompose(*this);
    const Screen screen{viewport.width, viewport.height};
    ui_.begin(pageId);
    ui_.setFocusedId(focusedId_);
    composeFn(ui_, screen);
    ui_.end();
    // The values a tool replaced go back on the fresh tree before it is laid out:
    // everything downstream then reads one value instead of asking whether a tool
    // replaced it.
    tooling::afterCompose(*this);
    ui_.layout(screen.width, screen.height, viewport.x, viewport.y);
    elementStructure_ = collectElementStructure();
    syncScrollStateBindings();
    for (const std::string& scope : ui_.consumeReleasedStateScopes()) {
        const std::string childPrefix = scope + ".";
        if (focusedId_ == scope || focusedId_.rfind(childPrefix, 0) == 0) {
            focusedId_.clear();
            ui_.setFocusedId(focusedId_);
        }
    }
    if (!focusedId_.empty() && isElementInDisabledTree(focusedId_)) {
        setFocusedId({});
    }

    if (elementStructure_ != previousStructure) {
        paintRequested_ = true;
        fullPaintRequested_ = true;
        pruneInstancesRequested_ = true;
        // The tree a tool copied is out of date from here on, which the snapshot's
        // revision tells it without comparing snapshots.
        ++elementStructureRevision_;
    }

    if (viewport_.x != viewport.x || viewport_.y != viewport.y || viewport_.width != viewport.width || viewport_.height != viewport.height || clipViewport_ != clipViewport) {
        paintRequested_ = true;
        fullPaintRequested_ = true;
    }
    fullTreeUpdateRequested_ = true;
    viewport_ = viewport;
    clipViewport_ = clipViewport;
}

inline bool Runtime::update(core::window::Handle window, float deltaSeconds, float pointerScale, float dpiScale, bool inputEnabled) {
    ++updateFrameToken_;
    if (updateFrameToken_ == 0) {
        ++updateFrameToken_;
    }
    if (!inputEnabled) {
        cancelInput(window);
        // Input is off for this frame, so the events a host pushed for it go with it.
        tooling::resetHostInput(*this);
    }
    std::vector<PointerEvent> pointerEvents = consumePointerEvents(window, pointerScale);
    std::vector<KeyEvent> keyEvents = consumeKeyEvents(window);
    TextInputEvent textInputEvent = consumeTextInput(window);
    ScrollEvent scrollEvent = consumeScrollInput(window);
    // A runtime driven by a host has no window input queue: the events its tool pushed
    // are merged into this frame's input.
    tooling::mergeHostInput(*this, pointerEvents, scrollEvent);
    if (!inputEnabled) {
        for (PointerEvent& event : pointerEvents) {
            event.x = -1000000.0;
            event.y = -1000000.0;
            event.deltaX = 0.0;
            event.deltaY = 0.0;
            event.modifiers = {};
        }
        keyEvents.clear();
        textInputEvent = {};
        scrollEvent = {};
    }
    // The tool takes the events it wants before the page sees them.
    tooling::filterInput(*this, pointerEvents, scrollEvent);
    animating_ = false;
    composeRequested_ = false;
    wantsHandCursor_ = false;
    if (pruneInstancesRequested_) {
        instances_.markInstancesUnseen();
    }
    instances_.markTimersUnseen();
    if (ImagePrimitive::consumeRemoteImageReady()) {
        fullPaintRequested_ = true;
        paintRequested_ = true;
    }

    syncScrollStateBindings();
    if (scrollEvent.active()) {
        // The scroll receiver is whatever is under the pointer, so a scroll with no pointer
        // event in this frame has nothing to address and is dropped.
        if (!pointerEvents.empty()) {
            updateScroll(scrollEvent, hitTestScrollable(pointerEvents.back(), dpiScale));
        }
        hoverTargetCacheValid_ = false;
    }
    updateScrollMotion(deltaSeconds);

    for (std::size_t index = 0; index < pointerEvents.size(); ++index) {
        const PointerEvent& event = pointerEvents[index];
        if (event.isPress(PointerButton::Left)) {
            setFocusedId(hitTestFocusable(event, dpiScale));
        }
        const std::string hoverTargetId = resolveHoverTarget(event, dpiScale, inputEnabled);
#if EUI_TOOLING_ENABLED
        if (event.action == PointerAction::Press || event.action == PointerAction::Release) {
            runtime::ToolingInputRecord record;
            record.timestamp = core::window::timeSeconds();
            record.kind = event.action == PointerAction::Press
                ? runtime::ToolingInputKind::PointerPress
                : runtime::ToolingInputKind::PointerRelease;
            record.targetId = hoverTargetId;
            record.x = static_cast<float>(event.x);
            record.y = static_cast<float>(event.y);
            record.button = event.button;
            const char* btnStr = "None";
            if (event.button == PointerButton::Left) btnStr = "Left";
            else if (event.button == PointerButton::Right) btnStr = "Right";
            else if (event.button == PointerButton::Middle) btnStr = "Middle";
            record.detail = std::string(btnStr) + " @ (" + std::to_string(static_cast<int>(event.x)) + ", " + std::to_string(static_cast<int>(event.y)) + ")";
            tooling::recordInputEvent(*this, std::move(record));
        }
#endif
        const float eventDeltaSeconds = index + 1 == pointerEvents.size() ? deltaSeconds : 0.0f;
        updateElementTree(event, eventDeltaSeconds, dpiScale, hoverTargetId);
    }
    updateDependentVisualDirtyRegions(dpiScale);

    if (!keyEvents.empty()) {
        updateKeyInput(keyEvents);
    }
    if (textInputEvent.hasInput()) {
        updateTextInput(textInputEvent);
    }
    instances_.releaseUnseenTimers();
    updateImeCursorRect(window, dpiScale);
    applyCursor(window);

    promoteBackdropBlurDirtyRegions(dpiScale);
    if (pruneInstancesRequested_) {
        instances_.releaseUnseenInstances();
        pruneInstancesRequested_ = false;
    }
    fullTreeUpdateRequested_ = false;
    previousFrameAnimating_ = animating_;

    const bool result = paintRequested_;
    paintRequested_ = false;
    return result;
}

inline bool Runtime::isAnimating() const {
    return animating_;
}

inline bool Runtime::composeRequested() const {
    return composeRequested_;
}

inline bool Runtime::paintRequested() const {
    return paintRequested_;
}

inline void Runtime::requestFullPaint() {
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline Element* Runtime::findElement(const std::string& id) const {
    // Insertion order, not paint order: the walk has to work between a compose and the next
    // layout pass, when only `children` is filled in.
    std::vector<Element*> pending;
    pending.reserve(ui_.roots().size());
    for (const auto& root : ui_.roots()) {
        pending.push_back(root.get());
    }
    while (!pending.empty()) {
        Element* element = pending.back();
        pending.pop_back();
        if (element->id == id) {
            return element;
        }
        for (const auto& child : element->children) {
            pending.push_back(child.get());
        }
    }
    return nullptr;
}

inline void Runtime::requestElementRefresh() {
    // The per-frame capture walks the tree every frame, but it may skip a static subtree,
    // so one full tree update is requested as well.
    fullTreeUpdateRequested_ = true;
    paintRequested_ = true;
    fullPaintRequested_ = true;
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale, const Color& clearColor) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    core::render::beginRenderFrameStats(windowWidth, windowHeight);
    ImagePrimitive::beginRenderFrame();
    core::render::RenderFrameStats& stats = core::render::currentRenderFrameStats();
    const Rect viewportPixels = toPixelRect(viewport_, dpiScale);
    const Rect* viewportClip = clipViewport_ ? &viewportPixels : nullptr;

    const bool hasRenderableContent = !ui_.roots().empty();
    const auto releasePrunedRetainedLayers = [&] {
        instances_.releaseUnseenRetainedLayers();
    };
    if (!hasRenderableContent) {
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        // The tool draws on top of the page, inside this pass, so its output belongs to
        // the cached frame the window blits.
        tooling::drawOverlay(*this, windowWidth, windowHeight, dpiScale, nullptr);
        dirtyRects_.clear();
        fullPaintRequested_ = false;
        releasePrunedRetainedLayers();
        core::render::publishRenderFrameStats();
        return;
    }

    if (!renderBackend->ensureRenderCache(windowWidth, windowHeight)) {
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        ++stats.renderDirectPasses;
        RuntimeRenderer(ui_, instances_, viewportClip, tooling_.get())
            .renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
        tooling::drawOverlay(*this, windowWidth, windowHeight, dpiScale, nullptr);
        dirtyRects_.clear();
        fullPaintRequested_ = false;
        releasePrunedRetainedLayers();
        core::render::publishRenderFrameStats();
        return;
    }
    stats.usedRenderCache = true;
    if (renderBackend->renderCacheWasRecreated()) {
        fullPaintRequested_ = true;
        stats.renderCacheRecreated = true;
    }

    if (!fullPaintRequested_ && dirtyRects_.empty()) {
        renderBackend->blitRenderCache(windowWidth, windowHeight, core::render::RenderCacheBlitMode::Existing);
        releasePrunedRetainedLayers();
        core::render::publishRenderFrameStats();
        return;
    }

    stats.fullPaint = fullPaintRequested_;
    const std::vector<Rect> dirtyRects = fullPaintRequested_
        ? std::vector<Rect>{}
        : core::dsl::resolveDirtyRects(dirtyRects_, windowWidth, windowHeight, dpiScale);
    if (!fullPaintRequested_ && dirtyRects.empty()) {
        dirtyRects_.clear();
        renderBackend->blitRenderCache(windowWidth, windowHeight, core::render::RenderCacheBlitMode::Existing);
        releasePrunedRetainedLayers();
        core::render::publishRenderFrameStats();
        return;
    }
    stats.dirtyRectCount = static_cast<int>(dirtyRects.size());
    for (const Rect& dirty : dirtyRects) {
        const float width = std::max(0.0f, dirty.width);
        const float height = std::max(0.0f, dirty.height);
        stats.dirtyPixels += static_cast<std::uint64_t>(width * height);
    }

    renderBackend->beginRenderCacheFrame(windowWidth, windowHeight, dirtyRects);

    if (fullPaintRequested_) {
        renderBackend->setScissor(false, {}, windowHeight);
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        ++stats.renderDirectPasses;
        RuntimeRenderer(ui_, instances_, viewportClip, tooling_.get())
            .renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
        tooling::drawOverlay(*this, windowWidth, windowHeight, dpiScale, nullptr);
    } else {
        for (const Rect& dirty : dirtyRects) {
            renderBackend->setScissor(true, dirty, windowHeight);
            ++stats.clearCalls;
            renderBackend->clear(clearColor);
            ++stats.renderDirectPasses;
            RuntimeRenderer(ui_, instances_, viewportClip, tooling_.get())
                .renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale, &dirty);
            tooling::drawOverlay(*this, windowWidth, windowHeight, dpiScale, &dirty);
        }
        renderBackend->setScissor(false, {}, windowHeight);
    }

    renderBackend->endRenderCacheFrame();
    renderBackend->blitRenderCache(windowWidth,
                                   windowHeight,
                                   fullPaintRequested_ ? core::render::RenderCacheBlitMode::Full
                                                       : core::render::RenderCacheBlitMode::Dirty,
                                   dirtyRects);
    const bool retainedLayerWarmupNeeded =
        stats.retainedLayerMisses > 0 && stats.retainedLayerRebuilds == 0;
    dirtyRects_.clear();
    fullPaintRequested_ = retainedLayerWarmupNeeded;
    paintRequested_ = retainedLayerWarmupNeeded;
    releasePrunedRetainedLayers();
    core::render::publishRenderFrameStats();
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    ImagePrimitive::beginRenderFrame();

    const Rect viewportPixels = toPixelRect(viewport_, dpiScale);
    RuntimeRenderer(ui_, instances_, clipViewport_ ? &viewportPixels : nullptr, tooling_.get())
        .renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
    instances_.releaseUnseenRetainedLayers();
}

inline void Runtime::renderDirectOverlay(int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }
    const Rect viewportPixels = toPixelRect(viewport_, dpiScale);
    RuntimeRenderer(ui_, instances_, clipViewport_ ? &viewportPixels : nullptr, tooling_.get())
        .renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale, dirtyRect);
}

inline void Runtime::shutdown(bool releaseCachedImageTextures) {
    releaseGraphicsResources(releaseCachedImageTextures);
    instances_.clear();
    elementStructure_.clear();
    hoverTargetCacheValid_ = false;
    // 元素与回调也可能持有外部 GPU 资源，不能留到设备销毁后的 Runtime 析构。
    ui_.begin();
    ui_.end();
    ui_.clearState();
    keyEventHandler_ = {};
    // The tool is going away with the runtime, and so is everything it kept.
    tooling::release(*this);
    tooling_.reset();
}

inline void Runtime::releaseGraphicsResources(bool releaseCachedImageTextures) {
    instances_.releaseGraphicsResources(releaseCachedImageTextures);
    // The tool's overlay primitive belongs to the device that is going away.
    tooling::releaseGraphics(*this);
    destroyCursors();
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline void Runtime::applyCursor(core::window::Handle window) {
    if (window == nullptr) {
        // A host driven overlay runtime has no window of its own, so there is
        // nothing to apply a cursor to.
        return;
    }
    if (!arrowCursor_) {
        arrowCursor_ = core::window::createStandardCursor(core::window::CursorType::Arrow);
    }
    if (!handCursor_) {
        handCursor_ = core::window::createStandardCursor(core::window::CursorType::Hand);
    }

    core::window::CursorHandle target = wantsHandCursor_ && handCursor_ ? handCursor_ : arrowCursor_;
    if (target != currentCursor_) {
        core::window::setCursor(window, target);
        currentCursor_ = target;
    }
}

inline void Runtime::destroyCursors() {
    if (arrowCursor_) {
        core::window::destroyCursor(arrowCursor_);
        arrowCursor_ = nullptr;
    }
    if (handCursor_) {
        core::window::destroyCursor(handCursor_);
        handCursor_ = nullptr;
    }
    currentCursor_ = nullptr;
}

} // namespace core::dsl
