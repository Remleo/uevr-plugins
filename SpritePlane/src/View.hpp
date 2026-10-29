#pragma once

#include <cstdint>

// One view's uniform parameters, right after UE filled them: an eye's get the head's ViewRight and ViewUp.
namespace view {
// Render thread. `params` is the view's FViewUniformShaderParameters.
void after_setup(uint8_t* params);

// Totals since the start, for the log.
struct Stats {
    uint64_t patched;  // eye views given the head's axes
    uint64_t not_eye;  // views with ViewForward known whose axes are no eye's
    uint64_t unfound;  // views searched without finding ViewForward
    uint64_t faults;   // views whose parameters could not be read
};

Stats stats();
}
