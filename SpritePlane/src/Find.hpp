#pragma once

#include <cstdint>

// Where UE's FSceneView::SetupCommonViewUniformBufferParameters is in the running game's module, found without symbols.
namespace find {
// The function's start, or nullptr if the trail did not lead to exactly one function; the log says where it stopped.
void* setup_common_view_uniform_buffer_parameters();
}
