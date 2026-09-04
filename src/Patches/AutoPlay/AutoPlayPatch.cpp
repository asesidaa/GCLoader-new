#include "AutoPlayPatch.h"

#include "AutoPlayMarker.h"
#include "AutoPlayPatchDiagnostics.h"
#include "Patches/RuntimeImage/RuntimeImage.h"

#include <Windows.h>
#include <safetyhook.hpp>
#include "plog/Log.h"

#include <array>
#include <atomic>
#include <expected>
#include <limits>
#include <utility>

namespace gc::auto_play
{
    namespace
    {
        struct DirectContract
        {
            AutoPlayContractSite site;
            std::uint32_t rva;
            runtime_image::BytePattern clean;
            runtime_image::BytePattern patched;
        };

        struct ReadOnlyContract
        {
            AutoPlayContractSite site;
            std::uint32_t rva;
            runtime_image::BytePattern native;
        };

        constexpr std::array<DirectContract, 3> kDirectContracts{
            DirectContract{
                AutoPlayContractSite::do_not_save_card_data,
                0x00269951U,
                runtime_image::PatternOf<0x0F, 0x95, 0xC1>(),
                runtime_image::PatternOf<0xB1, 0x01, 0x90>()},
            DirectContract{
                AutoPlayContractSite::complete_is_mute,
                0x0003CAFAU,
                runtime_image::PatternOf<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
                runtime_image::PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
            DirectContract{
                AutoPlayContractSite::native_auto_play,
                0x0003CADAU,
                runtime_image::PatternOf<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
                runtime_image::PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
        };

        constexpr ReadOnlyContract kMarkerContract{
            AutoPlayContractSite::marker_seam,
            0x00058BE9U,
            runtime_image::PatternOf<
                0x8D,
                0x44,
                0x24,
                0x08,
                0x50,
                0xE8,
                0x8D,
                0x03,
                0x00,
                0x00>(),
        };

        constexpr ReadOnlyContract kNativeTextContract{
            AutoPlayContractSite::native_debug_text,
            0x00069650U,
            runtime_image::PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF>(),
        };

        struct AutoPlayRuntime
        {
            std::atomic_bool marker_active{};
            std::uintptr_t native_text_address{};
            std::size_t direct_patched{};
            std::size_t direct_existing{};
            safetyhook::MidHook marker_hook;
        };

        enum class InstallState : std::uint8_t
        {
            enabled,
            already_enabled,
        };

        struct InstallResult
        {
            InstallState state{};
            std::size_t direct_patched{};
            std::size_t direct_existing{};
        };

        AutoPlayRuntime g_runtime;
        std::atomic_bool g_already_enabled_logged{};


        runtime_image::SiteIdentity Identity(AutoPlayContractSite site, std::uint32_t rva) noexcept
        {
            return {"AutoPlay", AutoPlayContractSiteName(site), rva};
        }

        [[nodiscard]] AutoPlayPatchError MemoryError(
            AutoPlayPatchStage stage, AutoPlayContractSite site, std::uint32_t rva,
            const runtime_image::RuntimeImageError& memory) noexcept
        {
            return {.stage = stage, .site = site, .rva = rva, .actual = memory.observed,
                    .memory = memory};
        }

        void AutoPlayMarkerMidHook(safetyhook::Context&) noexcept
        {
            try
            {
                if (!g_runtime.marker_active.load(std::memory_order_acquire))
                {
                    return;
                }

                const auto function =
                    reinterpret_cast<NativeDebugTextFunction>(
                        g_runtime.native_text_address);
                if (!DrawAutoPlayMarker(function))
                {
                    PublishAutoPlayMarkerRuntimeFatal();
                }
            }
            catch (...)
            {
                PublishAutoPlayMarkerRuntimeFatal();
            }
        }

        [[nodiscard]] std::expected<void, std::uint32_t>
        InstallMarkerHook(const std::uintptr_t address) noexcept
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


        [[nodiscard]] std::expected<InstallResult, AutoPlayPatchError>
        InstallAutoPlayPatch() noexcept
        {
            if (g_runtime.marker_active.load(std::memory_order_acquire))
            {
                return InstallResult{InstallState::already_enabled,
                    g_runtime.direct_patched, g_runtime.direct_existing};
            }
            g_runtime.native_text_address = 0;
            g_runtime.direct_patched = 0;
            g_runtime.direct_existing = 0;

            const auto image = runtime_image::RuntimeImage::MainModule();
            if (!image)
            {
                return std::unexpected(MemoryError(AutoPlayPatchStage::resolve_image_base,
                    AutoPlayContractSite::none, 0, image.error()));
            }

            std::array<bool, kDirectContracts.size()> already_patched{};
            std::size_t direct_existing{};
            for (std::size_t index = 0; index < kDirectContracts.size(); ++index)
            {
                const auto& contract = kDirectContracts[index];
                const auto actual = image->Read(Identity(contract.site, contract.rva), contract.clean.size);
                if (!actual)
                {
                    auto error = MemoryError(AutoPlayPatchStage::preflight_read,
                        contract.site, contract.rva, actual.error());
                    error.expected_clean = contract.clean;
                    error.expected_patched = contract.patched;
                    return std::unexpected(error);
                }
                if (*actual == contract.clean)
                {
                    continue;
                }
                if (*actual == contract.patched)
                {
                    already_patched[index] = true;
                    ++direct_existing;
                    continue;
                }
                return std::unexpected(AutoPlayPatchError{
                    .stage = AutoPlayPatchStage::byte_mismatch, .site = contract.site,
                    .rva = contract.rva, .expected_clean = contract.clean,
                    .expected_patched = contract.patched, .actual = *actual});
            }

            const auto check_native = [&image](const ReadOnlyContract& contract)
                -> std::expected<std::uintptr_t, AutoPlayPatchError>
            {
                const auto identity = Identity(contract.site, contract.rva);
                const auto actual = image->Read(identity, contract.native.size);
                if (!actual)
                {
                    auto error = MemoryError(AutoPlayPatchStage::preflight_read,
                        contract.site, contract.rva, actual.error());
                    error.expected_clean = contract.native;
                    return std::unexpected(error);
                }
                if (*actual != contract.native)
                {
                    return std::unexpected(AutoPlayPatchError{
                        .stage = AutoPlayPatchStage::byte_mismatch, .site = contract.site,
                        .rva = contract.rva, .expected_clean = contract.native, .actual = *actual});
                }
                const auto address = image->Resolve(identity, contract.native.size);
                if (!address)
                {
                    return std::unexpected(MemoryError(AutoPlayPatchStage::address_range,
                        contract.site, contract.rva, address.error()));
                }
                return *address;
            };
            const auto marker_address = check_native(kMarkerContract);
            if (!marker_address)
            {
                return std::unexpected(marker_address.error());
            }
            const auto native_text = check_native(kNativeTextContract);
            if (!native_text)
            {
                return std::unexpected(native_text.error());
            }

            // Keep the original ordering: publish text target, install marker,
            // suppress saving, complete mute, then enable native auto play.
            g_runtime.native_text_address = *native_text;
            const auto installed = InstallMarkerHook(*marker_address);
            if (!installed)
            {
                PublishAutoPlaySetupFatal({
                    .stage = AutoPlayPatchStage::hook_install,
                    .site = AutoPlayContractSite::marker_seam,
                    .rva = kMarkerContract.rva, .expected_clean = kMarkerContract.native,
                    .safetyhook_error = installed.error()});
            }

            std::size_t direct_patched{};
            for (std::size_t index = 0; index < kDirectContracts.size(); ++index)
            {
                if (already_patched[index])
                {
                    continue;
                }
                const auto& contract = kDirectContracts[index];
                const auto written = image->Write(Identity(contract.site, contract.rva),
                    contract.patched, runtime_image::MemoryKind::code);
                if (!written)
                {
                    auto error = MemoryError(AutoPlayPatchStage::direct_write,
                        contract.site, contract.rva, written.error());
                    error.expected_clean = contract.clean;
                    error.expected_patched = contract.patched;
                    PublishAutoPlaySetupFatal(error);
                }
                ++direct_patched;
            }

            g_runtime.direct_patched = direct_patched;
            g_runtime.direct_existing = direct_existing;
            g_runtime.marker_active.store(true, std::memory_order_release);
            return InstallResult{InstallState::enabled, direct_patched, direct_existing};
        }
    } // namespace

    bool AutoPlayPatchInit(const bool enabled) noexcept
    {
        if (!enabled)
        {
            PLOG_INFO << "AutoPlayPatch: state=disabled";
            return true;
        }

        try
        {
            const auto result = InstallAutoPlayPatch();
            if (!result)
            {
                PublishAutoPlaySetupFatal(result.error());
                return false;
            }

            switch (result->state)
            {
            case InstallState::enabled:
                PLOG_WARNING
                    << "AutoPlayPatch: state=enabled direct_patched="
                    << result->direct_patched
                    << " direct_existing=" << result->direct_existing
                    << " marker=active score_save=disabled";
                return true;
            case InstallState::already_enabled:
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
