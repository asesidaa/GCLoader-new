#include "Patches/WindowedWidescreen/WindowedWidescreenPatch.h"

#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "SystemPath/StartupFatal.h"

#include <Windows.h>
#include <safetyhook.hpp>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <span>

namespace gc::windowed_widescreen
{
    namespace
    {
        enum class RuntimePublicationState : std::uint8_t
        {
            preparing,
            enabling,
            active,
        };

        struct StoredHook
        {
            WidescreenContractSite site{WidescreenContractSite::none};
            WidescreenHookKind kind{WidescreenHookKind::read_only};
            safetyhook::InlineHook inline_hook{};
            safetyhook::MidHook mid_hook{};
        };

        struct WindowedWidescreenRuntime
        {
            WindowedWidescreenRuntime(
                WindowedWidescreenSettings configured_settings,
                const ResolutionModel configured_resolution,
                PreparedWindowPlacement configured_placement) noexcept
                : settings{configured_settings},
                  resolution{configured_resolution},
                  placement{configured_placement},
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
                      device.DeviceActions()}
            {
            }

            WindowedWidescreenSettings settings;
            ResolutionModel resolution;
            PreparedWindowPlacement placement;
            D3D9CompositorDevice device;
            NativeCanvasCompositor compositor;
            std::array<StoredHook, kMaximumWidescreenHooks> hooks{};
            std::size_t hook_count{};
            RuntimePublicationState publication_state{
                RuntimePublicationState::preparing};
            std::optional<renderer_device_loss::RendererResourceError>
                last_resource_error;
        };

        struct ProductionInstallContext
        {
            std::unique_ptr<WindowedWidescreenRuntime>* candidate_owner{};
        };

        std::unique_ptr<WindowedWidescreenRuntime> g_runtime_owner;
        std::atomic<WindowedWidescreenRuntime*> g_callback_runtime{};
        std::atomic_bool g_runtime_fatal_published{};

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
                    "WindowedWidescreen: fatal stage={} d3d_stage={} hook_stage={}",
                    static_cast<unsigned>(error.stage),
                    static_cast<unsigned>(error.d3d_failure.stage),
                    error.install_error.has_value()
                        ? static_cast<unsigned>(error.install_error->stage)
                        : 0U);
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

        [[nodiscard]] bool ProductionPrepareCandidate(void* opaque) noexcept
        {
            auto* context = static_cast<ProductionInstallContext*>(opaque);
            if (context == nullptr || context->candidate_owner == nullptr ||
                !*context->candidate_owner)
            {
                return false;
            }
            auto* runtime = context->candidate_owner->get();
            runtime->publication_state = RuntimePublicationState::enabling;
            g_callback_runtime.store(runtime, std::memory_order_release);
            const auto attached =
                renderer_device_loss::RendererDeviceLossAttachResource(
                    runtime->device.ResourceParticipant());
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
            auto* hook = FindHook(**context->candidate_owner, site);
            if (hook == nullptr)
            {
                return false;
            }
            try
            {
                return hook->kind == WidescreenHookKind::inline_hook
                    ? hook->inline_hook.enable().has_value()
                    : hook->mid_hook.enable().has_value();
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
            runtime->publication_state = RuntimePublicationState::active;
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

        [[nodiscard]] bool ValidateAndPlaceWindow(
            void* context,
            const std::uintptr_t renderer) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            return runtime != nullptr &&
                ValidateAndPlaceRendererWindow(
                    renderer,
                    runtime->placement).has_value();
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
            return runtime != nullptr && runtime->device.active() &&
                runtime->compositor.BeginFrame().has_value();
        }

        [[nodiscard]] bool EndCompositorFrame(void* context) noexcept
        {
            auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
            return runtime != nullptr && runtime->device.active() &&
                runtime->compositor.EndFrame().has_value();
        }

        int __cdecl ConfigApplyDetour(const int config) noexcept
        {
            auto* runtime =
                g_callback_runtime.load(std::memory_order_acquire);
            if (runtime == nullptr)
            {
                return 0;
            }
            if (runtime->publication_state != RuntimePublicationState::active)
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
            if (runtime->publication_state != RuntimePublicationState::active)
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
            if (runtime->publication_state != RuntimePublicationState::active)
            {
                return CallFrameBeginOriginal(
                    runtime,
                    reinterpret_cast<std::uintptr_t>(renderer));
            }
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
                PublishRuntimeFatal(result.error());
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
            if (runtime->publication_state != RuntimePublicationState::active)
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
                PublishRuntimeFatal(result.error());
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
        if (native_result == 0)
        {
            return native_result;
        }
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
                renderer_owner) ||
            !actions.activate_resources(
                actions.context,
                renderer_owner))
        {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::window_device,
            });
        }
        return native_result;
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
                      std::unexpected(*error)}
                : std::expected<void, WindowedWidescreenError>{};
        };

        if (!settings.enabled())
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
                *placement);
            ProductionInstallContext install_context{
                .candidate_owner = &candidate,
            };
            const std::array requests{
                WidescreenHookRequest{
                    WidescreenContractSite::config_apply,
                    reinterpret_cast<void*>(&ConfigApplyDetour)},
                WidescreenHookRequest{
                    WidescreenContractSite::window_device_create,
                    reinterpret_cast<void*>(&WindowDeviceDetour)},
                WidescreenHookRequest{
                    WidescreenContractSite::frame_begin,
                    reinterpret_cast<void*>(&FrameBeginDetour)},
                WidescreenHookRequest{
                    WidescreenContractSite::frame_end,
                    reinterpret_cast<void*>(&FrameEndDetour)},
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
                }
                return finish(error);
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
