#include "modules/devtools/devtools.h"

#if defined(EUI_TOOLING)
#include "modules/devtools/devtools_host.h"
#endif

namespace modules::devtools {

bool available() {
#if defined(EUI_TOOLING)
    return true;
#else
    return false;
#endif
}

#if defined(EUI_TOOLING)

Session::Session() : attached_(attachDevtoolsHost()) {}

Session::~Session() {
    // A refused session never owned anything, so it has nothing to take back down.
    if (attached_) {
        detachDevtoolsHost();
    }
}

#endif

} // namespace modules::devtools
