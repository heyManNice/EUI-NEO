#pragma once

// The two build features the framework distinguishes. See docs/工具协议.md.
//
// EUI_TOOLING_ENABLED: an in-app tool (the DevTools panel, a profiler, a HUD) can
// talk to the runtime. This is the flag the tooling seam is built on. It follows
// the debug configuration by default (CMake defines EUI_TOOLING for Debug), but it
// is a feature of its own: a shipped build can enable it without being a debug
// build, and a debug build can leave it out.
//
// EUI_DEV_BUILD: the debug-build conveniences that are not tooling — window title
// statistics, the DSL debug overlay, extra assertions.
//
// This header is the single place in the framework that reads those two macros, so
// no other core file branches on "is this a debug build"; they ask about the
// feature they actually depend on.

#if defined(EUI_TOOLING)
#define EUI_TOOLING_ENABLED 1
#else
#define EUI_TOOLING_ENABLED 0
#endif

#if defined(EUI_DEBUG_BUILD)
#define EUI_DEV_BUILD 1
#else
#define EUI_DEV_BUILD 0
#endif

namespace core::dsl::tooling {

// Readable forms of the same two decisions, for code that wants a value instead of
// a preprocessor branch (a default, a table entry, an assertion).
inline constexpr bool kToolingEnabled = EUI_TOOLING_ENABLED != 0;
inline constexpr bool kDevBuild = EUI_DEV_BUILD != 0;

} // namespace core::dsl::tooling
