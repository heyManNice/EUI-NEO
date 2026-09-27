#pragma once

namespace modules::devtools {

// True when the DevTools panel is part of this build. The panel implementation
// is compiled for Debug configurations only, so Release builds report false and
// carry no DevTools code.
bool available();

#if defined(EUI_DEBUG_BUILD)
// Keeps the DevTools panel attached to the application for the lifetime of the
// object. An application opts in by keeping one session alive:
//
//     static modules::devtools::Session devtoolsSession;
//
// The session has to outlive app::shutdown(), which a file scope object does.
// Attaching does not open the panel; the user toggles it with F12.
//
// The session owns both halves of the registration: while it lives the app loop asks
// the panel to draw and the page calls it, and when it goes away both stops — the page
// never keeps calling a panel nobody owns. One application keeps one session; a second
// live session is refused (and asserts in a debug build), because the panel's place in
// the app loop is one slot.
class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

private:
    // True when this session is the one that installed the panel; a refused session has
    // nothing of its own to take back down.
    bool attached_ = false;
};
#else
// Debug tooling is not part of this configuration; a session compiles away.
class Session {};
#endif

} // namespace modules::devtools
