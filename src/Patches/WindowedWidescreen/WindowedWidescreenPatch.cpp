#include "Patches/WindowedWidescreen/WindowedWidescreenPatch.h"

#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"

#include <Windows.h>
#include "Diagnostics/FatalProcess.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include <plog/Log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

namespace gc::windowed_widescreen
{
    namespace
    {
        constexpr std::string_view kNetworkStatusNesysClipName = "imc_ico_n";
        constexpr std::string_view kNetworkStatusLocalClipName = "imc_ico_l";
        static_assert(sizeof(std::uintptr_t) == sizeof(std::uint32_t));

        [[nodiscard]] std::uint32_t MovieClipNameHash(
            const std::string_view name, const WidescreenNativeLayout& layout) noexcept
        {
            std::uint32_t value{};
            for (const auto character : name)
            {
                value = value * layout.movie_clip_name_hash_multiplier +
                    static_cast<std::uint8_t>(character);
            }
            return value;
        }

        void ReportUnknownTaskIdentity(
            void*,
            const std::uintptr_t identity) noexcept
        {
#if defined(_DEBUG)
            PLOG_WARNING << "WindowedWidescreen: unknown task vtable=0x"
                         << std::hex << identity;
#else
            (void)identity;
#endif
        }

        void ReportUnknownTaskCapacity(void*) noexcept
        {
#if defined(_DEBUG)
            PLOG_WARNING
                << "WindowedWidescreen: unknown task diagnostic capacity exhausted";
#endif
        }

        enum class GameplayFeedbackDrawScope : std::uint8_t
        {
            none,
            physical_gameplay_hud_overlay,
        };

        enum class NetworkStatusClip : std::uint8_t
        {
            none,
            nesys,
            local,
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
                  device{resolution, settings.gameplay_hud_placement(), abi.layout},
                  compositor{
                      resolution.output_size(),
                      settings.gameplay_hud_placement(),
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
            bool note_tutorial_group_active{};
            bool judgement_scope_reported{};
            bool note_tutorial_scope_reported{};
            bool test_mode_native_active{};
        };

        std::unique_ptr<WindowedWidescreenRuntime> g_runtime_owner;
        std::atomic<WindowedWidescreenRuntime*> g_callback_runtime{};
        struct WidescreenOriginals final {
            native::ConfigApply config_apply{};
            native::OwnerCall window_device_create{};
            native::OwnerCall frame_begin{};
            native::OwnerCall frame_end{};
            native::OwnerCall task_dispatch{};
            native::ResolutionSet logical_resolution_set{};
            native::TargetDimensionSet logical_target_width_set{};
            native::TargetDimensionSet logical_target_height_set{};
            native::ViewportReset viewport_reset{};
            native::MousePoll mouse_debug_poll{};
            native::MovieClipAccept network_status_movie_clip_accept{};
            native::ShapeDrawVisit network_status_shape_draw_visit{};
            native::IntDimension screen_width_int{};
            native::FloatDimension screen_width_float{};
            native::IntDimension screen_height_int{};
            native::FloatDimension screen_height_float{};
            native::IntDimension target_width_int{};
            native::FloatDimension target_width_float{};
            native::IntDimension target_height_int{};
            native::FloatDimension target_height_float{};
        };
        WidescreenOriginals g_originals;
        thread_local std::uint32_t g_network_status_native_scope_depth{};
        thread_local NetworkStatusClip g_network_status_active_clip{
            NetworkStatusClip::none};
        thread_local bool g_network_status_local_matrix_corrected{};

        class NetworkStatusNativeScope final
        {
        public:
            explicit NetworkStatusNativeScope(
                const NetworkStatusClip clip) noexcept
                : previous_clip_{g_network_status_active_clip},
                  previous_local_matrix_corrected_{
                      g_network_status_local_matrix_corrected}
            {
                ++g_network_status_native_scope_depth;
                g_network_status_active_clip = clip;
                g_network_status_local_matrix_corrected = false;
            }

            ~NetworkStatusNativeScope() noexcept
            {
                g_network_status_local_matrix_corrected =
                    previous_local_matrix_corrected_;
                g_network_status_active_clip = previous_clip_;
                if (g_network_status_native_scope_depth != 0)
                {
                    --g_network_status_native_scope_depth;
                }
            }

            [[nodiscard]] bool local_matrix_corrected() const noexcept
            {
                return g_network_status_local_matrix_corrected;
            }

            NetworkStatusNativeScope(const NetworkStatusNativeScope&) = delete;
            NetworkStatusNativeScope& operator=(
                const NetworkStatusNativeScope&) = delete;

        private:
            NetworkStatusClip previous_clip_{NetworkStatusClip::none};
            bool previous_local_matrix_corrected_{};
        };

        void ResetScopedRenderState(
            WindowedWidescreenRuntime& runtime) noexcept
        {
            runtime.active_gameplay_tune = 0;
            runtime.gameplay_frame_active.store(
                false,
                std::memory_order_release);
            runtime.gameplay_feedback_draw_scope =
                GameplayFeedbackDrawScope::none;
            runtime.note_tutorial_group_active = false;
            runtime.test_mode_native_active = false;
        }

        [[nodiscard]] bool RuntimeCallbacksAreActive(const WindowedWidescreenRuntime& runtime) noexcept {
            return runtime.active.load(std::memory_order_acquire);
        }

        [[noreturn]] void PublishRuntimeFatal(
            const WindowedWidescreenError& error) noexcept
        {
            std::string log;
            try
            {
                log = std::format(
                    "WindowedWidescreen: fatal stage={} window_policy_error={} resource_error={} d3d_stage={} d3d_hresult=0x{:08X} compositor_stage={} compositor_policy_error={} compositor_stable_space={} compositor_requested_space={} compositor_restore_attempted={} compositor_restore_succeeded={}",
                    static_cast<unsigned>(error.stage),
                    error.window_policy_error.has_value()
                        ? static_cast<int>(*error.window_policy_error)
                        : -1,
                    error.resource_error.has_value()
                        ? static_cast<int>(*error.resource_error)
                        : -1,
                    static_cast<unsigned>(error.d3d_failure.stage),
                    static_cast<std::uint32_t>(
                        error.d3d_failure.result),
                    error.compositor_error.has_value()
                        ? static_cast<int>(error.compositor_error->stage)
                        : -1,
                    error.compositor_error.has_value() &&
                    error.compositor_error->policy_error.has_value()
                        ? static_cast<int>(
                            *error.compositor_error->policy_error)
                        : -1,
                    error.compositor_error.has_value()
                        ? static_cast<int>(error.compositor_error->stable_space)
                        : -1,
                    error.compositor_error.has_value()
                        ? static_cast<int>(
                            error.compositor_error->requested_space)
                        : -1,
                    error.compositor_error.has_value() &&
                    error.compositor_error->restoration_attempted
                        ? 1
                        : 0,
                    error.compositor_error.has_value() &&
                    error.compositor_error->restoration_succeeded
                        ? 1
                        : 0);
            }
            catch (...)
            {
                log = "WindowedWidescreen: fatal rendering invariant";
            }
            gc::diagnostics::AbortProcess({
                std::move(log),
                L"The windowed widescreen renderer could not continue safely. "
                L"Check the GCLoader log for the failing stage.",
                L"GCLoader windowed widescreen error"});
        }

        [[nodiscard]] bool ProductionRead(
            void*,
            const std::uintptr_t address,
            const std::span<std::byte> output) noexcept
        {
            if (address == 0 || output.empty())
            {
                return false;
            }
            __try
            {
                std::memcpy(
                    output.data(),
                    reinterpret_cast<const void*>(address),
                    output.size());
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool FlushNativeBatches(void* opaque) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(opaque);
            if (!runtime || !runtime->abi.batch_flush) return false;
            __try { runtime->abi.batch_flush(); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        }

        [[nodiscard]] bool ReadNativeBatchCounts(
            WindowedWidescreenRuntime& runtime,
            NativeBatchCounts& pending) noexcept
        {
            std::uintptr_t queue_base{};
            if (!ProductionRead(
                    nullptr,
                    runtime.abi.batch_queue_pointer,
                    std::as_writable_bytes(std::span{&queue_base, 1})) ||
                queue_base == 0)
            {
                return false;
            }

            for (std::size_t index = 0; index < pending.size(); ++index)
            {
                const auto count_address = queue_base +
                    runtime.abi.layout.batch_pending_count_offset +
                    runtime.abi.layout.batch_queue_stride * index;
                if (!ProductionRead(
                    nullptr,
                    count_address,
                    std::as_writable_bytes(
                        std::span{&pending[index], 1})))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool NativeBatchesAreEmpty(void* opaque) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(opaque);
            NativeBatchCounts pending{};
            if (runtime == nullptr ||
                !ReadNativeBatchCounts(*runtime, pending))
            {
                return false;
            }

            const bool empty = std::ranges::all_of(
                pending,
                [](const std::uint32_t count) { return count == 0; });
#if defined(_DEBUG)
            if (!empty && !runtime->pending_batches_reported)
            {
                runtime->pending_batches_reported = true;
                PLOG_ERROR
                    << "WindowedWidescreen: native batches remained after flush counts="
                    << pending[0] << ',' << pending[1] << ','
                    << pending[2] << ',' << pending[3];
            }
#endif
            return empty;
        }

        [[nodiscard]] bool PrepareRendererParticipant(WindowedWidescreenRuntime* runtime) noexcept
        {
            runtime->device.SetNativeBatchActions(NativeBatchActions{
                .context = runtime,
                .flush = &FlushNativeBatches,
                .empty = &NativeBatchesAreEmpty,
            });
            const auto failure_publisher =
                renderer_device_loss::RendererDeviceLossSetResetFailureActions({
                    .context = runtime,
                    .failure = +[](void* opaque_runtime,
                        renderer_device_loss::RendererResetLifecycleStage stage,
                        renderer_device_loss::RendererResourceError error) noexcept {
                        auto* owner = static_cast<WindowedWidescreenRuntime*>(opaque_runtime);
                        PublishRuntimeFatal({
                            .stage = stage == renderer_device_loss::RendererResetLifecycleStage::before_reset
                                ? WindowedWidescreenOperationStage::reset_pre
                                : WindowedWidescreenOperationStage::reset_post,
                            .resource_error = error,
                            .d3d_failure = owner->device.last_failure(),
                        });
                    },
                });
            if (!failure_publisher) {
                runtime->last_resource_error = failure_publisher.error();
                return false;
            }
            g_callback_runtime.store(runtime, std::memory_order_release);
            const auto attached =
                renderer_device_loss::RendererDeviceLossAttachResource(
                    renderer_device_loss::RendererResourceParticipant{
                        .context = runtime,
                        .create = +[](
                        void* opaque_runtime,
                        const std::uintptr_t renderer_owner) noexcept
                        {
                            auto* owner = static_cast<
                                WindowedWidescreenRuntime*>(opaque_runtime);
                            return owner != nullptr &&
                                owner->device.Create(renderer_owner);
                        },
                        .release = +[](void* opaque_runtime) noexcept
                        {
                            auto* owner = static_cast<
                                WindowedWidescreenRuntime*>(opaque_runtime);
                            if (owner != nullptr)
                            {
                                ResetScopedRenderState(*owner);
                                owner->compositor.ResetForDeviceLoss();
                                owner->device.Release();
                            }
                        },
                    });
            if (!attached)
            {
                runtime->last_resource_error = attached.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ConfigVtableMatches(
            void* context,
            const std::uintptr_t config) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr || config == 0)
            {
                return false;
            }
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                return vtable ==
                    runtime->abi.main_config_vtable;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigWidth(
            void* context,
            const std::uintptr_t config,
            const std::uint32_t value,
            const int trailing) noexcept
        {
            __try
            {
                const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_width_setter;
                (void)setter(
                    reinterpret_cast<void*>(config),
                    static_cast<int>(value),
                    trailing);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigHeight(
            void* context,
            const std::uintptr_t config,
            const std::uint32_t value,
            const int trailing) noexcept
        {
            __try
            {
                const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_height_setter;
                (void)setter(
                    reinterpret_cast<void*>(config),
                    static_cast<int>(value),
                    trailing);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigResize(
            void* context,
            const std::uintptr_t config,
            const bool value) noexcept
        {
            __try
            {
                const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_resize_setter;
                setter(reinterpret_cast<void*>(config), value ? 1 : 0);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigMinmax(
            void* context,
            const std::uintptr_t config,
            const bool minimize,
            const bool maximize) noexcept
        {
            __try
            {
                const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_minmax_setter;
                setter(
                    reinterpret_cast<void*>(config),
                    minimize ? 1 : 0,
                    maximize ? 1 : 0);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigMode(
            void* context,
            const std::uintptr_t config,
            const int first,
            const int second,
            const int third,
            const int fourth) noexcept
        {
            __try
            {
                const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_mode_setter;
                (void)setter(
                    reinterpret_cast<void*>(config),
                    first,
                    second,
                    third,
                    fourth);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] int CallConfigOriginal(
            void* context,
            const std::uintptr_t config) noexcept
        {
            return context && g_originals.config_apply
                ? g_originals.config_apply(static_cast<int>(config)) : 0;

        }

        [[nodiscard]] int CallWindowOriginal(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            return context && g_originals.window_device_create
                ? g_originals.window_device_create(reinterpret_cast<void*>(renderer)) : 0;

        }

        [[nodiscard]] int CallLogicalResolutionSetOriginal(
            void* context,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            return context && g_originals.logical_resolution_set
                ? g_originals.logical_resolution_set(static_cast<int>(width), static_cast<int>(height)) : 0;

        }

        template <WidescreenContractSite Site>
        [[nodiscard]] int CallLogicalTargetDimensionSetOriginal(
            void* context,
            const std::uint32_t value) noexcept
        {
            const auto original = Site == WidescreenContractSite::logical_target_width_set
                ? g_originals.logical_target_width_set : g_originals.logical_target_height_set;
            return context && original ? original(static_cast<int>(value)) : 0;

        }

        [[nodiscard]] bool ValidateAndPlaceWindow(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr)
            {
                return false;
            }
            runtime->last_window_policy_error.reset();
            const auto placed = ValidateAndPlaceRendererWindow(
                renderer,
                runtime->placement, runtime->abi.layout);
            if (!placed)
            {
                runtime->last_window_policy_error = placed.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ActivateRendererResources(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr)
            {
                return false;
            }
            runtime->last_resource_error.reset();
            const auto activated =
                renderer_device_loss::RendererDeviceLossOnDeviceCreated(
                    renderer);
            if (!activated)
            {
                runtime->last_resource_error = activated.error();
                return false;
            }
            return runtime->device.active();
        }

        [[nodiscard]] int CallFrameBeginOriginal(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            return context && g_originals.frame_begin
                ? g_originals.frame_begin(reinterpret_cast<void*>(renderer)) : 0;

        }

        [[nodiscard]] int CallFrameEndOriginal(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            return context && g_originals.frame_end
                ? g_originals.frame_end(reinterpret_cast<void*>(renderer)) : 0;

        }

        [[nodiscard]] bool BeginCompositorFrame(void* context) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr || !runtime->device.active())
            {
                return false;
            }
            const auto begun = runtime->compositor.BeginFrame();
            if (!begun)
            {
                runtime->last_compositor_error = begun.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool EndCompositorFrame(void* context) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr || !runtime->device.active())
            {
                return false;
            }
            const auto ended = runtime->compositor.EndFrame();
            if (!ended)
            {
                runtime->last_compositor_error = ended.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadRuntimePointer(
            void*,
            const std::uintptr_t address,
            std::uintptr_t& value) noexcept
        {
            return ProductionRead(
                nullptr,
                address,
                std::as_writable_bytes(std::span{&value, 1}));
        }

        [[nodiscard]] bool RequestRuntimeSpace(
            void* context,
            const RenderSpace requested) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr || !runtime->device.active())
            {
                return false;
            }
            const auto changed = runtime->compositor.RequestSpace(requested);
            if (!changed)
            {
                runtime->last_compositor_error = changed.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool RequestRuntimeGameplayHudPlacement(
            WindowedWidescreenRuntime& runtime,
            const GameplayHudPlacement placement) noexcept
        {
            if (!runtime.device.active())
            {
                return false;
            }
            const auto changed =
                runtime.compositor.SetGameplayHudPlacement(placement);
            if (!changed)
            {
                runtime.last_compositor_error = changed.error();
                return false;
            }
            return true;
        }

        [[nodiscard]] int CallTaskDispatchOriginal(
            void* context,
            const std::uintptr_t task_node) noexcept
        {
            return context && g_originals.task_dispatch
                ? g_originals.task_dispatch(reinterpret_cast<void*>(task_node)) : 0;

        }

        [[nodiscard]] bool ReadCurrentDimensions(
            void* context,
            RenderDimensions& dimensions) noexcept
        {
            if (g_network_status_native_scope_depth != 0)
            {
                dimensions = RenderDimensions{
                    .width = kNativeWidth,
                    .height = kNativeHeight,
                    .width_float = static_cast<float>(kNativeWidth),
                    .height_float = static_cast<float>(kNativeHeight),
                };
                return true;
            }
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr)
            {
                return false;
            }
            const auto current = runtime->compositor.CurrentDimensions();
            if (!current)
            {
                return false;
            }
            dimensions = *current;
            return true;
        }

        [[nodiscard]] bool ReadCurrentViewport(
            void* context,
            NativeViewport& viewport) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            if (runtime == nullptr)
            {
                return false;
            }
            if (g_network_status_native_scope_depth != 0)
            {
                const auto base_hud = ResolveGameplayHudViewport(
                    runtime->resolution.output_size(),
                    runtime->settings.gameplay_hud_placement());
                if (!base_hud)
                {
                    return false;
                }
                viewport = NativeViewport{
                    .x = static_cast<float>(base_hud->x),
                    .y = static_cast<float>(base_hud->y),
                    .width = static_cast<float>(base_hud->width),
                    .height = static_cast<float>(base_hud->height),
                };
                return true;
            }
            const auto current_space = runtime->compositor.CurrentSpace();
            const auto dimensions = runtime->compositor.CurrentDimensions();
            if (!current_space || !dimensions)
            {
                return false;
            }

            viewport = NativeViewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = dimensions->width_float,
                .height = dimensions->height_float,
            };
            if (*current_space != RenderSpace::gameplay_hud)
            {
                return true;
            }

            const auto gameplay_viewport = ResolveGameplayHudViewport(
                runtime->resolution.output_size(),
                runtime->compositor.gameplay_hud_placement());
            if (!gameplay_viewport)
            {
                return false;
            }
            viewport.x = static_cast<float>(gameplay_viewport->x);
            viewport.y = static_cast<float>(gameplay_viewport->y);
            return true;
        }

        [[nodiscard]] int CallViewportOriginal(
            void* context,
            const NativeViewport* viewport) noexcept
        {
            return context && g_originals.viewport_reset
                ? g_originals.viewport_reset(viewport) : 0;

        }

        [[noreturn]] void PublishRenderRuntimeFatal(
            WindowedWidescreenRuntime& runtime,
            WindowedWidescreenError error) noexcept
        {
            error.compositor_error = runtime.last_compositor_error;
            error.d3d_failure = runtime.device.last_failure();
            PublishRuntimeFatal(error);
        }


        [[nodiscard]] bool RuntimeCStringEquals(
            const std::uintptr_t address,
            const std::string_view expected) noexcept
        {
            std::array<char, 16> actual{};
            if (address == 0 || expected.size() + 1 > actual.size())
            {
                return false;
            }
            const auto output = std::as_writable_bytes(std::span{actual}).
                first(expected.size() + 1);
            return ProductionRead(nullptr, address, output) &&
                std::memcmp(actual.data(), expected.data(), expected.size()) ==
                0 &&
                actual[expected.size()] == '\0';
        }

        [[nodiscard]] NetworkStatusClip IdentifyNetworkStatusClip(
            const WidescreenNativeLayout& layout, void* const movie_clip) noexcept
        {
            if (movie_clip == nullptr)
            {
                return NetworkStatusClip::none;
            }

            const auto address = reinterpret_cast<std::uintptr_t>(movie_clip);
            std::uint32_t name_hash{};
            if (!ProductionRead(
                nullptr,
                address + layout.movie_clip_name_hash_offset,
                std::as_writable_bytes(std::span{&name_hash, 1})))
            {
                return NetworkStatusClip::none;
            }

            NetworkStatusClip target = NetworkStatusClip::none;
            std::string_view expected_name{};
            if (name_hash == MovieClipNameHash(kNetworkStatusNesysClipName, layout))
            {
                target = NetworkStatusClip::nesys;
                expected_name = kNetworkStatusNesysClipName;
            }
            else if (name_hash == MovieClipNameHash(kNetworkStatusLocalClipName, layout))
            {
                target = NetworkStatusClip::local;
                expected_name = kNetworkStatusLocalClipName;
            }
            else
            {
                return NetworkStatusClip::none;
            }

            std::uintptr_t name_address{};
            if (!ReadRuntimePointer(
                    nullptr,
                    address + layout.movie_clip_name_offset,
                    name_address) ||
                !RuntimeCStringEquals(name_address, expected_name))
            {
                return NetworkStatusClip::none;
            }
            return target;
        }

        [[nodiscard]] bool IsMovieClipDrawVisitor(
            const WindowedWidescreenRuntime& runtime,
            void* const visitor) noexcept
        {
            std::uintptr_t vtable{};
            return visitor != nullptr &&
                ReadRuntimePointer(
                    nullptr,
                    reinterpret_cast<std::uintptr_t>(visitor),
                    vtable) &&
                vtable == runtime.abi.movie_clip_draw_visitor_vtable;
        }

        [[nodiscard]] constexpr const char* NetworkStatusClipName(
            const NetworkStatusClip clip) noexcept
        {
            return clip == NetworkStatusClip::nesys
                       ? "imc_ico_n"
                       : clip == NetworkStatusClip::local
                       ? "imc_ico_l"
                       : "none";
        }

        [[nodiscard]] bool NetworkStatusMatrixIsWritable(
            const std::uintptr_t address) noexcept
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (address == 0 || VirtualQuery(
                    reinterpret_cast<const void*>(address),
                    &memory,
                    sizeof(memory)) != sizeof(memory) ||
                memory.State != MEM_COMMIT ||
                (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }

            const auto protection = memory.Protect & 0xFFU;
            const bool writable = protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY ||
                protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
            const auto region_begin = reinterpret_cast<std::uintptr_t>(
                memory.BaseAddress);
            if (!writable || address < region_begin ||
                memory.RegionSize >
                std::numeric_limits<std::uintptr_t>::max() - region_begin)
            {
                return false;
            }
            const auto region_end = region_begin + memory.RegionSize;
            constexpr auto matrix_size =
                sizeof(NativeNetworkMatrix);
            return address <= region_end && matrix_size <= region_end - address;
        }

        class ScopedLocalNetworkStatusMatrix final
        {
        public:
            ScopedLocalNetworkStatusMatrix(
                const WindowedWidescreenRuntime& runtime,
                void* const visitor) noexcept
            {
                const auto output = runtime.resolution.output_size();
                const auto base_hud = ResolveGameplayHudViewport(
                    output,
                    runtime.settings.gameplay_hud_placement());
                if (visitor == nullptr || output.width <= kNativeWidth ||
                    output.height != kNativeHeight || !base_hud)
                {
                    return;
                }

                std::uintptr_t matrix_address{};
                if (!ReadRuntimePointer(
                        nullptr,
                        reinterpret_cast<std::uintptr_t>(visitor) +
                        runtime.abi.layout.network_status_visitor_matrix_stack_offset,
                        matrix_address) ||
                    !NetworkStatusMatrixIsWritable(matrix_address) ||
                    !ProductionRead(
                        nullptr,
                        matrix_address,
                        std::as_writable_bytes(std::span{original_})))
                {
                    return;
                }

                auto corrected = original_;
                const auto horizontal_scale =
                    static_cast<float>(kNativeWidth) /
                    static_cast<float>(output.width);
                for (const auto component : kNativeMatrixHorizontalComponents)
                    corrected[component] *= horizontal_scale;
                corrected[kNativeMatrixTranslation] = horizontal_scale *
                    (corrected[kNativeMatrixTranslation] + static_cast<float>(base_hud->x));

                matrix_ = reinterpret_cast<float*>(matrix_address);
                std::memcpy(
                    matrix_,
                    corrected.data(),
                    sizeof(corrected));
                applied_ = true;
            }

            ~ScopedLocalNetworkStatusMatrix() noexcept
            {
                if (applied_)
                {
                    std::memcpy(
                        matrix_,
                        original_.data(),
                        sizeof(original_));
                }
            }

            ScopedLocalNetworkStatusMatrix(
                const ScopedLocalNetworkStatusMatrix&) = delete;
            ScopedLocalNetworkStatusMatrix& operator=(
                const ScopedLocalNetworkStatusMatrix&) = delete;

            [[nodiscard]] bool applied() const noexcept
            {
                return applied_;
            }

        private:
            NativeNetworkMatrix original_{};
            float* matrix_{};
            bool applied_{};
        };

        void CallShapeDrawVisitOriginal(
            void* const visitor,
            void* const definition) noexcept
        {
            if (g_originals.network_status_shape_draw_visit && visitor && definition)
                g_originals.network_status_shape_draw_visit(visitor, definition);

        }

        void __fastcall NetworkStatusShapeDrawVisitDetour(
            void* const visitor,
            void*,
            void* const definition) noexcept
        {
            auto* const runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime != nullptr && RuntimeCallbacksAreActive(*runtime) &&
                definition != nullptr &&
                g_network_status_native_scope_depth != 0 &&
                g_network_status_active_clip == NetworkStatusClip::local &&
                runtime->gameplay_frame_active.load(std::memory_order_acquire))
            {
                const ScopedLocalNetworkStatusMatrix corrected{
                    *runtime,
                    visitor};
                if (corrected.applied())
                {
                    g_network_status_local_matrix_corrected = true;
                }
                CallShapeDrawVisitOriginal(visitor, definition);
                return;
            }
            CallShapeDrawVisitOriginal(visitor, definition);
        }

        [[nodiscard]] int CallMovieClipAcceptOriginal(
            WindowedWidescreenRuntime* const runtime,
            void* const movie_clip,
            void* const visitor) noexcept
        {
            (void)runtime;
            return g_originals.network_status_movie_clip_accept && movie_clip && visitor
                ? g_originals.network_status_movie_clip_accept(movie_clip, visitor) : 0;

        }

        void LogNetworkStatusCorrection(
            WindowedWidescreenRuntime& runtime,
            const NetworkStatusClip clip,
            void* const movie_clip,
            const RenderSpace space,
            const char* const action,
            const bool succeeded) noexcept
        {
            const auto clip_bit = clip == NetworkStatusClip::nesys ? 0U : 1U;
            const auto outcome_bit = succeeded ? 0U : 2U;
            const auto log_bit = 1U << (clip_bit + outcome_bit);
            const auto previous = runtime.network_status_log_mask.fetch_or(
                log_bit,
                std::memory_order_relaxed);
            if ((previous & log_bit) != 0)
            {
                return;
            }
            try
            {
                if (succeeded)
                {
                    PLOG_INFO
                        << "WindowedWidescreen network-status clip correction"
                        << " clip="
                        << NetworkStatusClipName(clip)
                        << " object=0x" << std::hex
                        << reinterpret_cast<std::uintptr_t>(movie_clip)
                        << std::dec
                        << " frame="
                        << runtime.frame_sequence.load(
                            std::memory_order_relaxed)
                        << " space=" << static_cast<int>(space)
                        << " action=" << action;
                }
                else
                {
                    PLOG_WARNING
                        << "WindowedWidescreen network-status clip correction"
                        << " failed clip="
                        << NetworkStatusClipName(clip)
                        << " frame="
                        << runtime.frame_sequence.load(
                            std::memory_order_relaxed)
                        << " space=" << static_cast<int>(space)
                        << " action=" << action;
                }
            }
            catch (...)
            {
            }
        }

        int __fastcall NetworkStatusMovieClipAcceptDetour(
            void* const movie_clip,
            void*,
            void* const visitor) noexcept
        {
            auto* const runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            const auto original = [&]() noexcept
            {
                return CallMovieClipAcceptOriginal(
                    runtime,
                    movie_clip,
                    visitor);
            };
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
                !runtime->gameplay_frame_active.load(
                    std::memory_order_acquire))
            {
                return original();
            }

            if (!IsMovieClipDrawVisitor(*runtime, visitor))
            {
                return original();
            }
            const auto clip = IdentifyNetworkStatusClip(runtime->abi.layout, movie_clip);
            if (clip == NetworkStatusClip::none ||
                !runtime->compositor.frame_active())
            {
                return original();
            }
            const auto current_space = runtime->compositor.CurrentSpace();
            if (!current_space)
            {
                return original();
            }

            if (*current_space == RenderSpace::gameplay_hud)
            {
                const auto previous =
                    runtime->compositor.gameplay_hud_placement();
                if (!runtime->compositor.ReapplyGameplayHudPlacement(
                        runtime->settings.gameplay_hud_placement()))
                {
                    LogNetworkStatusCorrection(
                        *runtime,
                        clip,
                        movie_clip,
                        *current_space,
                        "gameplay-hud-base-begin",
                        false);
                    return original();
                }

                int result{};
                bool restored{};
                bool local_matrix_corrected{};
                {
                    const NetworkStatusNativeScope native_scope{clip};
                    result = original();
                    local_matrix_corrected =
                        native_scope.local_matrix_corrected();
                    restored = runtime->compositor.
                        ReapplyGameplayHudPlacement(previous).has_value();
                }
                const bool corrected = clip != NetworkStatusClip::local ||
                    local_matrix_corrected;
                LogNetworkStatusCorrection(
                    *runtime,
                    clip,
                    movie_clip,
                    *current_space,
                    !restored
                        ? "gameplay-hud-base-restore"
                        : corrected
                        ? clip == NetworkStatusClip::local
                              ? "gameplay-hud-local-matrix"
                              : "gameplay-hud-base"
                        : "gameplay-hud-local-matrix-unavailable",
                    restored && corrected);
                return result;
            }

            if (*current_space != RenderSpace::physical_3d ||
                runtime->compositor.physical_gameplay_hud_overlay_active())
            {
                return original();
            }

            const auto begun =
                runtime->compositor.BeginPhysicalGameplayHudOverlay(
                    runtime->settings.gameplay_hud_placement());
            if (!begun)
            {
                LogNetworkStatusCorrection(
                    *runtime,
                    clip,
                    movie_clip,
                    *current_space,
                    "physical-base-overlay-begin",
                    false);
                return original();
            }

            int result{};
            bool ended{};
            bool local_matrix_corrected{};
            {
                const NetworkStatusNativeScope native_scope{clip};
                result = original();
                local_matrix_corrected =
                    native_scope.local_matrix_corrected();
                const auto ended_result =
                    runtime->compositor.EndPhysicalGameplayHudOverlay();
                ended = ended_result.has_value();
            }
            const bool corrected = clip != NetworkStatusClip::local ||
                local_matrix_corrected;
            LogNetworkStatusCorrection(
                *runtime,
                clip,
                movie_clip,
                *current_space,
                !ended
                    ? "physical-base-overlay-end"
                    : corrected
                    ? clip == NetworkStatusClip::local
                          ? "physical-local-matrix"
                          : "physical-base-overlay"
                    : "physical-local-matrix-unavailable",
                ended && corrected);
            return result;
        }

        int __fastcall TaskDispatchDetour(
            void* const task_node,
            void*) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallTaskDispatchOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(task_node));
            }
            std::uintptr_t task{}, vtable{};
            if (!ReadRuntimePointer(nullptr, reinterpret_cast<std::uintptr_t>(task_node), task) ||
                !ReadRuntimePointer(nullptr, task, vtable)) vtable = 0;
            if (!RequestRuntimeSpace(runtime, runtime->classifier.ClassifyTask(vtable)))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::task_dispatch});
            return CallTaskDispatchOriginal(runtime, reinterpret_cast<std::uintptr_t>(task_node));
        }

        void RequestGameplayPass(const GameplayPass pass) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (!RequestRuntimeSpace(runtime, PassClassifier::ClassifyGameplay(pass)))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::render_transition});
        }

        void GameplayStageBackgroundMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime != nullptr && RuntimeCallbacksAreActive(*runtime))
            {
                runtime->gameplay_frame_active.store(
                    true,
                    std::memory_order_release);
            }
            RequestGameplayPass(GameplayPass::stage_background);
        }

        void GameplayTrackMid(safetyhook::Context& context) noexcept
        {
            RequestGameplayPass(GameplayPass::perspective_track);
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (context.ecx == 0 || runtime->active_gameplay_tune != 0 ||
                runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::none ||
                runtime->note_tutorial_group_active)
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            runtime->active_gameplay_tune =
                static_cast<std::uintptr_t>(context.ecx);
        }

        template <std::size_t SlotCount>
        [[nodiscard]] bool TryMatchEffectSlots(
            const WidescreenNativeLayout& layout, const std::uintptr_t tune,
            const std::uintptr_t effect,
            const std::array<std::uint32_t, SlotCount>& slots,
            bool& matches) noexcept
        {
            matches = false;
            const auto collection_extent =
                layout.tune_effect_collection_offset + layout.pointer_collection_end_offset;
            if (tune == 0 || effect == 0 ||
                tune > std::numeric_limits<std::uintptr_t>::max() -
                collection_extent)
            {
                return false;
            }

            const auto collection = tune + layout.tune_effect_collection_offset;
            std::uintptr_t begin{};
            std::uintptr_t end{};
            if (!ReadRuntimePointer(
                    nullptr,
                    collection + layout.pointer_collection_begin_offset,
                    begin) ||
                !ReadRuntimePointer(
                    nullptr,
                    collection + layout.pointer_collection_end_offset,
                    end) ||
                begin == 0 || end < begin ||
                (end - begin) % sizeof(std::uintptr_t) != 0)
            {
                return false;
            }

            const auto count =
                (end - begin) / sizeof(std::uintptr_t);
            if (slots.empty() || count <= slots.back())
            {
                return false;
            }

            for (const auto slot : slots)
            {
                const auto byte_offset =
                    static_cast<std::size_t>(slot) * sizeof(std::uintptr_t);
                if (begin >
                    std::numeric_limits<std::uintptr_t>::max() - byte_offset)
                {
                    return false;
                }
                std::uintptr_t candidate{};
                if (!ReadRuntimePointer(
                    nullptr,
                    begin + byte_offset,
                    candidate))
                {
                    return false;
                }
                matches = matches || candidate == effect;
            }
            return true;
        }

        [[nodiscard]] bool TryMatchPlayerOneJudgementEffect(
            const WidescreenNativeLayout& layout, const std::uintptr_t tune,
            const std::uintptr_t effect,
            bool& matches) noexcept
        {
            return TryMatchEffectSlots(layout, tune, effect, kPlayerOneJudgementSlots, matches);

        }

        void GameplayEffectsMid(safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (context.ecx == 0 || runtime->active_gameplay_tune == 0 ||
                runtime->active_gameplay_tune !=
                static_cast<std::uintptr_t>(context.ecx) ||
                runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::none ||
                runtime->note_tutorial_group_active)
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            RequestGameplayPass(GameplayPass::orthographic_effects);
        }

        void GameplayEffectsEndMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::none ||
                runtime->compositor.physical_gameplay_hud_overlay_active())
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            if (runtime->note_tutorial_group_active)
            {
                runtime->note_tutorial_group_active = false;
                if (!RequestRuntimeGameplayHudPlacement(
                    *runtime,
                    runtime->settings.gameplay_hud_placement()))
                {
                    PublishRenderRuntimeFatal(
                        *runtime,
                        WindowedWidescreenError{
                            .stage = WindowedWidescreenOperationStage::
                            gameplay_hud_placement,
                        });
                }
            }
            runtime->active_gameplay_tune = 0;
        }

        void GameplayFeedbackDrawBeginMid(
            safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
                runtime->active_gameplay_tune == 0)
            {
                return;
            }
            if (runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::none)
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }

            bool is_player_one_judgement{};
            if (!TryMatchPlayerOneJudgementEffect(
                runtime->abi.layout, runtime->active_gameplay_tune,
                static_cast<std::uintptr_t>(context.ecx),
                is_player_one_judgement))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            if (!is_player_one_judgement)
            {
                return;
            }

            const auto begun =
                runtime->compositor.BeginPhysicalGameplayHudOverlay(
                    GameplayHudPlacement::right);
            if (!begun)
            {
                runtime->last_compositor_error = begun.error();
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            runtime->gameplay_feedback_draw_scope =
                GameplayFeedbackDrawScope::physical_gameplay_hud_overlay;
            if (!runtime->judgement_scope_reported)
            {
                runtime->judgement_scope_reported = true;
                PLOG_INFO
                    << "WindowedWidescreen diagnostic: Player 1 judgement root entered right HUD overlay";
            }
        }

        void GameplayFeedbackDrawEndMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
                runtime->gameplay_feedback_draw_scope ==
                GameplayFeedbackDrawScope::none)
            {
                return;
            }
            if (runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::physical_gameplay_hud_overlay)
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            const auto ended =
                runtime->compositor.EndPhysicalGameplayHudOverlay();
            if (!ended)
            {
                runtime->last_compositor_error = ended.error();
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            runtime->gameplay_feedback_draw_scope =
                GameplayFeedbackDrawScope::none;
        }

        void NoteTutorialGroupBeginMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
                runtime->active_gameplay_tune == 0)
            {
                return;
            }
            if (runtime->note_tutorial_group_active ||
                runtime->gameplay_feedback_draw_scope !=
                GameplayFeedbackDrawScope::none ||
                !RequestRuntimeGameplayHudPlacement(
                    *runtime,
                    GameplayHudPlacement::right))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
            runtime->note_tutorial_group_active = true;
            if (!runtime->note_tutorial_scope_reported)
            {
                runtime->note_tutorial_scope_reported = true;
                PLOG_INFO
                    << "WindowedWidescreen diagnostic: note tutorial group entered right HUD viewport";
            }
        }

        void NoteTutorialGroupEndMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
                !runtime->note_tutorial_group_active)
            {
                return;
            }
            runtime->note_tutorial_group_active = false;
            if (!RequestRuntimeGameplayHudPlacement(
                *runtime,
                runtime->settings.gameplay_hud_placement()))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
        }

        void TestModeNativeBeginMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }

            // LoopLastTask calls IDirect3DDevice9::BeginScene directly, so the
            // game's normal frame wrapper never opens a compositor frame for
            // this standalone 2D traversal. Its direct EndScene is likewise
            // unconditional, making these two guarded sites the frame owner.
            if (runtime->test_mode_native_active)
            {
                return;
            }
            if (!BeginCompositorFrame(runtime))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        render_transition,
                    });
            }

            if (!RequestRuntimeSpace(runtime, RenderSpace::native_2d))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        render_transition,
                    });
            }
            runtime->test_mode_native_active = true;
        }

        void TestModeNativeEndMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (!runtime->test_mode_native_active)
            {
                return;
            }

            runtime->test_mode_native_active = false;
            if (!EndCompositorFrame(runtime))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        render_transition,
                    });
            }
        }

        [[nodiscard]] bool TryApplyNativeHudOrthographicArguments(
            const std::uint32_t stack_pointer) noexcept
        {
            __try
            {
                auto* const arguments =
                    reinterpret_cast<HudOrthographicArguments*>(
                        static_cast<std::uintptr_t>(stack_pointer));
                ApplyNativeHudOrthographicArguments(*arguments);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void GameplayHudProjectionMid(safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (!TryApplyNativeHudOrthographicArguments(context.esp))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage =
                        WindowedWidescreenOperationStage::projection,
                    });
            }
        }

        [[nodiscard]] bool TryReadComboEntry(
            const WidescreenNativeLayout& layout, const std::uint32_t frame_pointer,
            std::int32_t& entry) noexcept
        {
            if (frame_pointer < layout.combo_entry_frame_offset)
            {
                return false;
            }
            __try
            {
                entry = *reinterpret_cast<const std::int32_t*>(
                    static_cast<std::uintptr_t>(frame_pointer) -
                    layout.combo_entry_frame_offset);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void ComboBeginMid(safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }

            std::int32_t entry{};
            if (!TryReadComboEntry(runtime->abi.layout, context.ebp, entry) ||
                !RequestRuntimeGameplayHudPlacement(
                    *runtime,
                    ResolveComboHudPlacement(entry)))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
        }

        void ComboEndMid(safetyhook::Context&) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            if (!RequestRuntimeGameplayHudPlacement(
                *runtime,
                runtime->settings.gameplay_hud_placement()))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
        }

        template <typename Value>
        [[nodiscard]] Value CallDimensionOriginal(
            WindowedWidescreenRuntime& runtime,
            const WidescreenContractSite site) noexcept
        {
            (void)runtime;
            switch (site) {
            case WidescreenContractSite::screen_width_int:
                return g_originals.screen_width_int ? static_cast<Value>(g_originals.screen_width_int()) : Value{};
            case WidescreenContractSite::screen_width_float:
                return g_originals.screen_width_float ? static_cast<Value>(g_originals.screen_width_float()) : Value{};
            case WidescreenContractSite::screen_height_int:
                return g_originals.screen_height_int ? static_cast<Value>(g_originals.screen_height_int()) : Value{};
            case WidescreenContractSite::screen_height_float:
                return g_originals.screen_height_float ? static_cast<Value>(g_originals.screen_height_float()) : Value{};
            case WidescreenContractSite::target_width_int:
                return g_originals.target_width_int ? static_cast<Value>(g_originals.target_width_int()) : Value{};
            case WidescreenContractSite::target_width_float:
                return g_originals.target_width_float ? static_cast<Value>(g_originals.target_width_float()) : Value{};
            case WidescreenContractSite::target_height_int:
                return g_originals.target_height_int ? static_cast<Value>(g_originals.target_height_int()) : Value{};
            case WidescreenContractSite::target_height_float:
                return g_originals.target_height_float ? static_cast<Value>(g_originals.target_height_float()) : Value{};
            default: return Value{};
            }

        }

        [[nodiscard]] std::uint32_t ReadDimensionInt(
            const RenderDimensionAxis axis,
            const WidescreenContractSite site) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return axis == RenderDimensionAxis::width
                           ? kNativeWidth
                           : kNativeHeight;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallDimensionOriginal<std::uint32_t>(*runtime, site);
            }
            if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
                RenderQueryRoute::native_passthrough)
            {
                return CallDimensionOriginal<std::uint32_t>(*runtime, site);
            }
            RenderDimensions dimensions{};
            if (!ReadCurrentDimensions(runtime, dimensions))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::dimension_query});
            return axis == RenderDimensionAxis::width ? dimensions.width : dimensions.height;
        }

        [[nodiscard]] float ReadDimensionFloat(
            const RenderDimensionAxis axis,
            const WidescreenContractSite site) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return axis == RenderDimensionAxis::width
                           ? static_cast<float>(kNativeWidth)
                           : static_cast<float>(kNativeHeight);
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallDimensionOriginal<float>(*runtime, site);
            }
            if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
                RenderQueryRoute::native_passthrough)
            {
                return CallDimensionOriginal<float>(*runtime, site);
            }
            RenderDimensions dimensions{};
            if (!ReadCurrentDimensions(runtime, dimensions))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::dimension_query});
            return axis == RenderDimensionAxis::width ? dimensions.width_float : dimensions.height_float;
        }

        std::uint32_t __cdecl ScreenWidthIntDetour() noexcept
        {
            return ReadDimensionInt(
                RenderDimensionAxis::width,
                WidescreenContractSite::screen_width_int);
        }

        std::uint32_t __cdecl ScreenHeightIntDetour() noexcept
        {
            return ReadDimensionInt(
                RenderDimensionAxis::height,
                WidescreenContractSite::screen_height_int);
        }

        float __cdecl ScreenWidthFloatDetour() noexcept
        {
            return ReadDimensionFloat(
                RenderDimensionAxis::width,
                WidescreenContractSite::screen_width_float);
        }

        float __cdecl ScreenHeightFloatDetour() noexcept
        {
            return ReadDimensionFloat(
                RenderDimensionAxis::height,
                WidescreenContractSite::screen_height_float);
        }

        std::uint32_t __cdecl TargetWidthIntDetour() noexcept
        {
            return ReadDimensionInt(
                RenderDimensionAxis::width,
                WidescreenContractSite::target_width_int);
        }

        std::uint32_t __cdecl TargetHeightIntDetour() noexcept
        {
            return ReadDimensionInt(
                RenderDimensionAxis::height,
                WidescreenContractSite::target_height_int);
        }

        float __cdecl TargetWidthFloatDetour() noexcept
        {
            return ReadDimensionFloat(
                RenderDimensionAxis::width,
                WidescreenContractSite::target_width_float);
        }

        float __cdecl TargetHeightFloatDetour() noexcept
        {
            return ReadDimensionFloat(
                RenderDimensionAxis::height,
                WidescreenContractSite::target_height_float);
        }

        int __cdecl ViewportResetDetour(int* const viewport) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            const auto* typed =
                reinterpret_cast<const NativeViewport*>(viewport);
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallViewportOriginal(runtime, typed);
            }
            if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
                RenderQueryRoute::native_passthrough)
            {
                return CallViewportOriginal(runtime, typed);
            }
            NativeViewport current{};
            if (!ReadCurrentViewport(runtime, current))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::viewport});
            if (typed == nullptr) return CallViewportOriginal(runtime, &current);
            const NativeViewport translated{
                .x = current.x + typed->x, .y = current.y + typed->y,
                .width = typed->width, .height = typed->height};
            return CallViewportOriginal(runtime, &translated);
        }

        void ClipGateMid(safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            auto instruction_pointer = context.eip;
            const auto result = ApplyClipGateHook(
                runtime->abi.clip_continuation,
                instruction_pointer);
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            context.eip = instruction_pointer;
        }

        [[nodiscard]] std::uintptr_t CallMousePollOriginal(
            void* context,
            const std::uintptr_t owner,
            std::uint32_t* output) noexcept
        {
            return context && g_originals.mouse_debug_poll
                ? reinterpret_cast<std::uintptr_t>(g_originals.mouse_debug_poll(
                    reinterpret_cast<void*>(owner), output)) : 0;

        }

        POINT* __fastcall MouseDebugPollDetour(
            void* const owner,
            void*,
            std::uint32_t* const output) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return nullptr;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return reinterpret_cast<POINT*>(CallMousePollOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(owner),
                    output));
            }
            if (output == nullptr)
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::invalid_actions});
            const auto native_result = CallMousePollOriginal(
                runtime, reinterpret_cast<std::uintptr_t>(owner), output);
            const auto& layout = runtime->abi.layout;
            if (output[layout.mouse_valid_word] == 1) {
                const auto mapped = runtime->resolution.ClientToNative(
                    static_cast<std::int32_t>(output[layout.mouse_x_word]),
                    static_cast<std::int32_t>(output[layout.mouse_y_word]));
                if (!mapped) {
                    output[layout.mouse_valid_word] = 0;
                } else {
                    output[layout.mouse_x_word] = static_cast<std::uint32_t>(mapped->x);
                    output[layout.mouse_y_word] = static_cast<std::uint32_t>(mapped->y);
                }
            }
            return reinterpret_cast<POINT*>(native_result);
        }

        int __cdecl LogicalResolutionSetDetour(
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallLogicalResolutionSetOriginal(
                    runtime,
                    width,
                    height);
            }
            const auto result = CallLogicalResolutionSetOriginal(runtime, kNativeWidth, kNativeHeight);
            (void)CallLogicalTargetDimensionSetOriginal<WidescreenContractSite::logical_target_width_set>(runtime, width);
            (void)CallLogicalTargetDimensionSetOriginal<WidescreenContractSite::logical_target_height_set>(runtime, height);
            return result;
        }

        template <RenderDimensionAxis Axis, WidescreenContractSite Site>
        int __cdecl LogicalTargetDimensionSetDetour(
            const std::uint32_t value) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallLogicalTargetDimensionSetOriginal<Site>(
                    runtime,
                    value);
            }
            return CallLogicalTargetDimensionSetOriginal<Site>(runtime, value);
        }

        int __cdecl ConfigApplyDetour(const int config) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }

            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallConfigOriginal(
                    runtime,
                    static_cast<std::uintptr_t>(config));
            }
            const auto address = static_cast<std::uintptr_t>(config);
            const auto output = runtime->resolution.output_size();
            const auto result = CallConfigOriginal(runtime, address);
            if (!ConfigVtableMatches(runtime, address) ||
                !SetConfigWidth(runtime, address, output.width, 0) ||
                !SetConfigHeight(runtime, address, output.height, 0) ||
                !SetConfigResize(runtime, address, false) ||
                !SetConfigMinmax(runtime, address, true, false) ||
                !SetConfigMode(runtime, address, 1, 1, 1, 1))
                PublishRuntimeFatal({.stage = WindowedWidescreenOperationStage::config_override});
            return result;
        }

        int __fastcall WindowDeviceDetour(
            void* const renderer,
            void*) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallWindowOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(renderer));
            }
            const auto address = reinterpret_cast<std::uintptr_t>(renderer);
            const auto result = CallWindowOriginal(runtime, address);
            if (result == 0) return result;
            WindowedWidescreenOperationStage failed_stage{};
            if (!ValidateAndPlaceWindow(runtime, address))
                failed_stage = WindowedWidescreenOperationStage::window_policy;
            else if (!ActivateRendererResources(runtime, address))
                failed_stage = WindowedWidescreenOperationStage::resource_attach;
            if (failed_stage != WindowedWidescreenOperationStage::none)
                PublishRuntimeFatal({.stage = failed_stage,
                    .window_policy_error = runtime->last_window_policy_error,
                    .resource_error = runtime->last_resource_error,
                    .d3d_failure = runtime->device.last_failure()});
            return result;
        }

        int __fastcall FrameBeginDetour(
            void* const renderer,
            void*) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallFrameBeginOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(renderer));
            }
            runtime->frame_sequence.fetch_add(1, std::memory_order_relaxed);
            runtime->gameplay_frame_active.store(
                false,
                std::memory_order_release);
            if (!BeginCompositorFrame(runtime))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::frame_begin});
            const auto result = CallFrameBeginOriginal(runtime, reinterpret_cast<std::uintptr_t>(renderer));
            if (FAILED(static_cast<HRESULT>(result))) {
                // Native BeginScene failure can skip frame end and enter device reset.
                ResetScopedRenderState(*runtime);
                runtime->compositor.ResetForDeviceLoss();
            }
            return result;
        }

        int __fastcall FrameEndDetour(
            void* const renderer,
            void*) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (!RuntimeCallbacksAreActive(*runtime))
            {
                return CallFrameEndOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(renderer));
            }
            if (!EndCompositorFrame(runtime))
                PublishRenderRuntimeFatal(*runtime, {
                    .stage = WindowedWidescreenOperationStage::frame_end});
            return CallFrameEndOriginal(runtime, reinterpret_cast<std::uintptr_t>(renderer));
        }
    } // namespace

    RenderQueryRoute ResolveRenderQueryRoute(
        const bool compositor_frame_active) noexcept
    {
        return compositor_frame_active
                   ? RenderQueryRoute::frame_virtualized
                   : RenderQueryRoute::native_passthrough;
    }

    void ApplyNativeHudOrthographicArguments(
        HudOrthographicArguments& arguments) noexcept
    {
        static_assert(
            std::is_standard_layout_v<HudOrthographicArguments> &&
            sizeof(HudOrthographicArguments) == sizeof(float) * 6);
        arguments.right = static_cast<float>(kNativeWidth);
        arguments.bottom = static_cast<float>(kNativeHeight);
    }

    std::expected<void, WindowedWidescreenError> ApplyClipGateHook(
        const std::uintptr_t continuation,
        std::uint32_t& instruction_pointer) noexcept
    {
        if (continuation == 0 || continuation > std::numeric_limits<std::uint32_t>::max())
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::clip_bypass});
        instruction_pointer = static_cast<std::uint32_t>(continuation);
        return {};

    }

    game_version::VersionedOperation detail::BindWidescreenHook(
        WidescreenContractSite site, const game_version::SiteContract& contract, void* expected) noexcept {
        using namespace game_version;
        switch (site) {
        case WidescreenContractSite::config_apply:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ConfigApplyDetour),
                hooking::OriginalPublisher::To(&g_originals.config_apply)};
        case WidescreenContractSite::window_device_create:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&WindowDeviceDetour),
                hooking::OriginalPublisher::To(&g_originals.window_device_create)};
        case WidescreenContractSite::logical_resolution_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalResolutionSetDetour),
                hooking::OriginalPublisher::To(&g_originals.logical_resolution_set)};
        case WidescreenContractSite::logical_target_width_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalTargetDimensionSetDetour< RenderDimensionAxis::width, WidescreenContractSite::logical_target_width_set>),
                hooking::OriginalPublisher::To(&g_originals.logical_target_width_set)};
        case WidescreenContractSite::logical_target_height_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalTargetDimensionSetDetour< RenderDimensionAxis::height, WidescreenContractSite::logical_target_height_set>),
                hooking::OriginalPublisher::To(&g_originals.logical_target_height_set)};
        case WidescreenContractSite::frame_begin:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&FrameBeginDetour),
                hooking::OriginalPublisher::To(&g_originals.frame_begin)};
        case WidescreenContractSite::frame_end:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&FrameEndDetour),
                hooking::OriginalPublisher::To(&g_originals.frame_end)};
        case WidescreenContractSite::task_dispatch:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TaskDispatchDetour),
                hooking::OriginalPublisher::To(&g_originals.task_dispatch)};
        case WidescreenContractSite::network_status_movie_clip_accept: {
            auto bound = contract;
            auto* replacement = reinterpret_cast<void*>(&NetworkStatusMovieClipAcceptDetour);
            bound.original.size = sizeof(expected);
            std::memcpy(bound.original.bytes.data(), &expected, sizeof(expected));
            bound.installed.size = sizeof(replacement);
            std::memcpy(bound.installed.bytes.data(), &replacement, sizeof(replacement));
            return GlobalVtableSlotOperation{bound, expected, replacement,
                runtime_image::VtableOriginalPublisher::To(&g_originals.network_status_movie_clip_accept)};
        }
        case WidescreenContractSite::network_status_shape_draw_visit: {
            auto bound = contract;
            auto* replacement = reinterpret_cast<void*>(&NetworkStatusShapeDrawVisitDetour);
            bound.original.size = sizeof(expected);
            std::memcpy(bound.original.bytes.data(), &expected, sizeof(expected));
            bound.installed.size = sizeof(replacement);
            std::memcpy(bound.installed.bytes.data(), &replacement, sizeof(replacement));
            return GlobalVtableSlotOperation{bound, expected, replacement,
                runtime_image::VtableOriginalPublisher::To(&g_originals.network_status_shape_draw_visit)};
        }
        case WidescreenContractSite::test_mode_native_begin:
            return MidHookOperation{contract, &TestModeNativeBeginMid};
        case WidescreenContractSite::test_mode_native_end:
            return MidHookOperation{contract, &TestModeNativeEndMid};
        case WidescreenContractSite::screen_width_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenWidthIntDetour),
                hooking::OriginalPublisher::To(&g_originals.screen_width_int)};
        case WidescreenContractSite::screen_height_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenHeightIntDetour),
                hooking::OriginalPublisher::To(&g_originals.screen_height_int)};
        case WidescreenContractSite::screen_width_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenWidthFloatDetour),
                hooking::OriginalPublisher::To(&g_originals.screen_width_float)};
        case WidescreenContractSite::screen_height_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenHeightFloatDetour),
                hooking::OriginalPublisher::To(&g_originals.screen_height_float)};
        case WidescreenContractSite::target_width_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetWidthIntDetour),
                hooking::OriginalPublisher::To(&g_originals.target_width_int)};
        case WidescreenContractSite::target_height_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetHeightIntDetour),
                hooking::OriginalPublisher::To(&g_originals.target_height_int)};
        case WidescreenContractSite::target_width_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetWidthFloatDetour),
                hooking::OriginalPublisher::To(&g_originals.target_width_float)};
        case WidescreenContractSite::target_height_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetHeightFloatDetour),
                hooking::OriginalPublisher::To(&g_originals.target_height_float)};
        case WidescreenContractSite::viewport_reset:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ViewportResetDetour),
                hooking::OriginalPublisher::To(&g_originals.viewport_reset)};
        case WidescreenContractSite::mouse_debug_poll:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&MouseDebugPollDetour),
                hooking::OriginalPublisher::To(&g_originals.mouse_debug_poll)};
        case WidescreenContractSite::gameplay_stage_background:
            return MidHookOperation{contract, &GameplayStageBackgroundMid};
        case WidescreenContractSite::gameplay_track:
            return MidHookOperation{contract, &GameplayTrackMid};
        case WidescreenContractSite::gameplay_effects:
            return MidHookOperation{contract, &GameplayEffectsMid};
        case WidescreenContractSite::gameplay_effects_end:
            return MidHookOperation{contract, &GameplayEffectsEndMid};
        case WidescreenContractSite::gameplay_hud_projection:
            return MidHookOperation{contract, &GameplayHudProjectionMid};
        case WidescreenContractSite::combo_begin:
            return MidHookOperation{contract, &ComboBeginMid};
        case WidescreenContractSite::combo_end:
            return MidHookOperation{contract, &ComboEndMid};
        case WidescreenContractSite::gameplay_feedback_draw_begin:
            return MidHookOperation{contract, &GameplayFeedbackDrawBeginMid};
        case WidescreenContractSite::gameplay_feedback_draw_end:
            return MidHookOperation{contract, &GameplayFeedbackDrawEndMid};
        case WidescreenContractSite::note_tutorial_group_begin:
            return MidHookOperation{contract, &NoteTutorialGroupBeginMid};
        case WidescreenContractSite::note_tutorial_group_end:
            return MidHookOperation{contract, &NoteTutorialGroupEndMid};
        case WidescreenContractSite::clip_gate:
            return MidHookOperation{contract, &ClipGateMid};
        case WidescreenContractSite::reset_pre:
            return MidHookOperation{contract, &renderer_device_loss::OnWidescreenBeforeReset};
        case WidescreenContractSite::reset_post:
            return MidHookOperation{contract, &renderer_device_loss::OnWidescreenAfterReset};
        default: return ReadOnlyContractOperation{contract};
        }
    }

    std::expected<void, WindowedWidescreenError> PrepareWidescreenRuntime(
        const WindowedWidescreenSettings settings,
        const game_version::ApprovedVersionedPlan& plan,
        const runtime_image::RuntimeImage& image) noexcept {
        if (!settings.enabled()) return {};
        if (g_runtime_owner) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install});
        const auto* build = std::get_if<game_version::GameBuild>(&plan.context().build);
        const auto* variant = std::get_if<game_version::GameImageVariant>(&plan.context().variant);
        const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
        if (!profile) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install,
            .plan_error = game_version::PlanError{.stage = game_version::PlanStage::unsupported_feature,
                .context = plan.context(), .feature = game_version::FeatureId::windowed_widescreen,
                .site = "profile"}});
        const auto abi = BuildWidescreenGameAbi(image, *profile, plan);
        if (!abi) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install, .plan_error = abi.error()});
        const auto resolution = ResolutionModel::Create(settings.output_width(), settings.output_height());
        if (!resolution) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::resolution, .resolution_error = resolution.error()});
        const auto placement = PrepareFixedWindowPlacement(
            resolution->output_size(), abi->layout.fixed_decorated_window_style);
        if (!placement) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::window_policy, .window_policy_error = placement.error()});
        try {
            // Publish stable state before the executor enables the first detour.
            // A later failure is fatal; ownership and resource registration are not undone.
            g_runtime_owner = std::make_unique<WindowedWidescreenRuntime>(
                settings, *resolution, *placement, *abi);
            if (!PrepareRendererParticipant(g_runtime_owner.get()))
                return std::unexpected(WindowedWidescreenError{
                    .stage = WindowedWidescreenOperationStage::resource_attach,
                    .resource_error = g_runtime_owner->last_resource_error});
            return {};
        } catch (...) {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::hook_install});
        }
    }

    void CompleteWidescreenStartup() noexcept {
        if (!g_runtime_owner ||
            !g_originals.config_apply ||
            !g_originals.window_device_create ||
            !g_originals.frame_begin ||
            !g_originals.frame_end ||
            !g_originals.task_dispatch ||
            !g_originals.logical_resolution_set ||
            !g_originals.logical_target_width_set ||
            !g_originals.logical_target_height_set ||
            !g_originals.viewport_reset ||
            !g_originals.mouse_debug_poll ||
            !g_originals.network_status_movie_clip_accept ||
            !g_originals.network_status_shape_draw_visit ||
            !g_originals.screen_width_int ||
            !g_originals.screen_width_float ||
            !g_originals.screen_height_int ||
            !g_originals.screen_height_float ||
            !g_originals.target_width_int ||
            !g_originals.target_width_float ||
            !g_originals.target_height_int ||
            !g_originals.target_height_float)
            PublishRuntimeFatal({.stage = WindowedWidescreenOperationStage::hook_install});
        g_runtime_owner->active.store(true, std::memory_order_release);
        const auto& runtime = *g_runtime_owner;
        const auto rect = runtime.resolution.native_rect();
        PLOG_INFO << "WindowedWidescreen: profile installed"
            << " output=" << runtime.resolution.output_size().width << 'x'
            << runtime.resolution.output_size().height
            << " native_rect=" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
            << " hud_placement=" << GameplayHudPlacementName(runtime.settings.gameplay_hud_placement())
            << " authored_stage_clip=bypassed hooks=36 global_vtable_slots=2";
    }
    [[noreturn]] void AbortWidescreenStartup(const WindowedWidescreenError& error) noexcept {
        if (error.plan_error) gc::diagnostics::AbortProcess(game_version::FormatPlanError(*error.plan_error));
        PublishRuntimeFatal(error);
    }
} // namespace gc::windowed_widescreen
