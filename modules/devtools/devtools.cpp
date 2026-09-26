#include "modules/devtools/devtools.h"

#if defined(EUI_DEBUG_BUILD)
#include "modules/devtools/devtools_host.h"
#endif

namespace modules::devtools {

bool available() {
#if defined(EUI_DEBUG_BUILD)
    return true;
#else
    return false;
#endif
}

#if defined(EUI_DEBUG_BUILD)

Session::Session() {
    attachDevtoolsHost();
}

Session::~Session() {
    detachDevtoolsHost();
}

#endif

} // namespace modules::devtools
