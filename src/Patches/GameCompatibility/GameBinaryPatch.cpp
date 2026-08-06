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

constexpr std::uint32_t kSupportedTimestamp = 0x5FA90825U;
constexpr std::uint32_t kSupportedPreferredImageBase = 0x00400000U;
constexpr std::uint32_t kSupportedEntryPointRva = 0x0010964AU;
constexpr std::uint32_t kSupportedImageSize = 0x00433000U;
constexpr std::uint32_t kSupportedHeaderSize = 0x00000400U;
constexpr std::uint16_t kSupportedSectionCount = 5;

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

[[nodiscard]] bool CheckedAddress(
    std::uintptr_t image_base,
    std::uint64_t offset,
    std::size_t size,
    std::uint64_t maximum_size,
    std::uintptr_t& address) noexcept {
    if (size == 0 || offset > maximum_size ||
        size > maximum_size - offset) {
        return false;
    }
    constexpr auto maximum_address =
        std::numeric_limits<std::uintptr_t>::max();
    if (offset > maximum_address - image_base) {
        return false;
    }
    address = image_base + static_cast<std::uintptr_t>(offset);
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

[[nodiscard]] GameBinaryPatchError IdentityError(
    GameBinaryIdentityField field,
    std::uint64_t expected,
    std::uint64_t actual) noexcept {
    return {
        .stage = GameBinaryPatchStage::IdentityMismatch,
        .identity_field = field,
        .expected_identity = expected,
        .actual_identity = actual,
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

    std::uintptr_t dos_address{};
    if (!CheckedAddress(
            image_base,
            0,
            sizeof(IMAGE_DOS_HEADER),
            kSupportedHeaderSize,
            dos_address)) {
        return std::unexpected(AddressRangeError());
    }

    IMAGE_DOS_HEADER dos{};
    const auto dos_read = actions.read(
        actions.context,
        dos_address,
        std::as_writable_bytes(std::span{&dos, 1}));
    if (!dos_read) {
        return std::unexpected(ReadError(
            GameBinaryPatchStage::HeaderRead,
            dos_read.error()));
    }
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::DosMagic,
            IMAGE_DOS_SIGNATURE,
            static_cast<std::uint16_t>(dos.e_magic)));
    }
    if (dos.e_lfanew < 0) {
        return std::unexpected(AddressRangeError());
    }

    const auto nt_rva = static_cast<std::uint32_t>(dos.e_lfanew);
    std::uintptr_t nt_address{};
    if (!CheckedAddress(
            image_base,
            nt_rva,
            sizeof(IMAGE_NT_HEADERS32),
            kSupportedHeaderSize,
            nt_address)) {
        return std::unexpected(AddressRangeError(
            GameBinaryPatchSite::None,
            nt_rva));
    }

    IMAGE_NT_HEADERS32 nt{};
    const auto nt_read = actions.read(
        actions.context,
        nt_address,
        std::as_writable_bytes(std::span{&nt, 1}));
    if (!nt_read) {
        return std::unexpected(ReadError(
            GameBinaryPatchStage::HeaderRead,
            nt_read.error(),
            GameBinaryPatchSite::None,
            nt_rva));
    }

    if (nt.Signature != IMAGE_NT_SIGNATURE) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::NtSignature,
            IMAGE_NT_SIGNATURE,
            nt.Signature));
    }
    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::OptionalHeaderMagic,
            IMAGE_NT_OPTIONAL_HDR32_MAGIC,
            nt.OptionalHeader.Magic));
    }
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::Machine,
            IMAGE_FILE_MACHINE_I386,
            nt.FileHeader.Machine));
    }
    if (nt.FileHeader.TimeDateStamp != kSupportedTimestamp) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::Timestamp,
            kSupportedTimestamp,
            nt.FileHeader.TimeDateStamp));
    }
    if (nt.OptionalHeader.ImageBase != kSupportedPreferredImageBase) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::PreferredImageBase,
            kSupportedPreferredImageBase,
            nt.OptionalHeader.ImageBase));
    }
    if (nt.OptionalHeader.AddressOfEntryPoint != kSupportedEntryPointRva) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::EntryPointRva,
            kSupportedEntryPointRva,
            nt.OptionalHeader.AddressOfEntryPoint));
    }
    if (nt.OptionalHeader.SizeOfImage != kSupportedImageSize) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::SizeOfImage,
            kSupportedImageSize,
            nt.OptionalHeader.SizeOfImage));
    }
    if (nt.OptionalHeader.SizeOfHeaders != kSupportedHeaderSize) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::SizeOfHeaders,
            kSupportedHeaderSize,
            nt.OptionalHeader.SizeOfHeaders));
    }
    if (nt.FileHeader.NumberOfSections != kSupportedSectionCount) {
        return std::unexpected(IdentityError(
            GameBinaryIdentityField::SectionCount,
            kSupportedSectionCount,
            nt.FileHeader.NumberOfSections));
    }

    std::array<GameBinaryBytePattern, kGameBinaryPatchSiteCount> actual{};
    std::array<std::uintptr_t, kGameBinaryPatchSiteCount> addresses{};
    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        if (contract.clean.size != contract.patched.size ||
            contract.clean.size == 0 ||
            !CheckedAddress(
                image_base,
                contract.rva,
                contract.clean.size,
                kSupportedImageSize,
                addresses[index])) {
            return std::unexpected(AddressRangeError(
                contract.site,
                contract.rva));
        }

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

    for (std::size_t index = 1; index < states.size(); ++index) {
        if (states[index] == states.front()) {
            continue;
        }
        const auto& contract = kContracts[index];
        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::MixedState,
            .site = contract.site,
            .rva = contract.rva,
            .expected_clean = contract.clean,
            .expected_patched = contract.patched,
            .actual = actual[index],
        });
    }

    if (states.front() == SiteState::Patched) {
        return GameBinaryPatchResult{
            .state = GameBinaryImageState::AlreadyPatchedImage,
            .site_count = kContracts.size(),
        };
    }

    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        const std::size_t possibly_applied = index + 1;
        const auto write = actions.write(
            actions.context,
            addresses[index],
            contract.patched.view());
        if (write) {
            continue;
        }

        GameBinaryPatchError error{
            .stage = GameBinaryPatchStage::SiteWrite,
            .site = contract.site,
            .rva = contract.rva,
            .expected_clean = contract.clean,
            .expected_patched = contract.patched,
            .actual = actual[index],
            .memory_stage = write.error().stage,
            .win32_error = write.error().win32_error,
            .rollback_attempted = true,
        };

        bool rollback_complete = true;
        for (std::size_t rollback = possibly_applied; rollback != 0;
             --rollback) {
            const auto restore_index = rollback - 1;
            if (!actions.write(
                    actions.context,
                    addresses[restore_index],
                    kContracts[restore_index].clean.view())) {
                rollback_complete = false;
            }
        }
        for (std::size_t verify = 0; verify < possibly_applied; ++verify) {
            GameBinaryBytePattern restored{};
            restored.size = kContracts[verify].clean.size;
            const auto read = actions.read(
                actions.context,
                addresses[verify],
                std::span{
                    restored.bytes.data(),
                    static_cast<std::size_t>(restored.size),
                });
            if (!read || !Matches(restored, kContracts[verify].clean)) {
                rollback_complete = false;
            }
        }
        error.rollback_complete = rollback_complete;
        return std::unexpected(error);
    }

    return GameBinaryPatchResult{
        .state = GameBinaryImageState::PatchedCleanImage,
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
    case GameBinaryPatchStage::HeaderRead: return "header_read";
    case GameBinaryPatchStage::IdentityMismatch: return "identity_mismatch";
    case GameBinaryPatchStage::AddressRange: return "address_range";
    case GameBinaryPatchStage::SiteRead: return "site_read";
    case GameBinaryPatchStage::UnknownBytes: return "unknown_bytes";
    case GameBinaryPatchStage::MixedState: return "mixed_state";
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

const char* GameBinaryIdentityFieldName(
    GameBinaryIdentityField field) noexcept {
    switch (field) {
    case GameBinaryIdentityField::None: return "none";
    case GameBinaryIdentityField::DosMagic: return "dos_magic";
    case GameBinaryIdentityField::NtSignature: return "nt_signature";
    case GameBinaryIdentityField::OptionalHeaderMagic:
        return "optional_header_magic";
    case GameBinaryIdentityField::Machine: return "machine";
    case GameBinaryIdentityField::Timestamp: return "timestamp";
    case GameBinaryIdentityField::PreferredImageBase:
        return "preferred_image_base";
    case GameBinaryIdentityField::EntryPointRva: return "entry_point_rva";
    case GameBinaryIdentityField::SizeOfImage: return "size_of_image";
    case GameBinaryIdentityField::SizeOfHeaders: return "size_of_headers";
    case GameBinaryIdentityField::SectionCount: return "section_count";
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
    case GameBinaryImageState::PatchedCleanImage: return "patched_clean";
    case GameBinaryImageState::AlreadyPatchedImage: return "already_patched";
    }
    return "unknown";
}

} // namespace gc::game_compatibility
