#pragma once

#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
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
        invalid_actions,
        resolve_image_base,
        address_range,
        preflight_read,
        byte_mismatch,
        hook_install,
        direct_write,
    };

    enum class AutoPlayPatchState : std::uint8_t
    {
        disabled,
        enabled,
        already_enabled,
    };

    struct AutoPlayRuntimeState
    {
        std::atomic_bool marker_active{};
        std::uintptr_t native_text_address{};
        std::size_t direct_patched{};
        std::size_t direct_existing{};
    };

    struct AutoPlayPatchActions
    {
        void* context{};
        std::expected<std::uintptr_t, DWORD> (*resolve_image_base)(
            void*) noexcept{};
        game_compatibility::GameBinaryMemoryResult (*read)(
            void*,
            std::uintptr_t,
            std::span<std::byte>) noexcept{};
        game_compatibility::GameBinaryMemoryResult (*write)(
            void*,
            std::uintptr_t,
            std::span<const std::byte>) noexcept{};
        std::expected<void, std::uint32_t> (*install_marker_hook)(
            void*,
            std::uintptr_t) noexcept{};
        bool (*reset_marker_hook)(void*) noexcept{};
    };

    struct AutoPlayPatchResult
    {
        AutoPlayPatchState state{AutoPlayPatchState::disabled};
        std::size_t direct_patched{};
        std::size_t direct_existing{};
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

    [[nodiscard]] std::expected<AutoPlayPatchResult, AutoPlayPatchError>
    InstallAutoPlayPatch(
        bool enabled,
        AutoPlayRuntimeState& runtime,
        const AutoPlayPatchActions& actions) noexcept;

    [[nodiscard]] const char* AutoPlayPatchStageName(
        AutoPlayPatchStage stage) noexcept;
    [[nodiscard]] const char* AutoPlayContractSiteName(
        AutoPlayContractSite site) noexcept;
    [[nodiscard]] const char* AutoPlayPatchStateName(
        AutoPlayPatchState state) noexcept;
} // namespace gc::auto_play
