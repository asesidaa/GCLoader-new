#pragma once
#include <vector>
#include "Patches/WindowedWidescreen/WindowedWidescreenFeature.h"
#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include <atomic>
#include <memory>
#include <span>
#include <string_view>

namespace gc::windowed_widescreen::detail {
void ReportUnknownTaskIdentity(void*, std::uintptr_t) noexcept;
void ReportUnknownTaskCapacity(void*) noexcept;
enum class GameplayFeedbackDrawScope : std::uint8_t
{
    none,
    bar, counter, direct_effect, effect_packet, stage_title, stage_players, timed_text,
};


enum class GameplayEffectOwner : std::uint8_t { none, judgement_text, tutorial };

struct OwnedGameplayPacket final
{
    std::uintptr_t address{};
    GameplayEffectOwner owner{};
};

struct WindowedWidescreenRuntime
{
    WindowedWidescreenRuntime(
        WindowedWidescreenSettings configured_settings,
        const ResolutionModel configured_resolution,
        PreparedWindowPlacement configured_placement,
        const WidescreenGameAbi configured_abi) noexcept
        : settings{configured_settings},
          resolution{configured_resolution},
          placement{configured_placement},
          abi{configured_abi},
          classifier{
              abi.common_2d_vtable, abi.common_3d_vtable,
              PassClassifierDiagnosticSink{
                  .unknown_identity = &ReportUnknownTaskIdentity,
                  .unknown_capacity_exhausted =
                  &ReportUnknownTaskCapacity,
              }
          },
          device{resolution, GameplayHudPlacement::center, abi.layout},
          compositor{
              resolution.output_size(),
              GameplayHudPlacement::center,
              RenderThreadIdProvider{
                  .current = +[](void*) noexcept
                  {
                      return static_cast<std::uint32_t>(
                          GetCurrentThreadId());
                  },
              },
              device.DeviceActions()
          }
    {
    }

    WindowedWidescreenSettings settings;
    ResolutionModel resolution;
    PreparedWindowPlacement placement;
    const WidescreenGameAbi abi;
    std::atomic_bool active{};
    PassClassifier classifier;
    D3D9CompositorDevice device;
    NativeCanvasCompositor compositor;
    std::optional<NativeWindowPolicyError>
    last_window_policy_error;
    std::optional<renderer_device_loss::RendererResourceError>
    last_resource_error;
    std::optional<CompositorError> last_compositor_error;
    bool pending_batches_reported{};
    std::atomic_bool gameplay_frame_active{};
    std::atomic_uint32_t frame_sequence{};
    std::atomic_uint32_t network_status_log_mask{};
    std::uintptr_t active_gameplay_tune{};
    GameplayFeedbackDrawScope gameplay_feedback_draw_scope{
        GameplayFeedbackDrawScope::none
    };
    bool effect_root_active{};
    GameplayEffectOwner effect_root_owner{};
    std::vector<OwnedGameplayPacket> effect_packets;
    std::uint32_t gameplay_render_thread{};
    bool test_mode_native_active{};
};

extern std::atomic<WindowedWidescreenRuntime*> g_callback_runtime;
[[nodiscard]] bool RuntimeCallbacksAreActive(const WindowedWidescreenRuntime& runtime) noexcept;
[[noreturn]] void PublishRuntimeFatal(
    const WindowedWidescreenError& error) noexcept;
[[nodiscard]] bool ProductionRead(
    void*,
    const std::uintptr_t address,
    const std::span<std::byte> output) noexcept;
[[nodiscard]] bool ReadRuntimePointer(
    void*,
    const std::uintptr_t address,
    std::uintptr_t& value) noexcept;
[[noreturn]] void PublishRenderRuntimeFatal(
    WindowedWidescreenRuntime& runtime,
    WindowedWidescreenError error) noexcept;
enum class RenderDimensionAxis : std::uint8_t
    {
width,
height,
    };

    enum class RenderQueryRoute : std::uint8_t
    {
native_passthrough,
frame_virtualized,
    };

    [[nodiscard]] RenderQueryRoute ResolveRenderQueryRoute(
bool compositor_frame_active) noexcept;

    void ApplyNativeHudOrthographicArguments(
HudOrthographicArguments& arguments) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    ApplyClipGateHook(
std::uintptr_t continuation,
std::uint32_t& instruction_pointer) noexcept;

}
