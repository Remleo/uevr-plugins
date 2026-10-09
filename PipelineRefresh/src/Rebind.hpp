#pragma once

// A draw after compute work gets its pipeline state switched away and back right before it.
//
// Measured in STALKER 2 (UE 5.5, D3D12, NVIDIA, frames rotated four ways right before the compose pass's draw): the same
// state set again, 173 of 511 frames squeezed; switched to another state and back, 6 of 512; untouched, 132 of 511. So
// the draw must follow a change of pipeline state made after its constants were bound; setting the same state again does
// nothing, presumably because the driver drops it as redundant. The pass is the first draw after the DLSS evaluation, a
// compute workload on the same list, and the game sets its state once, before an earlier draw with other constants --
// so a draw after compute work is where the switch goes. That compute work is the trigger is presumed, not measured.

#include <atomic>
#include <cstdint>

#include <d3d12.h>

namespace rebind {
using SetPipelineStateFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);

void on_set_pipeline_state(ID3D12GraphicsCommandList* list, ID3D12PipelineState* state);
void on_set_root_signature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root);
void on_compute(ID3D12GraphicsCommandList* list);
void on_reset(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initial);

// Right before a draw: after compute work since the last draw, the state is switched to a dummy with the bound root
// signature and back, through `set` (the runtime's own SetPipelineState).
void before_draw(ID3D12GraphicsCommandList* list, SetPipelineStateFn set);

inline std::atomic<uint64_t> g_switched{};  // draws given the switch
inline std::atomic<uint64_t> g_unknown{};   // draws after compute work whose state or root signature was not seen set
inline std::atomic<uint64_t> g_no_dummy{};  // draws whose root signature took no dummy state
}
