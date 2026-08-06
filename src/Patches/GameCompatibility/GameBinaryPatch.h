#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::game_compatibility {

inline constexpr std::size_t kGameBinaryPatchSiteCount = 4;
inline constexpr std::size_t kMaximumGameBinaryPatternBytes = 5;

struct GameBinaryBytePattern {
    std::array<std::byte, kMaximumGameBinaryPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return {bytes.data(), static_cast<std::size_t>(size)};
    }

    friend bool operator==(
        const GameBinaryBytePattern&,
        const GameBinaryBytePattern&) = default;
};

enum class GameBinaryPatchSite {
    None,
    NativeMouseEvents,
    DongleFailure,
    DongleSecurityTransmit,
    RfidComPort,
};

enum class GameBinaryImageState {
    PatchedCleanImage,
    AlreadyPatchedImage,
};

enum class GameBinaryIdentityField {
    None,
    DosMagic,
    NtSignature,
    OptionalHeaderMagic,
    Machine,
    Timestamp,
    PreferredImageBase,
    EntryPointRva,
    SizeOfImage,
    SizeOfHeaders,
    SectionCount,
};

enum class GameBinaryMemoryStage {
    None,
    Read,
    Protect,
    Copy,
    FlushInstructionCache,
    RestoreProtection,
};

enum class GameBinaryPatchStage {
    None,
    ResolveModule,
    InvalidActions,
    HeaderRead,
    IdentityMismatch,
    AddressRange,
    SiteRead,
    UnknownBytes,
    MixedState,
    SiteWrite,
};

struct GameBinaryMemoryError {
    GameBinaryMemoryStage stage{GameBinaryMemoryStage::None};
    DWORD win32_error{};
};

using GameBinaryMemoryResult =
    std::expected<void, GameBinaryMemoryError>;

struct GameBinaryPatchActions {
    void* context{};
    GameBinaryMemoryResult (*read)(
        void*,
        std::uintptr_t,
        std::span<std::byte>) noexcept{};
    GameBinaryMemoryResult (*write)(
        void*,
        std::uintptr_t,
        std::span<const std::byte>) noexcept{};
};

struct GameBinaryPatchError {
    GameBinaryPatchStage stage{GameBinaryPatchStage::None};
    GameBinaryPatchSite site{GameBinaryPatchSite::None};
    GameBinaryIdentityField identity_field{GameBinaryIdentityField::None};
    std::uint32_t rva{};
    std::uint64_t expected_identity{};
    std::uint64_t actual_identity{};
    GameBinaryBytePattern expected_clean{};
    GameBinaryBytePattern expected_patched{};
    GameBinaryBytePattern actual{};
    GameBinaryMemoryStage memory_stage{GameBinaryMemoryStage::None};
    DWORD win32_error{};
    bool rollback_attempted{};
    bool rollback_complete{};
};

struct GameBinaryPatchResult {
    GameBinaryImageState state{};
    std::size_t site_count{};
};

[[nodiscard]] std::expected<
    GameBinaryPatchResult,
    GameBinaryPatchError>
InstallGameBinaryPatch(
    std::uintptr_t image_base,
    GameBinaryPatchActions actions) noexcept;

[[nodiscard]] GameBinaryPatchActions
ProductionGameBinaryPatchActions() noexcept;

[[nodiscard]] std::expected<
    GameBinaryPatchResult,
    GameBinaryPatchError>
GameBinaryPatchInit() noexcept;

[[nodiscard]] const char* GameBinaryPatchStageName(
    GameBinaryPatchStage stage) noexcept;
[[nodiscard]] const char* GameBinaryPatchSiteName(
    GameBinaryPatchSite site) noexcept;
[[nodiscard]] const char* GameBinaryIdentityFieldName(
    GameBinaryIdentityField field) noexcept;
[[nodiscard]] const char* GameBinaryMemoryStageName(
    GameBinaryMemoryStage stage) noexcept;
[[nodiscard]] const char* GameBinaryImageStateName(
    GameBinaryImageState state) noexcept;

} // namespace gc::game_compatibility
