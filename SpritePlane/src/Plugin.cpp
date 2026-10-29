// SpritePlane: a UEVR plugin that gives both eyes' camera-facing sprites one plane under a canted projection.
//
// Why. UEVR's canted projection turns each eye a few degrees outward (7 on a Quest), and UE's sprites take their plane
// from the view's own axes: Cascade's sprite vertex factory and Niagara's FaceCamera mode use the view uniform buffer's
// ViewRight and ViewUp. So each eye's smoke stood in that eye's own plane, cut differently into walls and into the other
// sprites. In The Outer Worlds 2 the smoke matched straight ahead and thinned in one eye as the head turned, more with
// more turn; on both eyes read back it matched to NCC 0.64 under the cant and 0.96 without it.
//
// What. Right after UE's SetupCommonViewUniformBufferParameters fills a view's uniform parameters, the view's ViewRight
// and ViewUp (and HMDViewNoRollRight/Up, which sprites with "remove HMD roll" use) are replaced by the head's axes, the
// middle of the two eyes. ViewForward and every matrix stay the eye's: view space, depth, fog and the translucency volume
// all hang on them. Only views whose forward and up are an eye's of a recorded frame are touched.
//
// Switch. The DLL's presence in the profile's plugins folder; for comparisons, the custom event "sprite_plane" with
// "0" or "1" (from Lua: uevr.api:dispatch_custom_event) stops and resumes the replacement.

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>

#include <windows.h>

#include <safetyhook.hpp>

#include "uevr/Plugin.hpp"

#include "Basis.hpp"
#include "Find.hpp"
#include "Log.hpp"
#include "View.hpp"

namespace {
SafetyHookInline g_setup{};
std::atomic<bool> g_enabled{true};

// FSceneView::SetupCommonViewUniformBufferParameters(this, params, buffer size, MSAA samples, view rect, view matrices,
// previous view matrices). Every argument is passed on; only the second is looked at.
void setup_hook(void* scene_view, uint8_t* params, const void* buffer_size, uintptr_t samples, const void* rect, const void* matrices,
                const void* previous) {
    g_setup.unsafe_call<void>(scene_view, params, buffer_size, samples, rect, matrices, previous);

    if (g_enabled.load(std::memory_order_relaxed)) view::after_setup(params);
}

class SpritePlane final : public uevr::Plugin {
public:
    void on_initialize() override {
        const auto* functions = uevr::API::get()->param()->functions;
        plugin_log::info = functions->log_info;
        plugin_log::error = functions->log_error;

        // Pinned: the hook jumps into this module for the rest of the process.
        HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&setup_hook, &self);

        auto* const target = find::setup_common_view_uniform_buffer_parameters();

        if (target == nullptr) {
            plugin_log::error("[SpritePlane] the function was not found; nothing will be replaced");
            return;
        }

        // Installed disabled and stored before it is enabled: a render thread entering it in between would otherwise
        // call through an empty trampoline.
        g_setup = safetyhook::create_inline(target, (void*)&setup_hook, SafetyHookInline::StartDisabled);

        if (!g_setup || !g_setup.enable()) {
            plugin_log::error("[SpritePlane] the function could not be hooked; nothing will be replaced");
            return;
        }

        plugin_log::info("[SpritePlane] hooked");
        m_last_stats = std::chrono::steady_clock::now();
    }

    void on_pre_engine_tick(uevr::API::UGameEngine*, float) override { basis::next_frame(); }

    void on_post_calculate_stereo_view_offset(UEVR_StereoRenderingDeviceHandle, int view_index, float, UEVR_Vector3f*,
                                              UEVR_Rotatorf* rotation, bool is_double) override {
        if (rotation == nullptr) return;

        if (is_double) {
            const auto* d = (const double*)rotation;
            basis::record(view_index, basis::from_rotator(d[0], d[1], d[2]));
        } else {
            basis::record(view_index, basis::from_rotator(rotation->pitch, rotation->yaw, rotation->roll));
        }
    }

    void on_present() override { log_stats(); }

    void on_custom_event(const char* event_name, const char* event_data) override {
        if (event_name == nullptr || strcmp(event_name, "sprite_plane") != 0) return;

        g_enabled = event_data == nullptr || event_data[0] != '0';
        plugin_log::info("[SpritePlane] replacement %s", g_enabled ? "on" : "off");
    }

private:
    // Every 5 s while anything happened: how many eye views got the head's axes, how many were no eye's, how many were
    // searched in vain and how many could not be read.
    void log_stats() {
        const auto now = std::chrono::steady_clock::now();

        if (now - m_last_stats < std::chrono::seconds(5)) return;

        m_last_stats = now;
        const auto s = view::stats();

        if (s.patched == m_last.patched && s.not_eye == m_last.not_eye && s.unfound == m_last.unfound && s.faults == m_last.faults) return;

        plugin_log::info("[SpritePlane] last 5 s: %llu eye view(s) given the head's axes, %llu view(s) no eye's, %llu searched without "
                         "finding ViewForward, %llu unreadable", (unsigned long long)(s.patched - m_last.patched),
                         (unsigned long long)(s.not_eye - m_last.not_eye), (unsigned long long)(s.unfound - m_last.unfound),
                         (unsigned long long)(s.faults - m_last.faults));
        m_last = s;
    }

    std::chrono::steady_clock::time_point m_last_stats{};
    view::Stats m_last{};
};

std::unique_ptr<SpritePlane> g_plugin{new SpritePlane{}};
}
