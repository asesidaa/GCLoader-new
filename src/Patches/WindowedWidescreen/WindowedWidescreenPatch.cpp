#include "Patches/WindowedWidescreen/WindowedWidescreenPatch.h"

#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "SystemPath/StartupFatal.h"

#include <Windows.h>
#include <plog/Log.h>
#include <safetyhook.hpp>

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
        constexpr std::size_t kComboEntryFrameOffset = 0x14;
        constexpr std::size_t kTuneEffectCollectionOffset = 0x1D6C;
        constexpr std::size_t kPointerCollectionBeginOffset = 0x0C;
        constexpr std::size_t kPointerCollectionEndOffset = 0x10;
        constexpr std::uint32_t kJudgementFirstEffectSlot = 93;
        constexpr std::uint32_t kPlayerOneJudgementEffectCount = 5;
        constexpr std::string_view kNetworkStatusNesysClipName = "imc_ico_n";
        constexpr std::string_view kNetworkStatusLocalClipName = "imc_ico_l";
        constexpr std::size_t kNetworkStatusVisitorMatrixStackOffset = 0x1A0;
        constexpr std::size_t kNetworkStatusMatrixElementCount = 16;
        static_assert(sizeof(std::uintptr_t) == sizeof(std::uint32_t));

        [[nodiscard]] consteval std::uint32_t MovieClipNameHash(
            const std::string_view name) noexcept
        {
            std::uint32_t value{};
            for (const auto character : name)
            {
                value = value * 33U +
                    static_cast<std::uint8_t>(character);
            }
            return value;
        }

        constexpr auto kNetworkStatusNesysClipHash =
            MovieClipNameHash(kNetworkStatusNesysClipName);
        constexpr auto kNetworkStatusLocalClipHash =
            MovieClipNameHash(kNetworkStatusLocalClipName);

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

        enum class RuntimePublicationState : std::uint8_t
        {
            preparing,
            enabling,
            active,
        };

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

        struct StoredVtableHook
        {
            std::uintptr_t entry{};
            std::uintptr_t original{};
            std::uintptr_t replacement{};
            bool enabled{};
        };

        struct StoredHook
        {
            WidescreenContractSite site{WidescreenContractSite::none};
            WidescreenHookKind kind{WidescreenHookKind::read_only};
            safetyhook::InlineHook inline_hook{};
            safetyhook::MidHook mid_hook{};
            StoredVtableHook vtable_hook{};
        };

        struct WindowedWidescreenRuntime
        {
            WindowedWidescreenRuntime(
                WindowedWidescreenSettings configured_settings,
                const ResolutionModel configured_resolution,
                PreparedWindowPlacement configured_placement,
                const std::uintptr_t configured_image_base) noexcept
                : settings{configured_settings},
                  resolution{configured_resolution},
                  placement{configured_placement},
                  image_base{configured_image_base},
                  classifier{
                      configured_image_base,
                      PassClassifierDiagnosticSink{
                          .unknown_identity = &ReportUnknownTaskIdentity,
                          .unknown_capacity_exhausted =
                          &ReportUnknownTaskCapacity,
                      }
                  },
                  device{resolution},
                  compositor{
                      resolution.output_size(),
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
            std::uintptr_t image_base{};
            PassClassifier classifier;
            D3D9CompositorDevice device;
            NativeCanvasCompositor compositor;
            std::array<StoredHook, kMaximumWidescreenHooks> hooks{};
            std::size_t hook_count{};
            std::atomic<RuntimePublicationState> publication_state{
                RuntimePublicationState::preparing
            };
            std::optional<NativeWindowPolicyError>
            last_window_policy_error;
            std::optional<renderer_device_loss::RendererResourceError>
            last_resource_error;
            std::optional<CompositorError> last_compositor_error;
            std::optional<renderer_device_loss::RendererResetHookPairError>
            last_reset_hook_error;
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

        struct ProductionInstallContext
        {
            std::unique_ptr<WindowedWidescreenRuntime>* candidate_owner{};
        };

        std::unique_ptr<WindowedWidescreenRuntime> g_runtime_owner;
        std::atomic<WindowedWidescreenRuntime*> g_callback_runtime{};
        std::atomic_bool g_runtime_fatal_published{};
        std::atomic<std::uintptr_t> g_movie_clip_accept_original{};
        std::atomic<std::uintptr_t> g_shape_draw_visit_original{};
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

        [[nodiscard]] bool RuntimeCallbacksAreActive(
            const WindowedWidescreenRuntime& runtime) noexcept
        {
            return runtime.publication_state.load(std::memory_order_acquire) ==
                RuntimePublicationState::active;
        }

        [[nodiscard]] StoredHook* FindHook(
            WindowedWidescreenRuntime& runtime,
            const WidescreenContractSite site) noexcept
        {
            for (std::size_t index = 0; index < runtime.hook_count; ++index)
            {
                if (runtime.hooks[index].site == site)
                {
                    return &runtime.hooks[index];
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::atomic<std::uintptr_t>* VtableOriginalStorage(
            const WidescreenContractSite site) noexcept
        {
            switch (site)
            {
            case WidescreenContractSite::network_status_movie_clip_accept:
                return &g_movie_clip_accept_original;
            case WidescreenContractSite::network_status_shape_draw_visit:
                return &g_shape_draw_visit_original;
            default:
                return nullptr;
            }
        }

        [[nodiscard]] const WidescreenByteContract* FindProductionContract(
            const WidescreenContractSite site) noexcept
        {
            for (const auto& contract : WindowedWidescreenByteContracts())
            {
                if (contract.site == site)
                {
                    return &contract;
                }
            }
            return nullptr;
        }

        [[noreturn]] void PublishRuntimeFatal(
            const WindowedWidescreenError& error) noexcept
        {
            std::string log;
            try
            {
                log = std::format(
                    "WindowedWidescreen: fatal stage={} window_policy_error={} resource_error={} d3d_stage={} d3d_hresult=0x{:08X} hook_stage={} compositor_stage={} compositor_policy_error={} compositor_stable_space={} compositor_requested_space={} compositor_restore_attempted={} compositor_restore_succeeded={}",
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
                    error.install_error.has_value()
                        ? static_cast<unsigned>(error.install_error->stage)
                        : 0U,
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
            gc::system_path::PublishStartupFatal(
                g_runtime_fatal_published,
                log,
                L"The windowed widescreen renderer could not continue safely. "
                L"Check the GCLoader log for the failing stage.",
                L"GCLoader windowed widescreen error",
                0xE7);
            std::abort();
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

        [[nodiscard]] bool EnableVtableHook(
            StoredVtableHook& hook) noexcept
        {
            if (hook.entry == 0 || hook.original == 0 ||
                hook.replacement == 0 || hook.enabled)
            {
                return false;
            }

            auto unprotected = safetyhook::unprotect(
                reinterpret_cast<std::uint8_t*>(hook.entry),
                sizeof(std::uintptr_t));
            if (!unprotected)
            {
                return false;
            }

            auto* const entry = reinterpret_cast<PVOID volatile*>(hook.entry);
            const auto observed = reinterpret_cast<std::uintptr_t>(
                InterlockedCompareExchangePointer(
                    entry,
                    reinterpret_cast<PVOID>(hook.replacement),
                    reinterpret_cast<PVOID>(hook.original)));
            const bool exchanged = observed == hook.original;
            if (exchanged)
            {
                hook.enabled = true;
                (void)FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<void*>(hook.entry),
                    sizeof(std::uintptr_t));
            }
            return exchanged;
        }

        void ResetVtableHook(StoredVtableHook& hook) noexcept
        {
            if (!hook.enabled || hook.entry == 0)
            {
                return;
            }

            auto unprotected = safetyhook::unprotect(
                reinterpret_cast<std::uint8_t*>(hook.entry),
                sizeof(std::uintptr_t));
            if (!unprotected)
            {
                return;
            }

            auto* const entry = reinterpret_cast<PVOID volatile*>(hook.entry);
            const auto observed = reinterpret_cast<std::uintptr_t>(
                InterlockedCompareExchangePointer(
                    entry,
                    reinterpret_cast<PVOID>(hook.original),
                    reinterpret_cast<PVOID>(hook.replacement)));
            if (observed == hook.replacement)
            {
                hook.enabled = false;
                (void)FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<void*>(hook.entry),
                    sizeof(std::uintptr_t));
            }
        }

        [[nodiscard]] bool FlushNativeBatches(void* opaque) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(opaque);
            const auto* contract = FindProductionContract(
                WidescreenContractSite::batch_flush);
            if (runtime == nullptr || contract == nullptr)
            {
                return false;
            }
            __try
            {
                using Flush = void(__cdecl*)();
                reinterpret_cast<Flush>(
                    runtime->image_base + contract->rva)();
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        using NativeBatchCounts =
        std::array<std::uint32_t, kBatchQueueCount>;

        [[nodiscard]] bool ReadNativeBatchCounts(
            WindowedWidescreenRuntime& runtime,
            NativeBatchCounts& pending) noexcept
        {
            std::uintptr_t queue_base{};
            if (!ProductionRead(
                    nullptr,
                    runtime.image_base + kBatchQueuePointerRva,
                    std::as_writable_bytes(std::span{&queue_base, 1})) ||
                queue_base == 0)
            {
                return false;
            }

            for (std::size_t index = 0; index < pending.size(); ++index)
            {
                const auto count_address = queue_base +
                    kBatchPendingCountOffset +
                    kBatchQueueStride * index;
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

        [[nodiscard]] bool ProductionPrepareCandidate(void* opaque) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner)
            {
                return false;
            }
            auto* runtime = context->candidate_owner->get();
            runtime->publication_state.store(
                RuntimePublicationState::enabling,
                std::memory_order_release);
            runtime->device.SetNativeBatchActions(NativeBatchActions{
                .context = runtime,
                .flush = &FlushNativeBatches,
                .empty = &NativeBatchesAreEmpty,
            });
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
                g_callback_runtime.store(nullptr, std::memory_order_release);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ProductionCreateDisabled(
            void* opaque,
            const WidescreenContractSite site,
            const WidescreenHookKind kind,
            const std::uintptr_t address,
            void* callback) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner || callback == nullptr ||
                address == 0)
            {
                return false;
            }
            auto& runtime = **context->candidate_owner;

            if (site == WidescreenContractSite::reset_pre)
            {
                const auto* post_contract = FindProductionContract(
                    WidescreenContractSite::reset_post);
                if (post_contract == nullptr)
                {
                    return false;
                }
                const auto prepared = renderer_device_loss::
                    RendererDeviceLossPrepareResetHooksDisabled(
                        address,
                        runtime.image_base + post_contract->rva,
                        renderer_device_loss::RendererResetFailureActions{
                            .context = &runtime,
                            .failure = +[](
                            void* opaque_runtime,
                            const renderer_device_loss::
                            RendererResetLifecycleStage stage,
                            const renderer_device_loss::
                            RendererResourceError error) noexcept
                            {
                                auto* failed_runtime = static_cast<
                                    WindowedWidescreenRuntime*>(
                                    opaque_runtime);
                                if (failed_runtime == nullptr)
                                {
                                    return;
                                }
                                PublishRuntimeFatal(
                                    WindowedWidescreenError{
                                        .stage = stage ==
                                                 renderer_device_loss::
                                                 RendererResetLifecycleStage::
                                                 before_reset
                                                     ? WindowedWidescreenOperationStage::
                                                     reset_pre
                                                     : WindowedWidescreenOperationStage::
                                                     reset_post,
                                        .resource_error = error,
                                        .d3d_failure = failed_runtime->device.
                                                                       last_failure(),
                                    });
                            },
                        });
                if (!prepared)
                {
                    runtime.last_reset_hook_error = prepared.error();
                    return false;
                }
                return true;
            }
            if (site == WidescreenContractSite::reset_post)
            {
                return renderer_device_loss::
                    RendererDeviceLossResetHookPairState() ==
                    renderer_device_loss::
                    RendererResetHookPairState::disabled;
            }

            if (runtime.hook_count >= runtime.hooks.size())
            {
                return false;
            }

            try
            {
                auto& slot = runtime.hooks[runtime.hook_count];
                slot.site = site;
                slot.kind = kind;
                if (kind == WidescreenHookKind::inline_hook)
                {
                    auto created = safetyhook::InlineHook::create(
                        reinterpret_cast<void*>(address),
                        callback,
                        safetyhook::InlineHook::StartDisabled);
                    if (!created)
                    {
                        return false;
                    }
                    slot.inline_hook = std::move(*created);
                }
                else if (kind == WidescreenHookKind::mid_hook)
                {
                    auto created = safetyhook::MidHook::create(
                        reinterpret_cast<void*>(address),
                        reinterpret_cast<safetyhook::MidHookFn>(callback),
                        safetyhook::MidHook::StartDisabled);
                    if (!created)
                    {
                        return false;
                    }
                    slot.mid_hook = std::move(*created);
                }
                else if (kind == WidescreenHookKind::vtable_hook)
                {
                    auto* const original_storage =
                        VtableOriginalStorage(site);
                    std::uintptr_t original{};
                    if (original_storage == nullptr || !ProductionRead(
                            nullptr,
                            address,
                            std::as_writable_bytes(
                                std::span{&original, 1})) ||
                        original == 0)
                    {
                        return false;
                    }
                    const auto known_original =
                        original_storage->load(
                            std::memory_order_acquire);
                    if (known_original != 0 && known_original != original)
                    {
                        return false;
                    }
                    original_storage->store(
                        original,
                        std::memory_order_release);
                    slot.vtable_hook = StoredVtableHook{
                        .entry = address,
                        .original = original,
                        .replacement = reinterpret_cast<std::uintptr_t>(
                            callback),
                    };
                    PLOG_INFO
                        << "WindowedWidescreen vtable hook prepared"
                        << " site=" << WidescreenContractSiteName(site)
                        << " slot=0x" << std::hex << address
                        << " original=0x" << original
                        << " replacement=0x"
                        << reinterpret_cast<std::uintptr_t>(callback)
                        << std::dec;
                }
                else
                {
                    return false;
                }
                ++runtime.hook_count;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool ProductionEnable(
            void* opaque,
            const WidescreenContractSite site) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner)
            {
                return false;
            }
            if (site == WidescreenContractSite::reset_pre)
            {
                const auto enabled = renderer_device_loss::
                    RendererDeviceLossEnableResetHooks();
                if (!enabled)
                {
                    (*context->candidate_owner)->last_reset_hook_error =
                        enabled.error();
                    return false;
                }
                return true;
            }
            if (site == WidescreenContractSite::reset_post)
            {
                return renderer_device_loss::
                    RendererDeviceLossResetHookPairState() ==
                    renderer_device_loss::
                    RendererResetHookPairState::active;
            }
            auto* hook = FindHook(**context->candidate_owner, site);
            if (hook == nullptr)
            {
                return false;
            }
            try
            {
                if (hook->kind == WidescreenHookKind::inline_hook)
                {
                    return hook->inline_hook.enable().has_value();
                }
                if (hook->kind == WidescreenHookKind::mid_hook)
                {
                    return hook->mid_hook.enable().has_value();
                }
                if (hook->kind != WidescreenHookKind::vtable_hook)
                {
                    return false;
                }
                const bool enabled = EnableVtableHook(hook->vtable_hook);
                std::uintptr_t observed{};
                const bool observed_valid = ProductionRead(
                    nullptr,
                    hook->vtable_hook.entry,
                    std::as_writable_bytes(std::span{&observed, 1}));
                PLOG_INFO
                    << "WindowedWidescreen vtable hook enable"
                    << " site=" << WidescreenContractSiteName(site)
                    << " success=" << enabled
                    << " observed_valid=" << observed_valid
                    << " slot=0x" << std::hex << hook->vtable_hook.entry
                    << " observed=0x" << observed
                    << " replacement=0x" << hook->vtable_hook.replacement
                    << std::dec;
                return enabled;
            }
            catch (...)
            {
                return false;
            }
        }

        void ProductionReset(
            void* opaque,
            const WidescreenContractSite site) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner)
            {
                return;
            }
            if (site == WidescreenContractSite::reset_pre ||
                site == WidescreenContractSite::reset_post)
            {
                renderer_device_loss::RendererDeviceLossResetHooks();
                return;
            }
            if (auto* hook = FindHook(**context->candidate_owner, site))
            {
                try
                {
                    if (hook->kind == WidescreenHookKind::inline_hook)
                    {
                        hook->inline_hook.reset();
                    }
                    else if (hook->kind == WidescreenHookKind::mid_hook)
                    {
                        hook->mid_hook.reset();
                    }
                    else if (hook->kind == WidescreenHookKind::vtable_hook)
                    {
                        ResetVtableHook(hook->vtable_hook);
                    }
                }
                catch (...)
                {
                }
            }
        }

        void ProductionDetachRendererResource(void*) noexcept
        {
            renderer_device_loss::RendererDeviceLossDetachResource();
        }

        void ProductionClearCallbackContext(void*) noexcept
        {
            g_callback_runtime.store(nullptr, std::memory_order_release);
        }

        [[nodiscard]] bool ProductionPublishOwner(void* opaque) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner || g_runtime_owner)
            {
                return false;
            }
            auto* runtime = context->candidate_owner->get();
            runtime->publication_state.store(
                RuntimePublicationState::active,
                std::memory_order_release);
            g_runtime_owner = std::move(*context->candidate_owner);
            g_callback_runtime.store(runtime, std::memory_order_release);
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
                    kWidescreenPreferredImageBase + kMainConfigVtableRva;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigWidth(
            void*,
            const std::uintptr_t config,
            const std::uint32_t value,
            const int trailing) noexcept
        {
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                using Setter = int(__thiscall*)(void*, int, int);
                const auto setter = *reinterpret_cast<Setter const*>(
                    vtable + 0x18);
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
            void*,
            const std::uintptr_t config,
            const std::uint32_t value,
            const int trailing) noexcept
        {
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                using Setter = int(__thiscall*)(void*, int, int);
                const auto setter = *reinterpret_cast<Setter const*>(
                    vtable + 0x1C);
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
            void*,
            const std::uintptr_t config,
            const bool value) noexcept
        {
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                using Setter = void(__thiscall*)(void*, int);
                const auto setter = *reinterpret_cast<Setter const*>(
                    vtable + 0x28);
                setter(reinterpret_cast<void*>(config), value ? 1 : 0);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetConfigMinmax(
            void*,
            const std::uintptr_t config,
            const bool minimize,
            const bool maximize) noexcept
        {
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                using Setter = void(__thiscall*)(void*, int, int);
                const auto setter = *reinterpret_cast<Setter const*>(
                    vtable + 0x2C);
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
            void*,
            const std::uintptr_t config,
            const int first,
            const int second,
            const int third,
            const int fourth) noexcept
        {
            __try
            {
                const auto vtable =
                    *reinterpret_cast<const std::uintptr_t*>(config);
                using Setter = int(__thiscall*)(
                    void*, int, int, int, int);
                const auto setter = *reinterpret_cast<Setter const*>(
                    vtable + 0x30);
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
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, WidescreenContractSite::config_apply);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.ccall<int>(static_cast<int>(config));
        }

        [[nodiscard]] int CallWindowOriginal(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(
                                 *runtime,
                                 WidescreenContractSite::window_device_create);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.thiscall<int>(
                           reinterpret_cast<void*>(renderer));
        }

        [[nodiscard]] int CallLogicalResolutionSetOriginal(
            void* context,
            const std::uint32_t width,
            const std::uint32_t height) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(
                                 *runtime,
                                 WidescreenContractSite::logical_resolution_set);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.ccall<int>(
                           static_cast<int>(width),
                           static_cast<int>(height));
        }

        template <WidescreenContractSite Site>
        [[nodiscard]] int CallLogicalTargetDimensionSetOriginal(
            void* context,
            const std::uint32_t value) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, Site);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.ccall<int>(static_cast<int>(value));
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
                runtime->placement);
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
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, WidescreenContractSite::frame_begin);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.thiscall<int>(
                           reinterpret_cast<void*>(renderer));
        }

        [[nodiscard]] int CallFrameEndOriginal(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, WidescreenContractSite::frame_end);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.thiscall<int>(
                           reinterpret_cast<void*>(renderer));
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

        [[nodiscard]] RenderSpace ClassifyRuntimeTask(
            void* context,
            const std::uintptr_t vtable) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            return runtime == nullptr
                       ? RenderSpace::native_2d
                       : runtime->classifier.ClassifyTask(vtable);
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
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, WidescreenContractSite::task_dispatch);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.thiscall<int>(
                           reinterpret_cast<void*>(task_node));
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
                const auto centered = ResolveGameplayHudViewport(
                    runtime->resolution.output_size(),
                    GameplayHudPlacement::centered);
                if (!centered)
                {
                    return false;
                }
                viewport = NativeViewport{
                    .x = static_cast<float>(centered->x),
                    .y = static_cast<float>(centered->y),
                    .width = static_cast<float>(centered->width),
                    .height = static_cast<float>(centered->height),
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
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(*runtime, WidescreenContractSite::viewport_reset);
            return hook == nullptr
                       ? 0
                       : hook->inline_hook.ccall<int>(
                           reinterpret_cast<int*>(
                               const_cast<NativeViewport*>(viewport)));
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
            void* const movie_clip) noexcept
        {
            if (movie_clip == nullptr)
            {
                return NetworkStatusClip::none;
            }

            const auto address = reinterpret_cast<std::uintptr_t>(movie_clip);
            std::uint32_t name_hash{};
            if (!ProductionRead(
                nullptr,
                address + kMovieClipInstanceNameHashOffset,
                std::as_writable_bytes(std::span{&name_hash, 1})))
            {
                return NetworkStatusClip::none;
            }

            NetworkStatusClip target = NetworkStatusClip::none;
            std::string_view expected_name{};
            if (name_hash == kNetworkStatusNesysClipHash)
            {
                target = NetworkStatusClip::nesys;
                expected_name = kNetworkStatusNesysClipName;
            }
            else if (name_hash == kNetworkStatusLocalClipHash)
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
                    address + kMovieClipInstanceNameOffset,
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
                vtable == runtime.image_base +
                kMovieClipDrawVisitorVtableRva;
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
                sizeof(float) * kNetworkStatusMatrixElementCount;
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
                const auto native = runtime.resolution.native_rect();
                if (visitor == nullptr || output.width <= kNativeWidth ||
                    output.height != kNativeHeight || native.left < 0)
                {
                    return;
                }

                std::uintptr_t matrix_address{};
                if (!ReadRuntimePointer(
                        nullptr,
                        reinterpret_cast<std::uintptr_t>(visitor) +
                        kNetworkStatusVisitorMatrixStackOffset,
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
                corrected[0] *= horizontal_scale;
                corrected[4] *= horizontal_scale;
                corrected[8] *= horizontal_scale;
                corrected[12] = horizontal_scale *
                    (corrected[12] + static_cast<float>(native.left));

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
            std::array<float, kNetworkStatusMatrixElementCount> original_{};
            float* matrix_{};
            bool applied_{};
        };

        void CallShapeDrawVisitOriginal(
            void* const visitor,
            void* const definition) noexcept
        {
            const auto original = g_shape_draw_visit_original.load(
                std::memory_order_acquire);
            if (original == 0 || visitor == nullptr || definition == nullptr)
            {
                return;
            }
            using ShapeDrawVisit = void(__thiscall*)(void*, void*);
            reinterpret_cast<ShapeDrawVisit>(original)(visitor, definition);
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
            std::uintptr_t original =
                g_movie_clip_accept_original.load(std::memory_order_acquire);
            if (runtime != nullptr)
            {
                if (auto* const hook = FindHook(
                        *runtime,
                        WidescreenContractSite::
                        network_status_movie_clip_accept);
                    hook != nullptr && hook->vtable_hook.original != 0)
                {
                    original = hook->vtable_hook.original;
                }
            }
            if (original == 0 || movie_clip == nullptr || visitor == nullptr)
            {
                return 0;
            }
            using AcceptFunction = int(__thiscall*)(void*, void*);
            return reinterpret_cast<AcceptFunction>(original)(
                movie_clip,
                visitor);
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
            const auto clip = IdentifyNetworkStatusClip(movie_clip);
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
                        GameplayHudPlacement::centered))
                {
                    LogNetworkStatusCorrection(
                        *runtime,
                        clip,
                        movie_clip,
                        *current_space,
                        "gameplay-hud-center-begin",
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
                        ? "gameplay-hud-center-restore"
                        : corrected
                        ? clip == NetworkStatusClip::local
                              ? "gameplay-hud-local-matrix"
                              : "gameplay-hud-center"
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
                    GameplayHudPlacement::centered);
            if (!begun)
            {
                LogNetworkStatusCorrection(
                    *runtime,
                    clip,
                    movie_clip,
                    *current_space,
                    "physical-center-overlay-begin",
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
                    ? "physical-center-overlay-end"
                    : corrected
                    ? clip == NetworkStatusClip::local
                          ? "physical-local-matrix"
                          : "physical-center-overlay"
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
            const auto result = RunTaskDispatchHook(
                reinterpret_cast<std::uintptr_t>(task_node),
                TaskDispatchHookActions{
                    .context = runtime,
                    .read_pointer = &ReadRuntimePointer,
                    .classify_task = &ClassifyRuntimeTask,
                    .request_space = &RequestRuntimeSpace,
                    .call_original = &CallTaskDispatchOriginal,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
        }

        void RequestGameplayPass(const GameplayPass pass) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr ||
                !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            const auto result = RunGameplaySpaceHook(
                pass,
                RenderSpaceHookActions{
                    .context = runtime,
                    .request_space = &RequestRuntimeSpace,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
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
            const std::uintptr_t tune,
            const std::uintptr_t effect,
            const std::array<std::uint32_t, SlotCount>& slots,
            bool& matches) noexcept
        {
            matches = false;
            constexpr auto collection_extent =
                kTuneEffectCollectionOffset + kPointerCollectionEndOffset;
            if (tune == 0 || effect == 0 ||
                tune > std::numeric_limits<std::uintptr_t>::max() -
                collection_extent)
            {
                return false;
            }

            const auto collection = tune + kTuneEffectCollectionOffset;
            std::uintptr_t begin{};
            std::uintptr_t end{};
            if (!ReadRuntimePointer(
                    nullptr,
                    collection + kPointerCollectionBeginOffset,
                    begin) ||
                !ReadRuntimePointer(
                    nullptr,
                    collection + kPointerCollectionEndOffset,
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
            const std::uintptr_t tune,
            const std::uintptr_t effect,
            bool& matches) noexcept
        {
            std::array<std::uint32_t, kPlayerOneJudgementEffectCount> slots{};
            for (std::size_t index = 0; index < slots.size(); ++index)
            {
                slots[index] =
                    kJudgementFirstEffectSlot +
                    static_cast<std::uint32_t>(index);
            }
            return TryMatchEffectSlots(tune, effect, slots, matches);
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
                    GameplayHudPlacement::centered))
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
                runtime->active_gameplay_tune,
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
                GameplayHudPlacement::centered))
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
            if (runtime == nullptr ||
                !RuntimeCallbacksAreActive(*runtime))
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
            const std::uint32_t frame_pointer,
            std::int32_t& entry) noexcept
        {
            if (frame_pointer < kComboEntryFrameOffset)
            {
                return false;
            }
            __try
            {
                entry = *reinterpret_cast<const std::int32_t*>(
                    static_cast<std::uintptr_t>(frame_pointer) -
                    kComboEntryFrameOffset);
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
            if (!TryReadComboEntry(context.ebp, entry) ||
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
                GameplayHudPlacement::centered))
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage = WindowedWidescreenOperationStage::
                        gameplay_hud_placement,
                    });
            }
        }

        void RendererOwnedResetHookSentinel(safetyhook::Context&) noexcept
        {
        }

        template <typename Value>
        [[nodiscard]] Value CallDimensionOriginal(
            WindowedWidescreenRuntime& runtime,
            const WidescreenContractSite site) noexcept
        {
            auto* hook = FindHook(runtime, site);
            return hook == nullptr ? Value{} : hook->inline_hook.ccall<Value>();
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
            const auto result = RunRenderDimensionInt(
                axis,
                RenderDimensionHookActions{
                    .context = runtime,
                    .current_dimensions = &ReadCurrentDimensions,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
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
            const auto result = RunRenderDimensionFloat(
                axis,
                RenderDimensionHookActions{
                    .context = runtime,
                    .current_dimensions = &ReadCurrentDimensions,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
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
            const auto result = RunViewportResetHook(
                typed,
                ViewportResetHookActions{
                    .context = runtime,
                    .current_viewport = &ReadCurrentViewport,
                    .call_original = &CallViewportOriginal,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
        }

        void ClipGateMid(safetyhook::Context& context) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr ||
                !RuntimeCallbacksAreActive(*runtime))
            {
                return;
            }
            const auto* continuation = FindProductionContract(
                WidescreenContractSite::clip_continuation);
            if (continuation == nullptr)
            {
                PublishRenderRuntimeFatal(
                    *runtime,
                    WindowedWidescreenError{
                        .stage =
                        WindowedWidescreenOperationStage::clip_bypass,
                    });
            }
            auto instruction_pointer = context.eip;
            const auto result = ApplyClipGateHook(
                runtime->image_base,
                continuation->rva,
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
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            auto* hook = runtime == nullptr
                             ? nullptr
                             : FindHook(
                                 *runtime,
                                 WidescreenContractSite::mouse_debug_poll);
            return hook == nullptr
                       ? 0
                       : reinterpret_cast<std::uintptr_t>(
                           hook->inline_hook.thiscall<POINT*>(
                               reinterpret_cast<void*>(owner),
                               output));
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
            const auto result = RunMouseDebugPollHook(
                reinterpret_cast<std::uintptr_t>(owner),
                output,
                runtime->resolution,
                MousePollHookActions{
                    .context = runtime,
                    .call_original = &CallMousePollOriginal,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return reinterpret_cast<POINT*>(*result);
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
            const auto result = RunLogicalResolutionSetHook(
                width,
                height,
                LogicalResolutionSetHookActions{
                    .context = runtime,
                    .call_original = &CallLogicalResolutionSetOriginal,
                    .set_target_width =
                    &CallLogicalTargetDimensionSetOriginal<
                        WidescreenContractSite::logical_target_width_set>,
                    .set_target_height =
                    &CallLogicalTargetDimensionSetOriginal<
                        WidescreenContractSite::logical_target_height_set>,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
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
            const auto result = RunLogicalTargetDimensionSetHook(
                Axis,
                value,
                LogicalTargetDimensionSetHookActions{
                    .context = runtime,
                    .call_original =
                    &CallLogicalTargetDimensionSetOriginal<Site>,
                });
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
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

            const auto result = RunConfigApplyHook(
                static_cast<std::uintptr_t>(config),
                runtime->resolution.output_size(),
                ConfigApplyHookActions{
                    .context = runtime,
                    .call_original = &CallConfigOriginal,
                    .config_vtable_matches = &ConfigVtableMatches,
                    .set_width = &SetConfigWidth,
                    .set_height = &SetConfigHeight,
                    .set_resize = &SetConfigResize,
                    .set_minmax = &SetConfigMinmax,
                    .set_mode = &SetConfigMode,
                });
            if (!result)
            {
                PublishRuntimeFatal(result.error());
            }
            return *result;
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
            const auto result = RunWindowDeviceHook(
                reinterpret_cast<std::uintptr_t>(renderer),
                WindowDeviceHookActions{
                    .context = runtime,
                    .call_original = &CallWindowOriginal,
                    .validate_and_place = &ValidateAndPlaceWindow,
                    .activate_resources = &ActivateRendererResources,
                });
            if (!result)
            {
                auto error = result.error();
                error.window_policy_error =
                    runtime->last_window_policy_error;
                error.resource_error = runtime->last_resource_error;
                error.d3d_failure = runtime->device.last_failure();
                PublishRuntimeFatal(error);
            }
            return *result;
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
            const auto result = RunFrameBoundaryHook(
                reinterpret_cast<std::uintptr_t>(renderer),
                FrameBoundaryHookActions{
                    .context = runtime,
                    .run_compositor = &BeginCompositorFrame,
                    .call_original = &CallFrameBeginOriginal,
                },
                WindowedWidescreenOperationStage::frame_begin);
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            if (FAILED(static_cast<HRESULT>(*result)))
            {
                // Native BeginScene failed, so the render loop may skip the
                // matching frame-end callback and proceed into device reset.
                ResetScopedRenderState(*runtime);
                runtime->compositor.ResetForDeviceLoss();
            }
            return *result;
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
            const auto result = RunFrameBoundaryHook(
                reinterpret_cast<std::uintptr_t>(renderer),
                FrameBoundaryHookActions{
                    .context = runtime,
                    .run_compositor = &EndCompositorFrame,
                    .call_original = &CallFrameEndOriginal,
                },
                WindowedWidescreenOperationStage::frame_end);
            if (!result)
            {
                PublishRenderRuntimeFatal(*runtime, result.error());
            }
            return *result;
        }
    } // namespace

    std::expected<int, WindowedWidescreenError> RunConfigApplyHook(
        const std::uintptr_t main_config_ptr,
        const OutputSize output_size,
        const ConfigApplyHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.call_original == nullptr ||
            actions.config_vtable_matches == nullptr ||
            actions.set_width == nullptr || actions.set_height == nullptr ||
            actions.set_resize == nullptr || actions.set_minmax == nullptr ||
            actions.set_mode == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }

        const auto native_result = actions.call_original(
            actions.context,
            main_config_ptr);
        if (!actions.config_vtable_matches(
                actions.context,
                main_config_ptr) ||
            !actions.set_width(
                actions.context,
                main_config_ptr,
                output_size.width,
                0) ||
            !actions.set_height(
                actions.context,
                main_config_ptr,
                output_size.height,
                0) ||
            !actions.set_resize(
                actions.context,
                main_config_ptr,
                false) ||
            !actions.set_minmax(
                actions.context,
                main_config_ptr,
                true,
                false) ||
            !actions.set_mode(
                actions.context,
                main_config_ptr,
                1,
                1,
                1,
                1))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::config_override,
            });
        }
        return native_result;
    }

    std::expected<int, WindowedWidescreenError> RunWindowDeviceHook(
        const std::uintptr_t renderer_owner,
        const WindowDeviceHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.call_original == nullptr ||
            actions.validate_and_place == nullptr ||
            actions.activate_resources == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }

        const auto native_result = actions.call_original(
            actions.context,
            renderer_owner);
        if (native_result == 0)
        {
            return native_result;
        }
        if (!actions.validate_and_place(
            actions.context,
            renderer_owner))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::window_policy,
            });
        }
        if (!actions.activate_resources(
            actions.context,
            renderer_owner))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::resource_attach,
            });
        }
        return native_result;
    }

    std::expected<int, WindowedWidescreenError>
    RunLogicalResolutionSetHook(
        const std::uint32_t requested_width,
        const std::uint32_t requested_height,
        const LogicalResolutionSetHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.call_original == nullptr ||
            actions.set_target_width == nullptr ||
            actions.set_target_height == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        const auto native_result = actions.call_original(
            actions.context,
            kNativeWidth,
            kNativeHeight);
        actions.set_target_width(actions.context, requested_width);
        actions.set_target_height(actions.context, requested_height);
        return native_result;
    }

    std::expected<int, WindowedWidescreenError>
    RunLogicalTargetDimensionSetHook(
        const RenderDimensionAxis axis,
        const std::uint32_t requested_value,
        const LogicalTargetDimensionSetHookActions& actions) noexcept
    {
        (void)axis;
        if (actions.context == nullptr || actions.call_original == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        return actions.call_original(actions.context, requested_value);
    }

    std::expected<int, WindowedWidescreenError> RunFrameBoundaryHook(
        const std::uintptr_t renderer_owner,
        const FrameBoundaryHookActions& actions,
        const WindowedWidescreenOperationStage stage) noexcept
    {
        if ((stage != WindowedWidescreenOperationStage::frame_begin &&
                stage != WindowedWidescreenOperationStage::frame_end) ||
            actions.context == nullptr ||
            actions.run_compositor == nullptr ||
            actions.call_original == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        if (!actions.run_compositor(actions.context))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = stage,
            });
        }
        return actions.call_original(actions.context, renderer_owner);
    }

    std::expected<int, WindowedWidescreenError> RunTaskDispatchHook(
        const std::uintptr_t task_node,
        const TaskDispatchHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.read_pointer == nullptr ||
            actions.classify_task == nullptr ||
            actions.request_space == nullptr ||
            actions.call_original == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }

        std::uintptr_t task{};
        std::uintptr_t vtable{};
        if (!actions.read_pointer(actions.context, task_node, task) ||
            !actions.read_pointer(actions.context, task, vtable))
        {
            vtable = 0;
        }
        const auto requested = actions.classify_task(
            actions.context,
            vtable);
        if (!actions.request_space(actions.context, requested))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::task_dispatch,
            });
        }
        return actions.call_original(actions.context, task_node);
    }

    std::expected<void, WindowedWidescreenError> RunGameplaySpaceHook(
        const GameplayPass pass,
        const RenderSpaceHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.request_space == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        if (!actions.request_space(
            actions.context,
            PassClassifier::ClassifyGameplay(pass)))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::render_transition,
            });
        }
        return {};
    }

    RenderQueryRoute ResolveRenderQueryRoute(
        const bool compositor_frame_active) noexcept
    {
        return compositor_frame_active
                   ? RenderQueryRoute::frame_virtualized
                   : RenderQueryRoute::native_passthrough;
    }

    std::expected<std::uint32_t, WindowedWidescreenError>
    RunRenderDimensionInt(
        const RenderDimensionAxis axis,
        const RenderDimensionHookActions& actions) noexcept
    {
        if (actions.context == nullptr ||
            actions.current_dimensions == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        RenderDimensions dimensions{};
        if (!actions.current_dimensions(actions.context, dimensions))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::dimension_query,
            });
        }
        return axis == RenderDimensionAxis::width
                   ? dimensions.width
                   : dimensions.height;
    }

    std::expected<float, WindowedWidescreenError>
    RunRenderDimensionFloat(
        const RenderDimensionAxis axis,
        const RenderDimensionHookActions& actions) noexcept
    {
        if (actions.context == nullptr ||
            actions.current_dimensions == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        RenderDimensions dimensions{};
        if (!actions.current_dimensions(actions.context, dimensions))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::dimension_query,
            });
        }
        return axis == RenderDimensionAxis::width
                   ? dimensions.width_float
                   : dimensions.height_float;
    }

    std::expected<int, WindowedWidescreenError> RunViewportResetHook(
        const NativeViewport* const viewport,
        const ViewportResetHookActions& actions) noexcept
    {
        if (actions.context == nullptr ||
            actions.current_viewport == nullptr ||
            actions.call_original == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        NativeViewport current_viewport{};
        if (!actions.current_viewport(actions.context, current_viewport))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::viewport,
            });
        }
        if (viewport == nullptr)
        {
            return actions.call_original(
                actions.context,
                &current_viewport);
        }

        const NativeViewport translated_viewport{
            .x = current_viewport.x + viewport->x,
            .y = current_viewport.y + viewport->y,
            .width = viewport->width,
            .height = viewport->height,
        };
        return actions.call_original(
            actions.context,
            &translated_viewport);
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
        const std::uintptr_t image_base,
        const std::uint32_t live_continuation_rva,
        std::uint32_t& instruction_pointer) noexcept
    {
        if (image_base >
            std::numeric_limits<std::uint32_t>::max() -
            live_continuation_rva)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::clip_bypass,
            });
        }
        instruction_pointer = static_cast<std::uint32_t>(
            image_base + live_continuation_rva);
        return {};
    }

    std::expected<std::uintptr_t, WindowedWidescreenError>
    RunMouseDebugPollHook(
        const std::uintptr_t owner,
        std::uint32_t* const output,
        const ResolutionModel& resolution,
        const MousePollHookActions& actions) noexcept
    {
        if (actions.context == nullptr || actions.call_original == nullptr ||
            output == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }

        const auto native_result = actions.call_original(
            actions.context,
            owner,
            output);
        if (output[kMouseValidWord] != 1)
        {
            return native_result;
        }

        const auto mapped = resolution.ClientToNative(
            static_cast<std::int32_t>(output[kMouseXWord]),
            static_cast<std::int32_t>(output[kMouseYWord]));
        if (!mapped)
        {
            output[kMouseValidWord] = 0;
            return native_result;
        }
        output[kMouseXWord] = static_cast<std::uint32_t>(mapped->x);
        output[kMouseYWord] = static_cast<std::uint32_t>(mapped->y);
        return native_result;
    }

    std::expected<void, WindowedWidescreenError>
    RunWindowedWidescreenInitializationGate(
        const bool enabled,
        const WindowedWidescreenInitializationGateActions& actions) noexcept
    {
        if (!enabled)
        {
            return {};
        }
        if (actions.context == nullptr ||
            actions.initialize_enabled == nullptr)
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::invalid_actions,
            });
        }
        if (!actions.initialize_enabled(actions.context))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::hook_install,
            });
        }
        return {};
    }

    std::expected<void, WindowedWidescreenError>
    WindowedWidescreenPatchInit(
        const WindowedWidescreenSettings settings) noexcept
    {
        static std::atomic<std::uint8_t> state{};
        static bool stored_success{};
        static WindowedWidescreenError stored_error{};

        std::uint8_t expected{};
        if (!state.compare_exchange_strong(
            expected,
            1,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
        {
            while (state.load(std::memory_order_acquire) == 1)
            {
                YieldProcessor();
            }
            if (stored_success)
            {
                return {};
            }
            return std::unexpected(stored_error);
        }

        const auto finish = [](
            const std::optional<
                WindowedWidescreenError> error)
            -> std::expected<void, WindowedWidescreenError>
        {
            stored_success = !error.has_value();
            if (error)
            {
                stored_error = *error;
            }
            state.store(2, std::memory_order_release);
            return error
                       ? std::expected<void, WindowedWidescreenError>{
                           std::unexpected(*error)
                       }
                       : std::expected<void, WindowedWidescreenError>{};
        };

        bool enabled_initialization{};
        const auto gate = RunWindowedWidescreenInitializationGate(
            settings.enabled(),
            WindowedWidescreenInitializationGateActions{
                .context = &enabled_initialization,
                .initialize_enabled = +[](void* context) noexcept
                {
                    *static_cast<bool*>(context) = true;
                    return true;
                },
            });
        if (!gate)
        {
            return finish(gate.error());
        }
        if (!enabled_initialization)
        {
            return finish(std::nullopt);
        }

        const auto resolution = ResolutionModel::Create(
            settings.output_width(),
            settings.output_height());
        if (!resolution)
        {
            return finish(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::resolution,
                .resolution_error = resolution.error(),
            });
        }

        const auto placement = PrepareFixedWindowPlacement(
            resolution->output_size());
        if (!placement)
        {
            return finish(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::window_policy,
                .window_policy_error = placement.error(),
            });
        }

        const auto module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            return finish(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::hook_install,
            });
        }
        const auto image_base = reinterpret_cast<std::uintptr_t>(module);

        try
        {
            auto candidate = std::make_unique<WindowedWidescreenRuntime>(
                settings,
                *resolution,
                *placement,
                image_base);
            ProductionInstallContext install_context{
                .candidate_owner = &candidate,
            };
            const std::array requests{
                WidescreenHookRequest{
                    WidescreenContractSite::config_apply,
                    reinterpret_cast<void*>(&ConfigApplyDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::window_device_create,
                    reinterpret_cast<void*>(&WindowDeviceDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::logical_resolution_set,
                    reinterpret_cast<void*>(&LogicalResolutionSetDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::logical_target_width_set,
                    reinterpret_cast<void*>(
                        &LogicalTargetDimensionSetDetour<
                            RenderDimensionAxis::width,
                            WidescreenContractSite::logical_target_width_set>)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::logical_target_height_set,
                    reinterpret_cast<void*>(
                        &LogicalTargetDimensionSetDetour<
                            RenderDimensionAxis::height,
                            WidescreenContractSite::logical_target_height_set>)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::frame_begin,
                    reinterpret_cast<void*>(&FrameBeginDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::frame_end,
                    reinterpret_cast<void*>(&FrameEndDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::task_dispatch,
                    reinterpret_cast<void*>(&TaskDispatchDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::
                    network_status_movie_clip_accept,
                    reinterpret_cast<void*>(
                        &NetworkStatusMovieClipAcceptDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::
                    network_status_shape_draw_visit,
                    reinterpret_cast<void*>(
                        &NetworkStatusShapeDrawVisitDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::test_mode_native_begin,
                    reinterpret_cast<void*>(&TestModeNativeBeginMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::test_mode_native_end,
                    reinterpret_cast<void*>(&TestModeNativeEndMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::screen_width_int,
                    reinterpret_cast<void*>(&ScreenWidthIntDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::screen_height_int,
                    reinterpret_cast<void*>(&ScreenHeightIntDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::screen_width_float,
                    reinterpret_cast<void*>(&ScreenWidthFloatDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::screen_height_float,
                    reinterpret_cast<void*>(&ScreenHeightFloatDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::target_width_int,
                    reinterpret_cast<void*>(&TargetWidthIntDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::target_height_int,
                    reinterpret_cast<void*>(&TargetHeightIntDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::target_width_float,
                    reinterpret_cast<void*>(&TargetWidthFloatDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::target_height_float,
                    reinterpret_cast<void*>(&TargetHeightFloatDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::viewport_reset,
                    reinterpret_cast<void*>(&ViewportResetDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::mouse_debug_poll,
                    reinterpret_cast<void*>(&MouseDebugPollDetour)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_stage_background,
                    reinterpret_cast<void*>(&GameplayStageBackgroundMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_track,
                    reinterpret_cast<void*>(&GameplayTrackMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_effects,
                    reinterpret_cast<void*>(&GameplayEffectsMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_effects_end,
                    reinterpret_cast<void*>(&GameplayEffectsEndMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_hud_projection,
                    reinterpret_cast<void*>(&GameplayHudProjectionMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::combo_begin,
                    reinterpret_cast<void*>(&ComboBeginMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::combo_end,
                    reinterpret_cast<void*>(&ComboEndMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_feedback_draw_begin,
                    reinterpret_cast<void*>(&GameplayFeedbackDrawBeginMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::gameplay_feedback_draw_end,
                    reinterpret_cast<void*>(&GameplayFeedbackDrawEndMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::note_tutorial_group_begin,
                    reinterpret_cast<void*>(&NoteTutorialGroupBeginMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::note_tutorial_group_end,
                    reinterpret_cast<void*>(&NoteTutorialGroupEndMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::clip_gate,
                    reinterpret_cast<void*>(&ClipGateMid)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::reset_pre,
                    reinterpret_cast<void*>(&RendererOwnedResetHookSentinel)
                },
                WidescreenHookRequest{
                    WidescreenContractSite::reset_post,
                    reinterpret_cast<void*>(&RendererOwnedResetHookSentinel)
                },
            };
            const auto installed = InstallWindowedWidescreenHooks(
                image_base,
                WidescreenContractManifest{
                    .byte_contracts = WindowedWidescreenByteContracts(),
                    .pointer_contracts = WindowedWidescreenPointerContracts(),
                },
                requests,
                WidescreenInstallActions{
                    .context = &install_context,
                    .read = &ProductionRead,
                    .prepare_candidate = &ProductionPrepareCandidate,
                    .create_disabled = &ProductionCreateDisabled,
                    .enable = &ProductionEnable,
                    .reset = &ProductionReset,
                    .detach_renderer_resource =
                    &ProductionDetachRendererResource,
                    .clear_callback_context =
                    &ProductionClearCallbackContext,
                    .publish_owner = &ProductionPublishOwner,
                });
            if (!installed)
            {
                WindowedWidescreenError error{
                    .stage = installed.error().stage ==
                             WidescreenInstallStage::candidate_prepare
                                 ? WindowedWidescreenOperationStage::resource_attach
                                 : WindowedWidescreenOperationStage::hook_install,
                    .install_error = installed.error(),
                };
                if (candidate)
                {
                    error.resource_error = candidate->last_resource_error;
                    error.reset_hook_error =
                        candidate->last_reset_hook_error;
                }
                return finish(error);
            }
            try
            {
                const auto native = resolution->native_rect();
                PLOG_INFO
                    << "WindowedWidescreen: transaction committed"
                    << " output=" << resolution->output_size().width << 'x'
                    << resolution->output_size().height
                    << " native_rect=" << native.left << ',' << native.top
                    << ',' << native.right << ',' << native.bottom
                    << " authored_stage_clip=bypassed"
                    << " hook_count=" << requests.size();
            }
            catch (...)
            {
            }
            return finish(std::nullopt);
        }
        catch (...)
        {
            renderer_device_loss::RendererDeviceLossDetachResource();
            g_callback_runtime.store(nullptr, std::memory_order_release);
            return finish(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::hook_install,
            });
        }
    }
} // namespace gc::windowed_widescreen
