#pragma once

#include "Patches/RuntimeImage/RuntimeImageError.h"
#include <optional>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::auto_play
{
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
        runtime_image::BytePattern expected_clean{};
        runtime_image::BytePattern expected_patched{};
        runtime_image::BytePattern actual{};
        std::optional<runtime_image::RuntimeImageError> memory;
        std::uint32_t safetyhook_error{};
    };

    [[nodiscard]] const char* AutoPlayPatchStageName(
        AutoPlayPatchStage stage) noexcept;
    [[nodiscard]] const char* AutoPlayContractSiteName(
        AutoPlayContractSite site) noexcept;

    [[noreturn]] void PublishAutoPlaySetupFatal(
        const AutoPlayPatchError& error) noexcept;
    [[noreturn]] void PublishAutoPlaySetupFallbackFatal() noexcept;
    [[noreturn]] void PublishAutoPlayMarkerRuntimeFatal() noexcept;
} // namespace gc::auto_play
