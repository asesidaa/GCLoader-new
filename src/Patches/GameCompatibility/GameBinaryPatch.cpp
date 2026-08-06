#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace gc::game_compatibility {
namespace {

struct PatchContract {
    GameBinaryPatchSite site{};
    std::uint32_t rva{};
    GameBinaryBytePattern clean{};
    GameBinaryBytePattern patched{};
};

template <std::size_t Size>
constexpr GameBinaryBytePattern Pattern(
    std::array<std::uint8_t, Size> values) noexcept {
    static_assert(Size > 0);
    static_assert(Size <= kMaximumGameBinaryPatternBytes);
    GameBinaryBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(Size);
    for (std::size_t index = 0; index < Size; ++index) {
        pattern.bytes[index] = static_cast<std::byte>(values[index]);
    }
    return pattern;
}

constexpr std::array<PatchContract, kGameBinaryPatchSiteCount> kContracts{
    PatchContract{
        GameBinaryPatchSite::NativeMouseEvents,
        0x000B0896U,
        Pattern<2>({0x75, 0x02}),
        Pattern<2>({0x90, 0x90}),
    },
    PatchContract{
        GameBinaryPatchSite::DongleFailure,
        0x00102C7BU,
        Pattern<2>({0x75, 0x3B}),
        Pattern<2>({0xEB, 0x3B}),
    },
    PatchContract{
        GameBinaryPatchSite::DongleSecurityTransmit,
        0x00103EE6U,
        Pattern<5>({0xE8, 0x45, 0xF6, 0xFF, 0xFF}),
        Pattern<5>({0x90, 0x90, 0x90, 0x90, 0x90}),
    },
    PatchContract{
        GameBinaryPatchSite::RfidComPort,
        0x002F7AC3U,
        Pattern<1>({0x31}),
        Pattern<1>({0x32}),
    },
};

enum class SiteState {
    Clean,
    Patched,
};

[[nodiscard]] bool CheckedPatchAddress(
    std::uintptr_t image_base,
    std::uint32_t rva,
    std::size_t size,
    std::uintptr_t& address) noexcept {
    if (size == 0) {
        return false;
    }
    constexpr auto maximum_address =
        std::numeric_limits<std::uintptr_t>::max();
    if (rva > maximum_address - image_base) {
        return false;
    }
    address = image_base + static_cast<std::uintptr_t>(rva);
    return size - 1 <= maximum_address - address;
}

[[nodiscard]] GameBinaryPatchError AddressRangeError(
    GameBinaryPatchSite site = GameBinaryPatchSite::None,
    std::uint32_t rva = 0) noexcept {
    return {
        .stage = GameBinaryPatchStage::AddressRange,
        .site = site,
        .rva = rva,
    };
}

[[nodiscard]] GameBinaryPatchError ReadError(
    GameBinaryPatchStage stage,
    const GameBinaryMemoryError& memory,
    GameBinaryPatchSite site = GameBinaryPatchSite::None,
    std::uint32_t rva = 0) noexcept {
    return {
        .stage = stage,
        .site = site,
        .rva = rva,
        .memory_stage = memory.stage,
        .win32_error = memory.win32_error,
    };
}

[[nodiscard]] bool Matches(
    const GameBinaryBytePattern& actual,
    const GameBinaryBytePattern& expected) noexcept {
    return actual.size == expected.size && actual.size != 0 &&
        std::equal(
            actual.view().begin(),
            actual.view().end(),
            expected.view().begin());
}

[[nodiscard]] bool CopyFromAddress(
    void* destination,
    const void* source,
    std::size_t size) noexcept {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool CopyToAddress(
    void* destination,
    const void* source,
    std::size_t size) noexcept {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

GameBinaryMemoryResult ProductionRead(
    void*,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    if (address == 0 || output.empty()) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Read,
            .win32_error = ERROR_INVALID_PARAMETER,
        });
    }
    if (!CopyFromAddress(
            output.data(),
            reinterpret_cast<const void*>(address),
            output.size())) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Read,
            .win32_error = ERROR_NOACCESS,
        });
    }
    return {};
}

GameBinaryMemoryResult ProductionWrite(
    void*,
    std::uintptr_t address,
    std::span<const std::byte> input) noexcept {
    if (address == 0 || input.empty()) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Copy,
            .win32_error = ERROR_INVALID_PARAMETER,
        });
    }

    auto* destination = reinterpret_cast<void*>(address);
    DWORD previous_protection{};
    if (!VirtualProtect(
            destination,
            input.size(),
            PAGE_EXECUTE_READWRITE,
            &previous_protection)) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Protect,
            .win32_error = GetLastError(),
        });
    }

    const bool copied = CopyToAddress(
        destination,
        input.data(),
        input.size());
    DWORD flush_error{};
    bool flushed{};
    if (copied) {
        flushed = FlushInstructionCache(
            GetCurrentProcess(), destination, input.size()) != FALSE;
        if (!flushed) {
            flush_error = GetLastError();
        }
    }

    DWORD temporary_protection{};
    const bool restored = VirtualProtect(
        destination,
        input.size(),
        previous_protection,
        &temporary_protection) != FALSE;
    const DWORD restore_error = restored ? ERROR_SUCCESS : GetLastError();

    if (!restored) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::RestoreProtection,
            .win32_error = restore_error,
        });
    }
    if (!copied) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Copy,
            .win32_error = ERROR_NOACCESS,
        });
    }
    if (!flushed) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::FlushInstructionCache,
            .win32_error = flush_error,
        });
    }
    return {};
}

} // namespace

std::expected<GameBinaryPatchResult, GameBinaryPatchError>
InstallGameBinaryPatch(
    std::uintptr_t image_base,
    GameBinaryPatchActions actions) noexcept {
    if (image_base == 0 || actions.read == nullptr || actions.write == nullptr) {
        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::InvalidActions,
        });
    }

    std::array<GameBinaryBytePattern, kGameBinaryPatchSiteCount> actual{};
    std::array<std::uintptr_t, kGameBinaryPatchSiteCount> addresses{};
    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        if (contract.clean.size != contract.patched.size ||
            contract.clean.size == 0 ||
            !CheckedPatchAddress(
                image_base,
                contract.rva,
                contract.clean.size,
                addresses[index])) {
            return std::unexpected(AddressRangeError(
                contract.site,
                contract.rva));
        }
    }

    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        actual[index].size = contract.clean.size;
        const auto site_read = actions.read(
            actions.context,
            addresses[index],
            std::span{
                actual[index].bytes.data(),
                static_cast<std::size_t>(actual[index].size),
            });
        if (!site_read) {
            return std::unexpected(ReadError(
                GameBinaryPatchStage::SiteRead,
                site_read.error(),
                contract.site,
                contract.rva));
        }
    }

    std::array<SiteState, kGameBinaryPatchSiteCount> states{};
    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        if (Matches(actual[index], contract.clean)) {
            states[index] = SiteState::Clean;
            continue;
        }
        if (Matches(actual[index], contract.patched)) {
            states[index] = SiteState::Patched;
            continue;
        }
        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::UnknownBytes,
            .site = contract.site,
            .rva = contract.rva,
            .expected_clean = contract.clean,
            .expected_patched = contract.patched,
            .actual = actual[index],
        });
    }

    if (std::ranges::all_of(
            states,
            [](SiteState state) { return state == SiteState::Patched; })) {
        return GameBinaryPatchResult{
            .state = GameBinaryImageState::AlreadyPatchedImage,
            .site_count = kContracts.size(),
        };
    }

    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        if (states[index] == SiteState::Patched) {
            continue;
        }

        const auto write = actions.write(
            actions.context,
            addresses[index],
            contract.patched.view());
        if (write) {
            continue;
        }

        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::SiteWrite,
            .site = contract.site,
            .rva = contract.rva,
            .expected_clean = contract.clean,
            .expected_patched = contract.patched,
            .actual = actual[index],
            .memory_stage = write.error().stage,
            .win32_error = write.error().win32_error,
        });
    }

    return GameBinaryPatchResult{
        .state = GameBinaryImageState::PatchedImage,
        .site_count = kContracts.size(),
    };
}

GameBinaryPatchActions ProductionGameBinaryPatchActions() noexcept {
    return {
        .context = nullptr,
        .read = ProductionRead,
        .write = ProductionWrite,
    };
}

std::expected<GameBinaryPatchResult, GameBinaryPatchError>
GameBinaryPatchInit() noexcept {
    const auto module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::ResolveModule,
            .win32_error = GetLastError(),
        });
    }
    return InstallGameBinaryPatch(
        reinterpret_cast<std::uintptr_t>(module),
        ProductionGameBinaryPatchActions());
}

const char* GameBinaryPatchStageName(GameBinaryPatchStage stage) noexcept {
    switch (stage) {
    case GameBinaryPatchStage::None: return "none";
    case GameBinaryPatchStage::ResolveModule: return "resolve_module";
    case GameBinaryPatchStage::InvalidActions: return "invalid_actions";
    case GameBinaryPatchStage::AddressRange: return "address_range";
    case GameBinaryPatchStage::SiteRead: return "site_read";
    case GameBinaryPatchStage::UnknownBytes: return "unknown_bytes";
    case GameBinaryPatchStage::SiteWrite: return "site_write";
    }
    return "unknown";
}

const char* GameBinaryPatchSiteName(GameBinaryPatchSite site) noexcept {
    switch (site) {
    case GameBinaryPatchSite::None: return "none";
    case GameBinaryPatchSite::NativeMouseEvents:
        return "native_mouse_events";
    case GameBinaryPatchSite::DongleFailure: return "dongle_failure";
    case GameBinaryPatchSite::DongleSecurityTransmit:
        return "dongle_security_transmit";
    case GameBinaryPatchSite::RfidComPort: return "rfid_com_port";
    }
    return "unknown";
}

const char* GameBinaryMemoryStageName(GameBinaryMemoryStage stage) noexcept {
    switch (stage) {
    case GameBinaryMemoryStage::None: return "none";
    case GameBinaryMemoryStage::Read: return "read";
    case GameBinaryMemoryStage::Protect: return "protect";
    case GameBinaryMemoryStage::Copy: return "copy";
    case GameBinaryMemoryStage::FlushInstructionCache:
        return "flush_instruction_cache";
    case GameBinaryMemoryStage::RestoreProtection:
        return "restore_protection";
    }
    return "unknown";
}

const char* GameBinaryImageStateName(GameBinaryImageState state) noexcept {
    switch (state) {
    case GameBinaryImageState::PatchedImage: return "patched";
    case GameBinaryImageState::AlreadyPatchedImage: return "already_patched";
    }
    return "unknown";
}

} // namespace gc::game_compatibility
