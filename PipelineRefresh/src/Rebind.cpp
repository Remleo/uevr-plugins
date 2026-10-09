#include "Rebind.hpp"

#include <array>
#include <mutex>
#include <unordered_map>

#include <windows.h>
#include <d3dcompiler.h>

#include "Log.hpp"

namespace rebind {
namespace {
// What a list has bound, as seen on the thread recording it. A list is recorded by one thread at a time; a thread
// records a handful of lists in turn, so a few slots per thread are enough, the least recently used given up first.
struct ListState {
    ID3D12GraphicsCommandList* list{};
    ID3D12PipelineState* state{};
    ID3D12RootSignature* root{};
    bool compute{};
    ID3D12PipelineState* last_draw{};      // the state the last draw used, a graphics one by definition
    ID3D12PipelineState* before_compute{}; // the last draw's state when compute work began
    uint64_t used{};
};

thread_local std::array<ListState, 8> t_lists{};
thread_local uint64_t t_clock{};

ListState& state_of(ID3D12GraphicsCommandList* list) {
    ListState* oldest = &t_lists[0];

    for (auto& s : t_lists) {
        if (s.list == list) {
            s.used = ++t_clock;
            return s;
        }

        if (s.used < oldest->used) oldest = &s;
    }

    *oldest = ListState{list, nullptr, nullptr, false, nullptr, nullptr, ++t_clock};
    return *oldest;
}

// A vertex shader that reads nothing, so that a state made from it suits any root signature.
const ID3DBlob* dummy_vertex_shader() {
    static ID3DBlob* blob = [] {
        using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                           ID3DBlob**);
        auto* const module = LoadLibraryW(L"d3dcompiler_47.dll");
        const auto compile = module != nullptr ? (CompileFn)GetProcAddress(module, "D3DCompile") : nullptr;
        constexpr char source[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }";
        ID3DBlob* code{};
        ID3DBlob* errors{};

        if (compile == nullptr || FAILED(compile(source, sizeof(source) - 1, "pipeline_refresh", nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors))) {
            plugin_log::error("[PipelineRefresh] the dummy vertex shader could not be compiled");
            code = nullptr;
        }

        if (errors != nullptr) errors->Release();

        return code;
    }();

    return blob;
}

std::mutex g_dummies_mutex{};
std::unordered_map<ID3D12RootSignature*, ID3D12PipelineState*> g_dummies{};

// A state that draws nothing, made against `root` once; null when it cannot be made. It holds a reference to `root`, so
// the address keeps naming the same root signature for as long as the entry exists.
ID3D12PipelineState* dummy_for(ID3D12RootSignature* root) {
    std::scoped_lock _{g_dummies_mutex};

    if (const auto it = g_dummies.find(root); it != g_dummies.end()) return it->second;

    auto& dummy = g_dummies[root];
    const auto* vs = dummy_vertex_shader();
    ID3D12Device* device{};

    if (vs == nullptr || FAILED(root->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) return dummy;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root;
    d.VS = {const_cast<ID3DBlob*>(vs)->GetBufferPointer(), const_cast<ID3DBlob*>(vs)->GetBufferSize()};

    for (auto& rt : d.BlendState.RenderTarget) {
        rt.SrcBlend = rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    d.DepthStencilState.FrontFace = d.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                                                    D3D12_COMPARISON_FUNC_ALWAYS};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.SampleDesc.Count = 1;

    const auto hr = device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&dummy));
    device->Release();

    if (FAILED(hr)) {
        plugin_log::error("[PipelineRefresh] no dummy state against root signature %p: %08x", (void*)root, (unsigned)hr);
        dummy = nullptr;
    }

    return dummy;
}
}

void on_set_pipeline_state(ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
    state_of(list).state = state;
}

void on_set_root_signature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root) {
    state_of(list).root = root;
}

void on_compute(ID3D12GraphicsCommandList* list) {
    auto& s = state_of(list);

    if (!s.compute) s.before_compute = s.last_draw;

    s.compute = true;
}

void on_reset(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initial) {
    auto& s = state_of(list);
    s.state = initial;
    s.root = nullptr;
    s.compute = false;
    s.last_draw = nullptr;
    s.before_compute = nullptr;
}

void before_draw(ID3D12GraphicsCommandList* list, SetPipelineStateFn set) {
    auto& s = state_of(list);
    const auto previous = s.last_draw;
    s.last_draw = s.state;

    if (!s.compute) return;

    s.compute = false;

    // A different state from the last draw before the compute work makes the driver bind everything anew by itself; only
    // the same one (set again after the compute work, or never changed) is the case measured. The compute work sets
    // compute states of its own, so it is the draws' states that are compared.
    if (s.state != s.before_compute || previous == nullptr) return;

    if (s.state == nullptr || s.root == nullptr) {
        g_unknown.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto* const dummy = dummy_for(s.root);

    if (dummy == nullptr) {
        g_no_dummy.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    set(list, dummy);
    set(list, s.state);
    g_switched.fetch_add(1, std::memory_order_relaxed);
}
}
