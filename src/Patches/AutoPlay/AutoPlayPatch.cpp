#include "AutoPlayPatch.h"

#include "AutoPlayMarker.h"
#include "AutoPlayPatchDiagnostics.h"
#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <safetyhook.hpp>

#include "plog/Log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <ranges>
#include <span>
#include <utility>

namespace gc::auto_play
{
    namespace
    {
        template <std::uint8_t... Values>
        consteval AutoPlayBytePattern Pattern() noexcept
        {
            static_assert(sizeof...(Values) <= kMaximumAutoPlayPatternBytes);
            AutoPlayBytePattern pattern{};
            std::size_t index{};
            ((pattern.bytes[index++] = std::byte{Values}), ...);
            pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
            return pattern;
        }

        struct DirectContract
        {
            AutoPlayContractSite site;
            std::uint32_t rva;
            AutoPlayBytePattern clean;
            AutoPlayBytePattern patched;
        };

        struct ReadOnlyContract
        {
            AutoPlayContractSite site;
            std::uint32_t rva;
            AutoPlayBytePattern native;
        };

        constexpr std::array<DirectContract, 3> kDirectContracts{
            DirectContract{
                AutoPlayContractSite::do_not_save_card_data,
                0x00269951U,
                Pattern<0x0F, 0x95, 0xC1>(),
                Pattern<0xB1, 0x01, 0x90>()},
            DirectContract{
                AutoPlayContractSite::complete_is_mute,
                0x0003CAFAU,
                Pattern<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
                Pattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
            DirectContract{
                AutoPlayContractSite::native_auto_play,
                0x0003CADAU,
                Pattern<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
                Pattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
        };

        constexpr ReadOnlyContract kMarkerContract{
            AutoPlayContractSite::marker_seam,
            0x00058BE9U,
            Pattern<
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
            Pattern<0x55, 0x8B, 0xEC, 0x6A, 0xFF>(),
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

        [[nodiscard]] bool CheckedAddress(
            const std::uintptr_t image_base,
            const std::uint32_t rva,
            const std::size_t size,
            std::uintptr_t& address) noexcept
        {
            const auto maximum =
                (std::numeric_limits<std::uintptr_t>::max)();
            if (image_base == 0 || size == 0 || image_base > maximum - rva)
            {
                return false;
            }
            address = image_base + rva;
            return size - 1 <= maximum - address;
        }

        [[nodiscard]] AutoPlayBytePattern CopyPattern(
            const std::span<const std::byte> bytes) noexcept
        {
            AutoPlayBytePattern pattern{};
            if (bytes.size() > pattern.bytes.size())
            {
                return pattern;
            }
            std::ranges::copy(bytes, pattern.bytes.begin());
            pattern.size = static_cast<std::uint8_t>(bytes.size());
            return pattern;
        }

        [[nodiscard]] bool Matches(
            const std::span<const std::byte> actual,
            const AutoPlayBytePattern& expected) noexcept
        {
            return std::ranges::equal(actual, expected.view());
        }

        [[nodiscard]] AutoPlayPatchError AddressError(
            const AutoPlayContractSite site,
            const std::uint32_t rva,
            const AutoPlayBytePattern& clean,
            const AutoPlayBytePattern& patched = {}) noexcept
        {
            return {
                .stage = AutoPlayPatchStage::address_range,
                .site = site,
                .rva = rva,
                .expected_clean = clean,
                .expected_patched = patched,
            };
        }

        [[nodiscard]] AutoPlayPatchError ReadError(
            const AutoPlayContractSite site,
            const std::uint32_t rva,
            const AutoPlayBytePattern& clean,
            const AutoPlayBytePattern& patched,
            const game_compatibility::GameBinaryMemoryError& memory) noexcept
        {
            return {
                .stage = AutoPlayPatchStage::preflight_read,
                .site = site,
                .rva = rva,
                .expected_clean = clean,
                .expected_patched = patched,
                .memory_stage = memory.stage,
                .win32_error = memory.win32_error,
            };
        }

        [[nodiscard]] AutoPlayPatchError MismatchError(
            const AutoPlayContractSite site,
            const std::uint32_t rva,
            const AutoPlayBytePattern& clean,
            const AutoPlayBytePattern& patched,
            const std::span<const std::byte> actual) noexcept
        {
            return {
                .stage = AutoPlayPatchStage::byte_mismatch,
                .site = site,
                .rva = rva,
                .expected_clean = clean,
                .expected_patched = patched,
                .actual = CopyPattern(actual),
            };
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

        [[nodiscard]] std::expected<std::uintptr_t, DWORD>
        ResolveImageBase() noexcept
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
                return InstallResult{
                    .state = InstallState::already_enabled,
                    .direct_patched = g_runtime.direct_patched,
                    .direct_existing = g_runtime.direct_existing,
                };
            }

            g_runtime.native_text_address = 0;
            g_runtime.direct_patched = 0;
            g_runtime.direct_existing = 0;

            const auto resolved = ResolveImageBase();
            if (!resolved)
            {
                return std::unexpected(AutoPlayPatchError{
                    .stage = AutoPlayPatchStage::resolve_image_base,
                    .win32_error = resolved.error(),
                });
            }
            const auto image_base = *resolved;
            const auto memory =
                game_compatibility::ProductionGameBinaryPatchActions();

            std::array<std::uintptr_t, kDirectContracts.size()>
                direct_addresses{};
            for (std::size_t index = 0; index < kDirectContracts.size(); ++index)
            {
                const auto& contract = kDirectContracts[index];
                if (!CheckedAddress(
                        image_base,
                        contract.rva,
                        contract.clean.size,
                        direct_addresses[index]))
                {
                    return std::unexpected(AddressError(
                        contract.site,
                        contract.rva,
                        contract.clean,
                        contract.patched));
                }
            }

            std::uintptr_t marker_address{};
            if (!CheckedAddress(
                    image_base,
                    kMarkerContract.rva,
                    kMarkerContract.native.size,
                    marker_address))
            {
                return std::unexpected(AddressError(
                    kMarkerContract.site,
                    kMarkerContract.rva,
                    kMarkerContract.native));
            }

            std::uintptr_t native_text_address{};
            if (!CheckedAddress(
                    image_base,
                    kNativeTextContract.rva,
                    kNativeTextContract.native.size,
                    native_text_address))
            {
                return std::unexpected(AddressError(
                    kNativeTextContract.site,
                    kNativeTextContract.rva,
                    kNativeTextContract.native));
            }

            std::array<bool, kDirectContracts.size()> already_patched{};
            std::array<std::byte, kMaximumAutoPlayPatternBytes> actual_bytes{};
            std::size_t direct_existing{};
            for (std::size_t index = 0; index < kDirectContracts.size(); ++index)
            {
                const auto& contract = kDirectContracts[index];
                auto actual =
                    std::span{actual_bytes}.first(contract.clean.size);
                const auto read = memory.read(
                    memory.context,
                    direct_addresses[index],
                    actual);
                if (!read)
                {
                    return std::unexpected(ReadError(
                        contract.site,
                        contract.rva,
                        contract.clean,
                        contract.patched,
                        read.error()));
                }
                if (Matches(actual, contract.clean))
                {
                    continue;
                }
                if (Matches(actual, contract.patched))
                {
                    already_patched[index] = true;
                    ++direct_existing;
                    continue;
                }
                return std::unexpected(MismatchError(
                    contract.site,
                    contract.rva,
                    contract.clean,
                    contract.patched,
                    actual));
            }

            const auto preflight_read_only = [&memory, &actual_bytes](
                                                 const ReadOnlyContract& contract,
                                                 const std::uintptr_t address)
                -> std::expected<void, AutoPlayPatchError>
            {
                auto actual =
                    std::span{actual_bytes}.first(contract.native.size);
                const auto read =
                    memory.read(memory.context, address, actual);
                if (!read)
                {
                    return std::unexpected(ReadError(
                        contract.site,
                        contract.rva,
                        contract.native,
                        {},
                        read.error()));
                }
                if (!Matches(actual, contract.native))
                {
                    return std::unexpected(MismatchError(
                        contract.site,
                        contract.rva,
                        contract.native,
                        {},
                        actual));
                }
                return {};
            };

            if (const auto marker_preflight = preflight_read_only(
                    kMarkerContract,
                    marker_address);
                !marker_preflight)
            {
                return std::unexpected(marker_preflight.error());
            }
            if (const auto text_preflight = preflight_read_only(
                    kNativeTextContract,
                    native_text_address);
                !text_preflight)
            {
                return std::unexpected(text_preflight.error());
            }

            g_runtime.native_text_address = native_text_address;
            std::array<std::size_t, kDirectContracts.size()> owned_indices{};
            std::size_t owned_count{};

            const auto rollback = [&memory, &direct_addresses, &owned_indices,
                                   &owned_count](AutoPlayPatchError error)
                -> std::expected<InstallResult, AutoPlayPatchError>
            {
                error.rollback_attempted = true;
                error.rollback_complete = true;
                try
                {
                    g_runtime.marker_hook.reset();
                }
                catch (...)
                {
                    error.rollback_complete = false;
                    error.rollback_site = AutoPlayContractSite::marker_seam;
                }

                while (owned_count > 0)
                {
                    const auto index = owned_indices[--owned_count];
                    const auto& contract = kDirectContracts[index];
                    const auto restored = memory.write(
                        memory.context,
                        direct_addresses[index],
                        contract.clean.view());
                    if (!restored)
                    {
                        if (error.rollback_complete)
                        {
                            error.rollback_site = contract.site;
                            error.rollback_memory_stage =
                                restored.error().stage;
                            error.rollback_win32_error =
                                restored.error().win32_error;
                        }
                        error.rollback_complete = false;
                    }
                }

                g_runtime.native_text_address = 0;
                g_runtime.direct_patched = 0;
                g_runtime.direct_existing = 0;
                return std::unexpected(error);
            };

            const auto installed = InstallMarkerHook(marker_address);
            if (!installed)
            {
                return rollback(AutoPlayPatchError{
                    .stage = AutoPlayPatchStage::hook_install,
                    .site = AutoPlayContractSite::marker_seam,
                    .rva = kMarkerContract.rva,
                    .expected_clean = kMarkerContract.native,
                    .safetyhook_error = installed.error(),
                });
            }

            for (std::size_t index = 0; index < kDirectContracts.size(); ++index)
            {
                if (already_patched[index])
                {
                    continue;
                }

                const auto& contract = kDirectContracts[index];
                owned_indices[owned_count++] = index;
                const auto written = memory.write(
                    memory.context,
                    direct_addresses[index],
                    contract.patched.view());
                if (!written)
                {
                    return rollback(AutoPlayPatchError{
                        .stage = AutoPlayPatchStage::direct_write,
                        .site = contract.site,
                        .rva = contract.rva,
                        .expected_clean = contract.clean,
                        .expected_patched = contract.patched,
                        .actual = contract.clean,
                        .memory_stage = written.error().stage,
                        .win32_error = written.error().win32_error,
                    });
                }
            }

            g_runtime.direct_patched = owned_count;
            g_runtime.direct_existing = direct_existing;
            g_runtime.marker_active.store(true, std::memory_order_release);
            return InstallResult{
                .state = InstallState::enabled,
                .direct_patched = owned_count,
                .direct_existing = direct_existing,
            };
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
