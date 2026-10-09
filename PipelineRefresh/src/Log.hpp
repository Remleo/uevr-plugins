#pragma once

// UEVR's own log, printf-style, set once the plugin is initialised.
namespace plugin_log {
inline void (*info)(const char* format, ...) = nullptr;
inline void (*error)(const char* format, ...) = nullptr;
}
