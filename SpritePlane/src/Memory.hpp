#pragma once

#include <cstddef>

// Reading the game's memory without letting a fault reach anything that holds a lock.
namespace memory {
// How many of the `want` bytes from `from` lie in committed pages that may be read, as VirtualQuery reports them.
size_t readable(const void* from, size_t want);

// Copies `size` bytes; false when the copy faulted. Nothing in it has a destructor, so a fault leaves nothing behind:
// a C++ lock released by a destructor stays taken when a structured exception skips it (/EHsc).
bool copy(void* to, const void* from, size_t size);
}
