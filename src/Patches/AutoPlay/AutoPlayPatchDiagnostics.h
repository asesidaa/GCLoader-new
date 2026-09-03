#pragma once

#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::auto_play
{
    inline constexpr std::size_t kMaximumAutoPlayPatternBytes = 10;

    struct AutoPlayBytePattern
    {
        std::array<std::byte, kMaximumAutoPlayPatternBytes> bytes{};
        std::uint8_t size{};

        [[nodiscard]] std::span<const std::byte> view() const noexcept
        {
            return {bytes.data(), static_cast<std::size_t>(size)};
        }
    };

    enum class AutoPlayContractSite : std::uint8_t
    {
        none,
        do_not_save_card_data,
        complete_is_mute,
        native_auto_play,
        marker_seam,
        native_debug_text,
    };

    enum class AutoPlayPatchStage : std::uint8_t
    {
        none,
        resolve_image_base,
        address_range,
        preflight_read,
        byte_mismatch,
        hook_install,
        direct_write,
    };

    struct AutoPlayPatchError
    {
        AutoPlayPatchStage stage{AutoPlayPatchStage::none};
        AutoPlayContractSite site{AutoPlayContractSite::none};
        std::uint32_t rva{};
        AutoPlayBytePattern expected_clean{};
        AutoPlayBytePattern expected_patched{};
        AutoPlayBytePattern actual{};
        game_compatibility::GameBinaryMemoryStage memory_stage{
            game_compatibility::GameBinaryMemoryStage::None};
        DWORD win32_error{};
        std::uint32_t safetyhook_error{};
        bool rollback_attempted{};
        bool rollback_complete{};
        AutoPlayContractSite rollback_site{AutoPlayContractSite::none};
        game_compatibility::GameBinaryMemoryStage rollback_memory_stage{
            game_compatibility::GameBinaryMemoryStage::None};
        DWORD rollback_win32_error{};
    };

    [[nodiscard]] const char* AutoPlayPatchStageName(
        AutoPlayPatchStage stage) noexcept;
    [[nodiscard]] const char* AutoPlayContractSiteName(
        AutoPlayContractSite site) noexcept;

    void PublishAutoPlaySetupFatal(
        const AutoPlayPatchError& error) noexcept;
    void PublishAutoPlaySetupFallbackFatal() noexcept;
    void PublishAutoPlayMarkerRuntimeFatal() noexcept;
} // namespace gc::auto_play
