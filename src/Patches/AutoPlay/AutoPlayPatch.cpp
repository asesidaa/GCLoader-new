#include "AutoPlayPatch.h"

#include "AutoPlayMarker.h"
#include "AutoPlayPatchDiagnostics.h"
#include "AutoPlayPatchTransaction.h"
#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <safetyhook.hpp>

#include "plog/Log.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <utility>

namespace gc::auto_play
{
    namespace
    {
        struct ProductionAutoPlayRuntime
        {
            AutoPlayRuntimeState state;
            safetyhook::MidHook marker_hook;
        };

        ProductionAutoPlayRuntime g_runtime;
        std::atomic_bool g_already_enabled_logged{};

        bool QueueNativeText(
            void* context,
            const float x,
            const float y,
            const std::uint32_t argb,
            const char* text) noexcept
        {
            const auto address = reinterpret_cast<std::uintptr_t>(context);
            const auto function =
                reinterpret_cast<NativeDebugTextFunction>(address);
            return CallNativeDebugTextGuarded(function, x, y, argb, text);
        }

        // SafetyHook's mid-hook ABI requires a mutable context reference.
        void AutoPlayMarkerMidHook(safetyhook::Context&) noexcept
        {
            try
            {
                if (!g_runtime.state.marker_active.load(
                        std::memory_order_acquire))
                {
                    return;
                }

                const auto native_text_address =
                    g_runtime.state.native_text_address;
                const AutoPlayMarkerTextActions actions{
                    .context = reinterpret_cast<void*>(native_text_address),
                    .queue = QueueNativeText,
                };
                if (ProduceAutoPlayMarkerFrame(true, actions) !=
                    AutoPlayMarkerFrameResult::queued)
                {
                    PublishAutoPlayMarkerRuntimeFatal();
                }
            }
            catch (...)
            {
                PublishAutoPlayMarkerRuntimeFatal();
            }
        }

        std::expected<std::uintptr_t, DWORD> ResolveImageBase(
            void*) noexcept
        {
            const auto module = GetModuleHandleW(nullptr);
            if (module == nullptr)
            {
                const auto error = GetLastError();
                return std::unexpected(
                    error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : error);
            }
            return reinterpret_cast<std::uintptr_t>(module);
        }

        game_compatibility::GameBinaryMemoryResult ReadGameImage(
            void*,
            const std::uintptr_t address,
            const std::span<std::byte> output) noexcept
        {
            const auto actions =
                game_compatibility::ProductionGameBinaryPatchActions();
            return actions.read(actions.context, address, output);
        }

        game_compatibility::GameBinaryMemoryResult WriteGameImage(
            void*,
            const std::uintptr_t address,
            const std::span<const std::byte> input) noexcept
        {
            const auto actions =
                game_compatibility::ProductionGameBinaryPatchActions();
            return actions.write(actions.context, address, input);
        }

        std::expected<void, std::uint32_t> InstallMarkerHook(
            void*,
            const std::uintptr_t address) noexcept
        {
            try
            {
                auto created = safetyhook::MidHook::create(
                    reinterpret_cast<void*>(address),
                    AutoPlayMarkerMidHook);
                if (!created)
                {
                    return std::unexpected(
                        static_cast<std::uint32_t>(created.error().type));
                }
                g_runtime.marker_hook = std::move(*created);
                return {};
            }
            catch (...)
            {
                return std::unexpected(
                    (std::numeric_limits<std::uint32_t>::max)());
            }
        }

        bool ResetMarkerHook(void*) noexcept
        {
            try
            {
                g_runtime.marker_hook.reset();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] AutoPlayPatchActions ProductionActions() noexcept
        {
            return {
                .context = &g_runtime,
                .resolve_image_base = ResolveImageBase,
                .read = ReadGameImage,
                .write = WriteGameImage,
                .install_marker_hook = InstallMarkerHook,
                .reset_marker_hook = ResetMarkerHook,
            };
        }
    } // namespace

    bool AutoPlayPatchInit(const bool enabled) noexcept
    {
        try
        {
            const auto result = InstallAutoPlayPatch(
                enabled,
                g_runtime.state,
                ProductionActions());
            if (!result)
            {
                PublishAutoPlaySetupFatal(result.error());
                return false;
            }

            switch (result->state)
            {
            case AutoPlayPatchState::disabled:
                PLOG_INFO << "AutoPlayPatch: state=disabled";
                return true;
            case AutoPlayPatchState::enabled:
                PLOG_WARNING
                    << "AutoPlayPatch: state=enabled direct_patched="
                    << result->direct_patched
                    << " direct_existing=" << result->direct_existing
                    << " marker=active score_save=disabled";
                return true;
            case AutoPlayPatchState::already_enabled:
                if (!g_already_enabled_logged.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    PLOG_INFO
                        << "AutoPlayPatch: state=already_enabled "
                        << "direct_patched=" << result->direct_patched
                        << " direct_existing=" << result->direct_existing
                        << " marker=active score_save=disabled";
                }
                return true;
            }
        }
        catch (...)
        {
            PublishAutoPlaySetupFallbackFatal();
            return false;
        }

        PublishAutoPlaySetupFallbackFatal();
        return false;
    }
} // namespace gc::auto_play
