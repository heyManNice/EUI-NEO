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
class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};
#else
// Debug tooling is not part of this configuration; a session compiles away.
class Session {};
#endif

} // namespace modules::devtools
