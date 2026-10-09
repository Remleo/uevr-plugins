// PipelineRefresh: a UEVR plugin that switches a command list's pipeline state away and back right before the first
// draw after compute work (Rebind.hpp).
//
// Why. In STALKER 2 (UE 5.5, D3D12, NVIDIA, native stereo fix, DLSS) the right eye intermittently showed the whole
// picture squeezed into the pre-upscale rectangle. The pass that draws it, ComposeSeparateTranslucency right after the
// DLSS evaluation, then samples with the constants of the draw made before it with the same pipeline state -- the same
// pass at the pre-upscale size. The state is the game's own and is fine: drawn right after a switch to another state and
// back it was clean in 506 of 512 frames, against 379 of 511 untouched. Nothing here knows the game or the engine.
//
// Switch. The DLL's presence in the profile's plugins folder; for comparisons, the custom event "pipeline_refresh" with
// "0" or "1" (from Lua: uevr.api:dispatch_custom_event) stops and resumes the switching.

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>

#include <windows.h>
#include <d3d12.h>

#include "uevr/Plugin.hpp"

#include "Log.hpp"
#include "Rebind.hpp"

namespace {
// ID3D12GraphicsCommandList's vtable.
constexpr size_t RESET = 10, CLEAR_STATE = 11, DRAW_INSTANCED = 12, DRAW_INDEXED_INSTANCED = 13, DISPATCH = 14, SET_PIPELINE_STATE = 25,
                 EXECUTE_BUNDLE = 27, SET_GRAPHICS_ROOT_SIGNATURE = 30, EXECUTE_INDIRECT = 59;

using List = ID3D12GraphicsCommandList;

HRESULT(STDMETHODCALLTYPE* g_reset)(List*, ID3D12CommandAllocator*, ID3D12PipelineState*){};
void(STDMETHODCALLTYPE* g_clear_state)(List*, ID3D12PipelineState*){};
void(STDMETHODCALLTYPE* g_draw_instanced)(List*, UINT, UINT, UINT, UINT){};
void(STDMETHODCALLTYPE* g_draw_indexed_instanced)(List*, UINT, UINT, UINT, INT, UINT){};
void(STDMETHODCALLTYPE* g_dispatch)(List*, UINT, UINT, UINT){};
rebind::SetPipelineStateFn g_set_pipeline_state{};
void(STDMETHODCALLTYPE* g_execute_bundle)(List*, List*){};
void(STDMETHODCALLTYPE* g_set_graphics_root_signature)(List*, ID3D12RootSignature*){};
void(STDMETHODCALLTYPE* g_execute_indirect)(List*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64){};

std::atomic<bool> g_enabled{true};

void before_draw(List* list) {
    if (g_enabled.load(std::memory_order_relaxed)) rebind::before_draw(list, g_set_pipeline_state);
}

HRESULT STDMETHODCALLTYPE reset(List* list, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initial) {
    rebind::on_reset(list, initial);
    return g_reset(list, allocator, initial);
}

void STDMETHODCALLTYPE clear_state(List* list, ID3D12PipelineState* state) {
    rebind::on_reset(list, state);
    g_clear_state(list, state);
}

void STDMETHODCALLTYPE draw_instanced(List* list, UINT vertices, UINT instances, UINT start_vertex, UINT start_instance) {
    before_draw(list);
    g_draw_instanced(list, vertices, instances, start_vertex, start_instance);
}

void STDMETHODCALLTYPE draw_indexed_instanced(List* list, UINT indices, UINT instances, UINT start_index, INT base_vertex, UINT start_instance) {
    before_draw(list);
    g_draw_indexed_instanced(list, indices, instances, start_index, base_vertex, start_instance);
}

void STDMETHODCALLTYPE dispatch(List* list, UINT x, UINT y, UINT z) {
    rebind::on_compute(list);
    g_dispatch(list, x, y, z);
}

void STDMETHODCALLTYPE set_pipeline_state(List* list, ID3D12PipelineState* state) {
    rebind::on_set_pipeline_state(list, state);
    g_set_pipeline_state(list, state);
}

// A bundle may leave a pipeline state of its own behind: the list's is not known after it.
void STDMETHODCALLTYPE execute_bundle(List* list, List* bundle) {
    rebind::on_set_pipeline_state(list, nullptr);
    g_execute_bundle(list, bundle);
}

void STDMETHODCALLTYPE set_graphics_root_signature(List* list, ID3D12RootSignature* root) {
    rebind::on_set_root_signature(list, root);
    g_set_graphics_root_signature(list, root);
}

// Treated as a draw. Not as compute work: UE draws most of a frame through it, and counted as compute it put the switch
// before nearly every draw (about 600 000 a second, the frame rate down to 33).
void STDMETHODCALLTYPE execute_indirect(List* list, ID3D12CommandSignature* signature, UINT count, ID3D12Resource* arguments, UINT64 offset,
                                        ID3D12Resource* counts, UINT64 counts_offset) {
    before_draw(list);
    g_execute_indirect(list, signature, count, arguments, offset, counts, counts_offset);
}

template <typename T> void patch(void** vtable, size_t slot, T hook, T& original) {
    original = (T)vtable[slot];
    vtable[slot] = (void*)hook;
}

// Every direct command list of the runtime shares one vtable.
bool hook(ID3D12Device* device) {
    ID3D12CommandAllocator* allocator{};
    List* list{};

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)))) {
        if (allocator != nullptr) allocator->Release();
        return false;
    }

    auto** const vtable = *(void***)list;
    DWORD old{};
    const bool writable = VirtualProtect(vtable, (EXECUTE_INDIRECT + 1) * sizeof(void*), PAGE_READWRITE, &old);

    if (writable) {
        // The state is tracked before anything acts on it.
        patch(vtable, SET_PIPELINE_STATE, &set_pipeline_state, g_set_pipeline_state);
        patch(vtable, SET_GRAPHICS_ROOT_SIGNATURE, &set_graphics_root_signature, g_set_graphics_root_signature);
        patch(vtable, RESET, &reset, g_reset);
        patch(vtable, CLEAR_STATE, &clear_state, g_clear_state);
        patch(vtable, EXECUTE_BUNDLE, &execute_bundle, g_execute_bundle);
        patch(vtable, DISPATCH, &dispatch, g_dispatch);
        patch(vtable, EXECUTE_INDIRECT, &execute_indirect, g_execute_indirect);
        patch(vtable, DRAW_INSTANCED, &draw_instanced, g_draw_instanced);
        patch(vtable, DRAW_INDEXED_INSTANCED, &draw_indexed_instanced, g_draw_indexed_instanced);
        VirtualProtect(vtable, (EXECUTE_INDIRECT + 1) * sizeof(void*), old, &old);
    }

    list->Close();
    list->Release();
    allocator->Release();
    return writable;
}

class PipelineRefresh final : public uevr::Plugin {
public:
    void on_initialize() override {
        const auto* functions = uevr::API::get()->param()->functions;
        plugin_log::info = functions->log_info;
        plugin_log::error = functions->log_error;

        // Pinned: the vtable points into this module for the rest of the process.
        HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&set_pipeline_state, &self);

        plugin_log::info("[PipelineRefresh] loaded");
    }

    void on_present() override {
        if (!m_hooked) {
            const auto* renderer = uevr::API::get()->param()->renderer;

            if (renderer->renderer_type != UEVR_RENDERER_D3D12 || renderer->device == nullptr) return;

            m_hooked = true;

            if (!hook((ID3D12Device*)renderer->device)) {
                plugin_log::error("[PipelineRefresh] the command list could not be hooked; nothing will be switched");
                return;
            }

            plugin_log::info("[PipelineRefresh] command list hooked");
            m_last_stats = std::chrono::steady_clock::now();
        }

        log_stats();
    }

    void on_custom_event(const char* event_name, const char* event_data) override {
        if (event_name == nullptr || strcmp(event_name, "pipeline_refresh") != 0) return;

        g_enabled = event_data == nullptr || event_data[0] != '0';
        plugin_log::info("[PipelineRefresh] switching %s", g_enabled ? "on" : "off");
    }

private:
    // Every 5 s while anything happened: how many draws were given the switch, and how many could not be.
    void log_stats() {
        const auto now = std::chrono::steady_clock::now();

        if (now - m_last_stats < std::chrono::seconds(5)) return;

        m_last_stats = now;
        const auto switched = rebind::g_switched.load(), unknown = rebind::g_unknown.load(), no_dummy = rebind::g_no_dummy.load();

        if (switched == m_switched && unknown == m_unknown && no_dummy == m_no_dummy) return;

        plugin_log::info("[PipelineRefresh] last 5 s: %llu draw(s) after compute work given a switch away and back; %llu with no state or root "
                         "signature seen set, %llu with no dummy state",
                         (unsigned long long)(switched - m_switched), (unsigned long long)(unknown - m_unknown),
                         (unsigned long long)(no_dummy - m_no_dummy));
        m_switched = switched;
        m_unknown = unknown;
        m_no_dummy = no_dummy;
    }

    bool m_hooked{};
    std::chrono::steady_clock::time_point m_last_stats{};
    uint64_t m_switched{}, m_unknown{}, m_no_dummy{};
};

std::unique_ptr<PipelineRefresh> g_plugin{new PipelineRefresh{}};
}
