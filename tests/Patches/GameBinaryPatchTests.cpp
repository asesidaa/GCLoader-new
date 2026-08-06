#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace gc::game_compatibility;

constexpr std::uintptr_t kFakeBase = 0x10000000U;
constexpr std::size_t kSyntheticImageSize = 0x00433000U;
constexpr std::uint8_t kAllPatchedMask =
    (1U << kGameBinaryPatchSiteCount) - 1U;

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
        std::vector<std::byte>(kSyntheticImageSize);
    std::optional<std::uintptr_t> failed_read_address{};
    std::vector<WriteFailure> write_failures{};
    std::vector<std::uintptr_t> read_addresses{};
    std::vector<std::uintptr_t> write_addresses{};
    std::size_t write_calls{};

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

FakeImage ApplicableImage(std::uint8_t patched_mask) {
    FakeImage fake{};
    for (std::size_t index = 0; index < kFixtureSites.size(); ++index) {
        const auto& fixture = kFixtureSites[index];
        const bool patched =
            (patched_mask & (1U << index)) != 0;
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

bool AllSitesPatched(const FakeImage& fake) {
    return std::ranges::all_of(
        kFixtureSites,
        [&](const FixtureSite& fixture) {
            return RangeEquals(fake, fixture.rva, fixture.patched);
        });
}

bool HasPatchSiteReadOrder(const FakeImage& fake) {
    if (fake.read_addresses.size() != kFixtureSites.size()) {
        return false;
    }
    for (std::size_t index = 0; index < kFixtureSites.size(); ++index) {
        if (fake.read_addresses[index] !=
            fake.base + kFixtureSites[index].rva) {
            return false;
        }
    }
    return true;
}

int TestEveryCleanPatchedCombinationCompletes() {
    int failures = 0;
    for (unsigned int mask = 0; mask <= kAllPatchedMask; ++mask) {
        auto fake = ApplicableImage(static_cast<std::uint8_t>(mask));
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        const auto expected_writes =
            kGameBinaryPatchSiteCount - std::popcount(mask);
        const auto expected_state = mask == kAllPatchedMask
            ? GameBinaryImageState::AlreadyPatchedImage
            : GameBinaryImageState::PatchedImage;

        std::vector<std::uintptr_t> expected_write_addresses;
        for (std::size_t index = 0; index < kFixtureSites.size(); ++index) {
            if ((mask & (1U << index)) == 0) {
                expected_write_addresses.push_back(
                    fake.base + kFixtureSites[index].rva);
            }
        }

        failures += Expect(
            result && result->state == expected_state &&
                result->site_count == kFixtureSites.size() &&
                fake.write_calls == expected_writes &&
                fake.write_addresses == expected_write_addresses &&
                HasPatchSiteReadOrder(fake) && AllSitesPatched(fake),
            "every clean/already-patched combination completes selectively");
    }
    return failures;
}

int TestUnknownBytesAtEverySiteWriteNothing() {
    int failures = 0;
    for (const auto& fixture : kFixtureSites) {
        auto fake = ApplicableImage(0x05U);
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
                fake.write_calls == 0 && HasPatchSiteReadOrder(fake),
            "unknown site bytes reject the image after complete preflight");
    }
    return failures;
}

int TestEverySiteReadFailureWritesNothing() {
    int failures = 0;
    for (const auto& fixture : kFixtureSites) {
        auto fake = ApplicableImage(0x05U);
        fake.failed_read_address = fake.base + fixture.rva;
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));
        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::SiteRead &&
                result.error().site == fixture.site &&
                result.error().rva == fixture.rva &&
                result.error().memory_stage == GameBinaryMemoryStage::Read &&
                result.error().win32_error == ERROR_NOACCESS &&
                fake.write_calls == 0,
            "site read failure aborts preflight without writes");
    }
    return failures;
}

int TestInvalidActionsAndOverflowTouchNothing() {
    int failures = 0;

    auto zero_base = ApplicableImage(0);
    const auto zero_result =
        InstallGameBinaryPatch(0, FakeActions(zero_base));
    failures += Expect(
        !zero_result &&
            zero_result.error().stage == GameBinaryPatchStage::InvalidActions &&
            zero_base.read_addresses.empty() && zero_base.write_calls == 0,
        "zero image base is rejected without memory access");

    auto no_read = ApplicableImage(0);
    auto missing_read = FakeActions(no_read);
    missing_read.read = nullptr;
    const auto no_read_result =
        InstallGameBinaryPatch(no_read.base, missing_read);
    failures += Expect(
        !no_read_result &&
            no_read_result.error().stage ==
                GameBinaryPatchStage::InvalidActions &&
            no_read.read_addresses.empty() && no_read.write_calls == 0,
        "missing read action is rejected without memory access");

    auto no_write = ApplicableImage(0);
    auto missing_write = FakeActions(no_write);
    missing_write.write = nullptr;
    const auto no_write_result =
        InstallGameBinaryPatch(no_write.base, missing_write);
    failures += Expect(
        !no_write_result &&
            no_write_result.error().stage ==
                GameBinaryPatchStage::InvalidActions &&
            no_write.read_addresses.empty() && no_write.write_calls == 0,
        "missing write action is rejected without memory access");

    auto overflowing = ApplicableImage(0);
    overflowing.base = std::numeric_limits<std::uintptr_t>::max() - 0x100U;
    const auto overflow_result =
        InstallGameBinaryPatch(overflowing.base, FakeActions(overflowing));
    failures += Expect(
        !overflow_result &&
            overflow_result.error().stage ==
                GameBinaryPatchStage::AddressRange &&
            overflowing.read_addresses.empty() &&
            overflowing.write_calls == 0,
        "overflowing patch address is rejected without memory access");

    return failures;
}

int TestEveryRequiredWriteFailureStopsImmediately() {
    int failures = 0;
    for (std::size_t call = 1; call <= kFixtureSites.size(); ++call) {
        auto fake = ApplicableImage(0);
        fake.write_failures.push_back(WriteFailure{.call = call});
        const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));

        bool state_is_expected = true;
        for (std::size_t index = 0; index < kFixtureSites.size(); ++index) {
            const auto& expected = index < call - 1
                ? kFixtureSites[index].patched
                : kFixtureSites[index].clean;
            state_is_expected = state_is_expected &&
                RangeEquals(fake, kFixtureSites[index].rva, expected);
        }

        bool address_order = fake.write_addresses.size() == call;
        for (std::size_t index = 0;
             address_order && index < call;
             ++index) {
            address_order = fake.write_addresses[index] ==
                fake.base + kFixtureSites[index].rva;
        }

        failures += Expect(
            !result && result.error().stage == GameBinaryPatchStage::SiteWrite &&
                result.error().site == kFixtureSites[call - 1].site &&
                result.error().rva == kFixtureSites[call - 1].rva &&
                result.error().memory_stage == GameBinaryMemoryStage::Copy &&
                result.error().win32_error == ERROR_NOACCESS &&
                fake.write_calls == call && address_order && state_is_expected,
            "write failure stops immediately without later writes or rollback");
    }
    return failures;
}

int TestPostCopyFailureStopsWithoutRollback() {
    int failures = 0;
    for (const auto stage : std::array{
             GameBinaryMemoryStage::FlushInstructionCache,
             GameBinaryMemoryStage::RestoreProtection,
         }) {
        auto fake = ApplicableImage(0);
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
                fake.write_calls == 2 && fake.write_addresses.size() == 2 &&
                RangeEquals(fake, kFixtureSites[0].rva, kFixtureSites[0].patched) &&
                RangeEquals(fake, kFixtureSites[1].rva, kFixtureSites[1].patched) &&
                RangeEquals(fake, kFixtureSites[2].rva, kFixtureSites[2].clean) &&
                RangeEquals(fake, kFixtureSites[3].rva, kFixtureSites[3].clean),
            "post-copy failure preserves process-local writes and stops");
    }
    return failures;
}

int TestPartialImageFailurePreservesPriorWrites() {
    auto fake = ApplicableImage(0x05U);
    fake.write_failures.push_back(WriteFailure{.call = 2});
    const auto result = InstallGameBinaryPatch(fake.base, FakeActions(fake));

    return Expect(
        !result && result.error().stage == GameBinaryPatchStage::SiteWrite &&
            result.error().site == kFixtureSites[3].site &&
            fake.write_calls == 2 && fake.write_addresses.size() == 2 &&
            fake.write_addresses[0] == fake.base + kFixtureSites[1].rva &&
            fake.write_addresses[1] == fake.base + kFixtureSites[3].rva &&
            RangeEquals(fake, kFixtureSites[0].rva, kFixtureSites[0].patched) &&
            RangeEquals(fake, kFixtureSites[1].rva, kFixtureSites[1].patched) &&
            RangeEquals(fake, kFixtureSites[2].rva, kFixtureSites[2].patched) &&
            RangeEquals(fake, kFixtureSites[3].rva, kFixtureSites[3].clean),
        "partial image failure keeps prior writes and attempts no rollback");
}

int TestSecondInstallIsAlreadyPatchedNoOp() {
    auto fake = ApplicableImage(0x05U);
    const auto first = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    const auto writes_after_first = fake.write_calls;
    const auto second = InstallGameBinaryPatch(fake.base, FakeActions(fake));
    return Expect(
        first && second &&
            first->state == GameBinaryImageState::PatchedImage &&
            second->state == GameBinaryImageState::AlreadyPatchedImage &&
            fake.write_calls == writes_after_first && AllSitesPatched(fake),
        "second installation recognizes the complete patched state");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestEveryCleanPatchedCombinationCompletes();
    failures += TestUnknownBytesAtEverySiteWriteNothing();
    failures += TestEverySiteReadFailureWritesNothing();
    failures += TestInvalidActionsAndOverflowTouchNothing();
    failures += TestEveryRequiredWriteFailureStopsImmediately();
    failures += TestPostCopyFailureStopsWithoutRollback();
    failures += TestPartialImageFailurePreservesPriorWrites();
    failures += TestSecondInstallIsAlreadyPatchedNoOp();
    return failures == 0 ? 0 : 1;
}
