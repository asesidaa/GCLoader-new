#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace gc::game_compatibility;

constexpr std::uintptr_t kFakeBase = 0x10000000U;
constexpr std::size_t kSupportedImageSize = 0x00433000U;
constexpr std::uint32_t kNtHeaderOffset = 0x138U;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::vector<std::byte> Bytes(
    std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

// Independently extracted from SHA-256
// 795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF
// (clean) and
// FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
// (legacy patched).
// Do not replace these values by importing the production manifest.
struct FixtureSite {
    GameBinaryPatchSite site;
    std::uint32_t rva;
    std::vector<std::byte> clean;
    std::vector<std::byte> patched;
};

const std::array<FixtureSite, 4> kFixtureSites{
    FixtureSite{
        GameBinaryPatchSite::NativeMouseEvents,
        0x000B0896U,
        Bytes({0x75, 0x02}),
        Bytes({0x90, 0x90}),
    },
    FixtureSite{
        GameBinaryPatchSite::DongleFailure,
        0x00102C7BU,
        Bytes({0x75, 0x3B}),
        Bytes({0xEB, 0x3B}),
    },
    FixtureSite{
        GameBinaryPatchSite::DongleSecurityTransmit,
        0x00103EE6U,
        Bytes({0xE8, 0x45, 0xF6, 0xFF, 0xFF}),
        Bytes({0x90, 0x90, 0x90, 0x90, 0x90}),
    },
    FixtureSite{
        GameBinaryPatchSite::RfidComPort,
        0x002F7AC3U,
        Bytes({0x31}),
        Bytes({0x32}),
    },
};

struct WriteFailure {
    std::size_t call{};
    bool after_copy{};
    GameBinaryMemoryStage stage{GameBinaryMemoryStage::Copy};
    DWORD win32_error{ERROR_NOACCESS};
};

struct FakeImage {
    std::uintptr_t base{kFakeBase};
    std::vector<std::byte> bytes =
        std::vector<std::byte>(kSupportedImageSize);
    std::optional<std::uintptr_t> failed_read_address{};
    std::vector<WriteFailure> write_failures{};
    std::vector<std::uintptr_t> read_addresses{};
    std::vector<std::uintptr_t> write_addresses{};
    std::size_t write_calls{};

    template <typename T>
    void Store(std::size_t offset, const T& value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    template <typename T>
    T Load(std::size_t offset) const {
        T result{};
        std::memcpy(&result, bytes.data() + offset, sizeof(result));
        return result;
    }

    void StoreBytes(
        std::uint32_t rva,
        std::span<const std::byte> value) {
        std::copy(value.begin(), value.end(), bytes.begin() + rva);
    }
};

bool TranslateRange(
    const FakeImage& fake,
    std::uintptr_t address,
    std::size_t size,
    std::size_t& offset) noexcept {
    if (address < fake.base) {
        return false;
    }
    const auto distance = address - fake.base;
    if (distance > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    offset = static_cast<std::size_t>(distance);
    return offset <= fake.bytes.size() &&
        size <= fake.bytes.size() - offset;
}

GameBinaryMemoryResult FakeRead(
    void* opaque,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    auto& fake = *static_cast<FakeImage*>(opaque);
    fake.read_addresses.push_back(address);

    std::size_t offset{};
    if (fake.failed_read_address == address ||
        !TranslateRange(fake, address, output.size(), offset)) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Read,
            .win32_error = ERROR_NOACCESS,
        });
    }
    std::copy_n(fake.bytes.begin() + offset, output.size(), output.begin());
    return {};
}

GameBinaryMemoryResult FakeWrite(
    void* opaque,
    std::uintptr_t address,
    std::span<const std::byte> input) noexcept {
    auto& fake = *static_cast<FakeImage*>(opaque);
    ++fake.write_calls;
    fake.write_addresses.push_back(address);

    std::size_t offset{};
    if (!TranslateRange(fake, address, input.size(), offset)) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = GameBinaryMemoryStage::Copy,
            .win32_error = ERROR_NOACCESS,
        });
    }

    const auto failure = std::find_if(
        fake.write_failures.begin(),
        fake.write_failures.end(),
        [&](const WriteFailure& candidate) {
            return candidate.call == fake.write_calls;
        });
    if (failure != fake.write_failures.end() && !failure->after_copy) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = failure->stage,
            .win32_error = failure->win32_error,
        });
    }

    std::copy(input.begin(), input.end(), fake.bytes.begin() + offset);
    if (failure != fake.write_failures.end()) {
        return std::unexpected(GameBinaryMemoryError{
            .stage = failure->stage,
            .win32_error = failure->win32_error,
        });
    }
    return {};
}

GameBinaryPatchActions FakeActions(FakeImage& fake) noexcept {
    return {
        .context = &fake,
        .read = FakeRead,
        .write = FakeWrite,
    };
}

void WriteSupportedHeaders(FakeImage& fake) {
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = kNtHeaderOffset;
    fake.Store(0, dos);

    IMAGE_NT_HEADERS32 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.NumberOfSections = 5;
    nt.FileHeader.TimeDateStamp = 0x5FA90825U;
    nt.FileHeader.SizeOfOptionalHeader =
        sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.ImageBase = 0x00400000U;
    nt.OptionalHeader.AddressOfEntryPoint = 0x0010964AU;
    nt.OptionalHeader.SizeOfImage = 0x00433000U;
    nt.OptionalHeader.SizeOfHeaders = 0x00000400U;
    fake.Store(kNtHeaderOffset, nt);
}

FakeImage SupportedImage(bool patched = false) {
    FakeImage fake{};
    WriteSupportedHeaders(fake);
    for (const auto& fixture : kFixtureSites) {
        fake.StoreBytes(
            fixture.rva,
            patched ? std::span<const std::byte>{fixture.patched}
                    : std::span<const std::byte>{fixture.clean});
    }
    return fake;
}

bool RangeEquals(
    const FakeImage& fake,
    std::uint32_t rva,
    std::span<const std::byte> expected) {
    const auto actual = std::span{
        fake.bytes.data() + rva,
        expected.size(),
    };
    return std::equal(actual.begin(), actual.end(), expected.begin());
}

bool PatternEquals(
    const GameBinaryBytePattern& actual,
    std::span<const std::byte> expected) {
    return actual.view().size() == expected.size() &&
        std::equal(actual.view().begin(), actual.view().end(), expected.begin());
}

bool AllSitesEqual(const FakeImage& fake, bool patched) {
    return std::ranges::all_of(
        kFixtureSites,
        [&](const FixtureSite& fixture) {
            return RangeEquals(
                fake,
                fixture.rva,
                patched ? std::span<const std::byte>{fixture.patched}
                        : std::span<const std::byte>{fixture.clean});
        });
}

int TestCleanImageWritesAllFourSites() {
    auto fake = SupportedImage();
    const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    return Expect(
        result &&
            result->state == GameBinaryImageState::PatchedCleanImage &&
            result->site_count == kFixtureSites.size() &&
            fake.write_calls == kFixtureSites.size() &&
            AllSitesEqual(fake, true),
        "clean image writes all four supported patches");
}

int TestLegacyPatchedImageWritesNothing() {
    auto fake = SupportedImage(true);
    const auto before = fake.bytes;
    const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    return Expect(
        result &&
            result->state == GameBinaryImageState::AlreadyPatchedImage &&
            result->site_count == kFixtureSites.size() &&
            fake.write_calls == 0 && fake.bytes == before,
        "legacy patched image is accepted without writes");
}

int TestEveryIdentityFieldMismatchWritesNothing() {
    struct IdentityCase {
        GameBinaryIdentityField field;
        std::uint64_t expected;
        std::uint64_t actual;
    };
    constexpr std::array cases{
        IdentityCase{GameBinaryIdentityField::DosMagic,
                     IMAGE_DOS_SIGNATURE, 0},
        IdentityCase{GameBinaryIdentityField::NtSignature,
                     IMAGE_NT_SIGNATURE, 0},
        IdentityCase{GameBinaryIdentityField::OptionalHeaderMagic,
                     IMAGE_NT_OPTIONAL_HDR32_MAGIC,
                     IMAGE_NT_OPTIONAL_HDR64_MAGIC},
        IdentityCase{GameBinaryIdentityField::Machine,
                     IMAGE_FILE_MACHINE_I386, IMAGE_FILE_MACHINE_AMD64},
        IdentityCase{GameBinaryIdentityField::Timestamp,
                     0x5FA90825U, 0x5FA90826U},
        IdentityCase{GameBinaryIdentityField::PreferredImageBase,
                     0x00400000U, 0x00500000U},
        IdentityCase{GameBinaryIdentityField::EntryPointRva,
                     0x0010964AU, 0x0010964BU},
        IdentityCase{GameBinaryIdentityField::SizeOfImage,
                     0x00433000U, 0x00434000U},
        IdentityCase{GameBinaryIdentityField::SizeOfHeaders,
                     0x00000400U, 0x00000600U},
        IdentityCase{GameBinaryIdentityField::SectionCount, 5, 6},
    };

    int failures = 0;
    for (const auto& identity : cases) {
        auto fake = SupportedImage();
        auto dos = fake.Load<IMAGE_DOS_HEADER>(0);
        auto nt = fake.Load<IMAGE_NT_HEADERS32>(kNtHeaderOffset);
        switch (identity.field) {
        case GameBinaryIdentityField::DosMagic:
            dos.e_magic = static_cast<WORD>(identity.actual);
            fake.Store(0, dos);
            break;
        case GameBinaryIdentityField::NtSignature:
            nt.Signature = static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::OptionalHeaderMagic:
            nt.OptionalHeader.Magic = static_cast<WORD>(identity.actual);
            break;
        case GameBinaryIdentityField::Machine:
            nt.FileHeader.Machine = static_cast<WORD>(identity.actual);
            break;
        case GameBinaryIdentityField::Timestamp:
            nt.FileHeader.TimeDateStamp = static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::PreferredImageBase:
            nt.OptionalHeader.ImageBase = static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::EntryPointRva:
            nt.OptionalHeader.AddressOfEntryPoint =
                static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::SizeOfImage:
            nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::SizeOfHeaders:
            nt.OptionalHeader.SizeOfHeaders =
                static_cast<DWORD>(identity.actual);
            break;
        case GameBinaryIdentityField::SectionCount:
            nt.FileHeader.NumberOfSections = static_cast<WORD>(identity.actual);
            break;
        case GameBinaryIdentityField::None:
            break;
        }
        if (identity.field != GameBinaryIdentityField::DosMagic) {
            fake.Store(kNtHeaderOffset, nt);
        }

        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result &&
                result.error().stage ==
                    GameBinaryPatchStage::IdentityMismatch &&
                result.error().identity_field == identity.field &&
                result.error().expected_identity == identity.expected &&
                result.error().actual_identity == identity.actual &&
                fake.write_calls == 0,
            "identity mismatch rejects image before writes");
    }
    return failures;
}

int TestUnknownBytesAtEverySiteWriteNothing() {
    int failures = 0;
    for (const auto& fixture : kFixtureSites) {
        auto fake = SupportedImage();
        auto unknown = fixture.clean;
        unknown.front() = std::byte{0x7F};
        fake.StoreBytes(fixture.rva, unknown);

        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result &&
                result.error().stage == GameBinaryPatchStage::UnknownBytes &&
                result.error().site == fixture.site &&
                result.error().rva == fixture.rva &&
                PatternEquals(result.error().expected_clean, fixture.clean) &&
                PatternEquals(result.error().expected_patched, fixture.patched) &&
                PatternEquals(result.error().actual, unknown) &&
                fake.write_calls == 0,
            "unknown site bytes reject image before writes");
    }
    return failures;
}

int TestMixedImageWritesNothing() {
    auto fake = SupportedImage();
    const auto& differing = kFixtureSites[2];
    fake.StoreBytes(differing.rva, differing.patched);
    const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    return Expect(
        !result && result.error().stage == GameBinaryPatchStage::MixedState &&
            result.error().site == differing.site &&
            result.error().rva == differing.rva && fake.write_calls == 0 &&
            RangeEquals(fake, differing.rva, differing.patched),
        "mixed clean and patched image is rejected without writes");
}

int TestHeaderAndSiteReadFailuresWriteNothing() {
    int failures = 0;
    for (const auto address :
         std::array{kFakeBase, kFakeBase + kNtHeaderOffset}) {
        auto fake = SupportedImage();
        fake.failed_read_address = address;
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::HeaderRead &&
                result.error().memory_stage == GameBinaryMemoryStage::Read &&
                result.error().win32_error == ERROR_NOACCESS &&
                fake.write_calls == 0,
            "header read failure is reported without writes");
    }

    for (const auto& fixture : kFixtureSites) {
        auto fake = SupportedImage();
        fake.failed_read_address = fake.base + fixture.rva;
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::SiteRead &&
                result.error().site == fixture.site &&
                result.error().rva == fixture.rva &&
                result.error().memory_stage == GameBinaryMemoryStage::Read &&
                result.error().win32_error == ERROR_NOACCESS &&
                fake.write_calls == 0,
            "site read failure is reported without writes");
    }
    return failures;
}

int TestInvalidActionsAndAddressRangesTouchNothing() {
    int failures = 0;

    auto fake = SupportedImage();
    failures += Expect(
        !InstallGameBinaryPatch(0, FakeActions(fake)) &&
            fake.read_addresses.empty() && fake.write_calls == 0,
        "zero image base is rejected without memory access");

    auto missing_read = FakeActions(fake);
    missing_read.read = nullptr;
    failures += Expect(
        !InstallGameBinaryPatch(fake.base, missing_read) &&
            fake.read_addresses.empty() && fake.write_calls == 0,
        "missing read action is rejected without memory access");

    auto missing_write = FakeActions(fake);
    missing_write.write = nullptr;
    failures += Expect(
        !InstallGameBinaryPatch(fake.base, missing_write) &&
            fake.read_addresses.empty() && fake.write_calls == 0,
        "missing write action is rejected without memory access");

    auto overflowing = SupportedImage();
    overflowing.base = std::numeric_limits<std::uintptr_t>::max() - 0x100U;
    const auto overflow_result =
        InstallGameBinaryPatch(overflowing.base, FakeActions(overflowing));
    failures += Expect(
        !overflow_result &&
            overflow_result.error().stage ==
                GameBinaryPatchStage::AddressRange &&
            overflowing.read_addresses.size() == 1 &&
            overflowing.write_calls == 0,
        "overflowing NT header address is rejected after DOS read");

    auto negative_header = SupportedImage();
    auto negative_dos = negative_header.Load<IMAGE_DOS_HEADER>(0);
    negative_dos.e_lfanew = -1;
    negative_header.Store(0, negative_dos);
    const auto negative_result = InstallGameBinaryPatch(
        negative_header.base,
        FakeActions(negative_header));
    failures += Expect(
        !negative_result &&
            negative_result.error().stage ==
                GameBinaryPatchStage::AddressRange &&
            negative_header.read_addresses.size() == 1 &&
            negative_header.write_calls == 0,
        "negative NT header offset is rejected after DOS read");

    auto outside_headers = SupportedImage();
    auto outside_dos = outside_headers.Load<IMAGE_DOS_HEADER>(0);
    outside_dos.e_lfanew = 0x3F0;
    outside_headers.Store(0, outside_dos);
    const auto outside_result = InstallGameBinaryPatch(
        outside_headers.base,
        FakeActions(outside_headers));
    failures += Expect(
        !outside_result &&
            outside_result.error().stage ==
                GameBinaryPatchStage::AddressRange &&
            outside_headers.read_addresses.size() == 1 &&
            outside_headers.write_calls == 0,
        "NT headers outside supported header range are rejected");

    return failures;
}

int TestEveryForwardWriteFailureRollsBackToClean() {
    int failures = 0;
    for (std::size_t call = 1; call <= kFixtureSites.size(); ++call) {
        auto fake = SupportedImage();
        fake.write_failures.push_back(WriteFailure{.call = call});
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));

        bool address_order = fake.write_addresses.size() == call * 2;
        if (address_order) {
            for (std::size_t index = 0; index < call; ++index) {
                address_order = address_order &&
                    fake.write_addresses[index] ==
                        fake.base + kFixtureSites[index].rva &&
                    fake.write_addresses[call + index] ==
                        fake.base + kFixtureSites[call - index - 1].rva;
            }
        }
        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::SiteWrite &&
                result.error().site == kFixtureSites[call - 1].site &&
                result.error().rva == kFixtureSites[call - 1].rva &&
                result.error().memory_stage == GameBinaryMemoryStage::Copy &&
                result.error().win32_error == ERROR_NOACCESS &&
                result.error().rollback_attempted &&
                result.error().rollback_complete && address_order &&
                AllSitesEqual(fake, false),
            "each forward write failure rolls back current and prior sites");
    }
    return failures;
}

int TestCurrentFailedWriteIsIncludedInRollback() {
    int failures = 0;
    for (const auto stage : std::array{
             GameBinaryMemoryStage::FlushInstructionCache,
             GameBinaryMemoryStage::RestoreProtection,
         }) {
        auto fake = SupportedImage();
        fake.write_failures.push_back(WriteFailure{
            .call = 2,
            .after_copy = true,
            .stage = stage,
            .win32_error = ERROR_ACCESS_DENIED,
        });
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::SiteWrite &&
                result.error().site == kFixtureSites[1].site &&
                result.error().memory_stage == stage &&
                result.error().win32_error == ERROR_ACCESS_DENIED &&
                result.error().rollback_attempted &&
                result.error().rollback_complete &&
                fake.write_addresses.size() == 4 &&
                fake.write_addresses[2] ==
                    fake.base + kFixtureSites[1].rva &&
                fake.write_addresses[3] ==
                    fake.base + kFixtureSites[0].rva &&
                AllSitesEqual(fake, false),
            "post-copy failure includes current site in reverse rollback");
    }
    return failures;
}

int TestRollbackFailureIsReportedAndNeverSucceeds() {
    auto fake = SupportedImage();
    fake.write_failures = {
        WriteFailure{
            .call = 3,
            .after_copy = true,
            .stage = GameBinaryMemoryStage::FlushInstructionCache,
            .win32_error = ERROR_WRITE_FAULT,
        },
        WriteFailure{
            .call = 4,
            .after_copy = false,
            .stage = GameBinaryMemoryStage::Protect,
            .win32_error = ERROR_ACCESS_DENIED,
        },
    };
    const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));

    return Expect(
        !result && result.error().stage == GameBinaryPatchStage::SiteWrite &&
            result.error().site == kFixtureSites[2].site &&
            result.error().memory_stage ==
                GameBinaryMemoryStage::FlushInstructionCache &&
            result.error().win32_error == ERROR_WRITE_FAULT &&
            result.error().rollback_attempted &&
            !result.error().rollback_complete &&
            fake.write_addresses.size() == 6 &&
            fake.write_addresses[3] == fake.base + kFixtureSites[2].rva &&
            fake.write_addresses[4] == fake.base + kFixtureSites[1].rva &&
            fake.write_addresses[5] == fake.base + kFixtureSites[0].rva &&
            RangeEquals(fake, kFixtureSites[2].rva, kFixtureSites[2].patched) &&
            RangeEquals(fake, kFixtureSites[1].rva, kFixtureSites[1].clean) &&
            RangeEquals(fake, kFixtureSites[0].rva, kFixtureSites[0].clean),
        "rollback failure is reported after all remaining restores are attempted");
}

int TestSecondInstallIsAlreadyPatchedNoOp() {
    auto fake = SupportedImage();
    const auto first = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    const auto writes_after_first = fake.write_calls;
    const auto second = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    return Expect(
        first && second &&
            first->state == GameBinaryImageState::PatchedCleanImage &&
            second->state == GameBinaryImageState::AlreadyPatchedImage &&
            fake.write_calls == writes_after_first && AllSitesEqual(fake, true),
        "second installation recognizes the complete patched state");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestCleanImageWritesAllFourSites();
    failures += TestLegacyPatchedImageWritesNothing();
    failures += TestEveryIdentityFieldMismatchWritesNothing();
    failures += TestUnknownBytesAtEverySiteWriteNothing();
    failures += TestMixedImageWritesNothing();
    failures += TestHeaderAndSiteReadFailuresWriteNothing();
    failures += TestInvalidActionsAndAddressRangesTouchNothing();
    failures += TestEveryForwardWriteFailureRollsBackToClean();
    failures += TestCurrentFailedWriteIsIncludedInRollback();
    failures += TestRollbackFailureIsReportedAndNeverSucceeds();
    failures += TestSecondInstallIsAlreadyPatchedNoOp();
    return failures == 0 ? 0 : 1;
}
