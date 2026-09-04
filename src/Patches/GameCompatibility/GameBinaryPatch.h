#pragma once

#include "Patches/RuntimeImage/RuntimeImage.h"

#include <cstddef>
#include <expected>
#include <optional>

namespace gc::game_compatibility {

inline constexpr std::size_t kGameBinaryPatchSiteCount = 4;

enum class GameBinaryPatchSite {
    None, NativeMouseEvents, DongleFailure, DongleSecurityTransmit, RfidComPort,
};

enum class GameBinaryImageState { PatchedImage, AlreadyPatchedImage };

enum class GameBinaryPatchStage {
    None, ResolveModule, AddressRange, SiteRead, UnknownBytes, MixedState, SiteWrite,
};

struct GameBinaryPatchError {
    GameBinaryPatchStage stage{GameBinaryPatchStage::None};
    GameBinaryPatchSite site{GameBinaryPatchSite::None};
    std::uint32_t rva{};
    runtime_image::BytePattern expected_clean{};
    runtime_image::BytePattern expected_patched{};
    runtime_image::BytePattern actual{};
    std::optional<runtime_image::RuntimeImageError> memory;
};

struct GameBinaryPatchResult {
    GameBinaryImageState state{};
    std::size_t site_count{};
};

[[nodiscard]] std::expected<GameBinaryPatchResult, GameBinaryPatchError>
InstallGameBinaryPatch(const runtime_image::RuntimeImage& image) noexcept;
[[nodiscard]] std::expected<GameBinaryPatchResult, GameBinaryPatchError>
GameBinaryPatchInit() noexcept;

[[nodiscard]] const char* GameBinaryPatchStageName(GameBinaryPatchStage) noexcept;
[[nodiscard]] const char* GameBinaryPatchSiteName(GameBinaryPatchSite) noexcept;
[[nodiscard]] const char* GameBinaryImageStateName(GameBinaryImageState) noexcept;

} // namespace gc::game_compatibility
