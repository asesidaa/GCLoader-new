#include "AutoPlayPatchTransaction.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>

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
    } // namespace

    std::expected<AutoPlayPatchResult, AutoPlayPatchError>
    InstallAutoPlayPatch(
        const bool enabled,
        AutoPlayRuntimeState& runtime,
        const AutoPlayPatchActions& actions) noexcept
    {
        if (!enabled)
        {
            return AutoPlayPatchResult{
                .state = AutoPlayPatchState::disabled,
            };
        }
        if (runtime.marker_active.load(std::memory_order_acquire))
        {
            return AutoPlayPatchResult{
                .state = AutoPlayPatchState::already_enabled,
                .direct_patched = runtime.direct_patched,
                .direct_existing = runtime.direct_existing,
            };
        }
        if (actions.context == nullptr ||
            actions.resolve_image_base == nullptr || actions.read == nullptr ||
            actions.write == nullptr || actions.install_marker_hook == nullptr ||
            actions.reset_marker_hook == nullptr)
        {
            return std::unexpected(AutoPlayPatchError{
                .stage = AutoPlayPatchStage::invalid_actions,
            });
        }

        runtime.native_text_address = 0;
        runtime.direct_patched = 0;
        runtime.direct_existing = 0;

        const auto resolved = actions.resolve_image_base(actions.context);
        if (!resolved)
        {
            return std::unexpected(AutoPlayPatchError{
                .stage = AutoPlayPatchStage::resolve_image_base,
                .win32_error = resolved.error(),
            });
        }
        const auto image_base = *resolved;

        std::array<std::uintptr_t, kDirectContracts.size()> direct_addresses{};
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
            auto actual = std::span{actual_bytes}.first(contract.clean.size);
            const auto read = actions.read(
                actions.context,
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

        const auto preflight_read_only = [&](
                                             const ReadOnlyContract& contract,
                                             const std::uintptr_t address)
            -> std::expected<void, AutoPlayPatchError>
        {
            auto actual = std::span{actual_bytes}.first(contract.native.size);
            const auto read = actions.read(actions.context, address, actual);
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

        if (const auto marker_preflight =
                preflight_read_only(kMarkerContract, marker_address);
            !marker_preflight)
        {
            return std::unexpected(marker_preflight.error());
        }
        if (const auto text_preflight =
                preflight_read_only(kNativeTextContract, native_text_address);
            !text_preflight)
        {
            return std::unexpected(text_preflight.error());
        }

        runtime.native_text_address = native_text_address;
        std::array<std::size_t, kDirectContracts.size()> owned_indices{};
        std::size_t owned_count{};

        const auto rollback = [&](AutoPlayPatchError error)
            -> std::expected<AutoPlayPatchResult, AutoPlayPatchError>
        {
            error.rollback_attempted = true;
            error.rollback_complete = true;
            if (!actions.reset_marker_hook(actions.context))
            {
                error.rollback_complete = false;
                error.rollback_site = AutoPlayContractSite::marker_seam;
            }

            while (owned_count > 0)
            {
                const auto index = owned_indices[--owned_count];
                const auto& contract = kDirectContracts[index];
                const auto restored = actions.write(
                    actions.context,
                    direct_addresses[index],
                    contract.clean.view());
                if (!restored)
                {
                    if (error.rollback_complete)
                    {
                        error.rollback_site = contract.site;
                        error.rollback_memory_stage = restored.error().stage;
                        error.rollback_win32_error =
                            restored.error().win32_error;
                    }
                    error.rollback_complete = false;
                }
            }

            runtime.native_text_address = 0;
            runtime.direct_patched = 0;
            runtime.direct_existing = 0;
            return std::unexpected(error);
        };

        const auto installed =
            actions.install_marker_hook(actions.context, marker_address);
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
            const auto written = actions.write(
                actions.context,
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
            owned_indices[owned_count++] = index;
        }

        runtime.direct_patched = owned_count;
        runtime.direct_existing = direct_existing;
        runtime.marker_active.store(true, std::memory_order_release);
        return AutoPlayPatchResult{
            .state = AutoPlayPatchState::enabled,
            .direct_patched = owned_count,
            .direct_existing = direct_existing,
        };
    }

    const char* AutoPlayPatchStageName(const AutoPlayPatchStage stage) noexcept
    {
        switch (stage)
        {
        case AutoPlayPatchStage::none: return "none";
        case AutoPlayPatchStage::invalid_actions: return "invalid_actions";
        case AutoPlayPatchStage::resolve_image_base:
            return "resolve_image_base";
        case AutoPlayPatchStage::address_range: return "address_range";
        case AutoPlayPatchStage::preflight_read: return "preflight_read";
        case AutoPlayPatchStage::byte_mismatch: return "byte_mismatch";
        case AutoPlayPatchStage::hook_install: return "hook_install";
        case AutoPlayPatchStage::direct_write: return "direct_write";
        }
        return "unknown";
    }

    const char* AutoPlayContractSiteName(
        const AutoPlayContractSite site) noexcept
    {
        switch (site)
        {
        case AutoPlayContractSite::none: return "none";
        case AutoPlayContractSite::do_not_save_card_data:
            return "do_not_save_card_data";
        case AutoPlayContractSite::complete_is_mute:
            return "complete_is_mute";
        case AutoPlayContractSite::native_auto_play:
            return "native_auto_play";
        case AutoPlayContractSite::marker_seam: return "marker_seam";
        case AutoPlayContractSite::native_debug_text:
            return "native_debug_text";
        }
        return "unknown";
    }

    const char* AutoPlayPatchStateName(const AutoPlayPatchState state) noexcept
    {
        switch (state)
        {
        case AutoPlayPatchState::disabled: return "disabled";
        case AutoPlayPatchState::enabled: return "enabled";
        case AutoPlayPatchState::already_enabled: return "already_enabled";
        }
        return "unknown";
    }
} // namespace gc::auto_play
