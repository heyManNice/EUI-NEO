#pragma once

namespace modules::devtools {

// True when the DevTools panel is part of this build. The panel is compiled where the
// tooling seam is, so a build without it reports false and carries no panel code.
bool available();

#if defined(EUI_TOOLING)
// Keeps the DevTools panel attached to the application for the lifetime of the object:
//
//     static modules::devtools::Session devtoolsSession;
//
// Constraints it has to meet:
//  - It must outlive app::shutdown(), which a file scope object does.
//  - One application keeps one session: the panel's place in the app loop is a single
//    slot, so a second live session is refused (and asserts in a debug build).
//  - While it lives the app loop asks the panel to draw and the page calls it; when it
//    goes away both stop, which is what keeps a page from calling a panel nobody owns.
//
// Attaching does not open the panel; the user toggles it with F12.
class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

private:
    // True when this session is the one that installed the panel; a refused session owns
    // nothing and has nothing to take back down.
    bool attached_ = false;
};
#else
// Debug tooling is not part of this configuration; a session compiles away.
class Session {};
#endif

} // namespace modules::devtools
