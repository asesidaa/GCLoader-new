#include "Patches/GameCompatibility/GameBinaryPatch.h"
#include "Patches/GameCompatibility/GameBinaryPatchDiagnostics.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace gc::game_compatibility;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool Contains(std::string_view value, std::string_view expected) {
    return value.find(expected) != std::string_view::npos;
}

bool Contains(std::wstring_view value, std::wstring_view expected) {
    return value.find(expected) != std::wstring_view::npos;
}

GameBinaryBytePattern TestPattern(
    std::initializer_list<std::uint8_t> values) {
    assert(!values.empty());
    assert(values.size() <= kMaximumGameBinaryPatternBytes);
    GameBinaryBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::size_t index = 0;
    for (const auto value : values) {
        pattern.bytes[index++] = static_cast<std::byte>(value);
    }
    return pattern;
}

int TestUnknownBytesShowsSupportedSiteStatesAndEvidence() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::UnknownBytes,
            .site = GameBinaryPatchSite::RfidComPort,
            .rva = 0x002F7AC3U,
            .expected_clean = TestPattern({0x31}),
            .expected_patched = TestPattern({0x32}),
            .actual = TestPattern({0x33}),
        });

    return Expect(
        diagnostic.title == L"GCLoader unsupported game version" &&
            diagnostic.exit_code == 26 &&
            diagnostic.log.starts_with("GameBinaryPatch: startup failed") &&
            Contains(diagnostic.log, "stage=unknown_bytes") &&
            Contains(diagnostic.log, "site=rfid_com_port") &&
            Contains(diagnostic.log, "rva=0x002f7ac3") &&
            Contains(diagnostic.log, "expected_clean=31") &&
            Contains(diagnostic.log, "expected_patched=32") &&
            Contains(diagnostic.log, "actual=33") &&
            !Contains(diagnostic.log, "identity") &&
            !Contains(diagnostic.log, "rollback") &&
            Contains(diagnostic.modal, L"Patch site: rfid_com_port") &&
            Contains(diagnostic.modal, L"RVA: 0x002f7ac3") &&
            Contains(
                diagnostic.modal,
                L"Every required patch site must be either clean or already patched.") &&
            Contains(diagnostic.modal, L"loader-log.txt") &&
            !Contains(diagnostic.modal, L"game_decrypted.exe") &&
            !Contains(diagnostic.modal, L"Rollback"),
        "unknown bytes explain the supported per-site states and evidence");
}

int TestWriteFailureUsesSetupPromptWithoutRollbackLanguage() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::SiteWrite,
            .site = GameBinaryPatchSite::DongleFailure,
            .rva = 0x00102C7BU,
            .memory_stage = GameBinaryMemoryStage::RestoreProtection,
            .win32_error = ERROR_ACCESS_DENIED,
        });

    return Expect(
        diagnostic.title == L"GCLoader game patch setup error" &&
            diagnostic.exit_code == 26 &&
            Contains(diagnostic.log, "stage=site_write") &&
            Contains(diagnostic.log, "site=dongle_failure") &&
            Contains(diagnostic.log, "rva=0x00102c7b") &&
            Contains(diagnostic.log, "memory_stage=restore_protection") &&
            Contains(diagnostic.log, "win32_error=5") &&
            !Contains(diagnostic.log, "rollback") &&
            Contains(
                diagnostic.modal,
                L"could not apply the required in-memory game patches") &&
            Contains(diagnostic.modal, L"Windows error: 5") &&
            Contains(diagnostic.modal, L"loader-log.txt") &&
            !Contains(diagnostic.modal, L"Rollback"),
        "write failure reports the exact setup error without rollback language");
}

int TestReadFailureUsesSetupPromptAndWindowsError() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::SiteRead,
            .site = GameBinaryPatchSite::NativeMouseEvents,
            .rva = 0x000B0896U,
            .memory_stage = GameBinaryMemoryStage::Read,
            .win32_error = ERROR_NOACCESS,
        });

    return Expect(
        diagnostic.title == L"GCLoader game patch setup error" &&
            diagnostic.exit_code == 26 &&
            Contains(diagnostic.log, "stage=site_read") &&
            Contains(diagnostic.log, "site=native_mouse_events") &&
            Contains(diagnostic.log, "rva=0x000b0896") &&
            Contains(diagnostic.log, "memory_stage=read") &&
            Contains(
                diagnostic.log,
                "win32_error=" + std::to_string(ERROR_NOACCESS)) &&
            !Contains(diagnostic.log, "rollback") &&
            Contains(diagnostic.modal, L"Stage: site_read") &&
            Contains(diagnostic.modal, L"Memory stage: read") &&
            !Contains(diagnostic.modal, L"Rollback"),
        "read failure reports the memory error without rollback language");
}

int TestAddressRangeFailureIsASetupError() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::AddressRange,
            .site = GameBinaryPatchSite::DongleSecurityTransmit,
            .rva = 0x00103EE6U,
        });

    return Expect(
        diagnostic.title == L"GCLoader game patch setup error" &&
            diagnostic.exit_code == 26 &&
            Contains(diagnostic.log, "stage=address_range") &&
            Contains(diagnostic.log, "site=dongle_security_transmit") &&
            Contains(diagnostic.log, "rva=0x00103ee6") &&
            !Contains(diagnostic.log, "rollback") &&
            Contains(diagnostic.modal, L"Stage: address_range") &&
            Contains(
                diagnostic.modal,
                L"Patch site: dongle_security_transmit") &&
            Contains(diagnostic.modal, L"RVA: 0x00103ee6") &&
            !Contains(diagnostic.modal, L"Rollback"),
        "address overflow remains a loader setup error");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestUnknownBytesShowsSupportedSiteStatesAndEvidence();
    failures += TestWriteFailureUsesSetupPromptWithoutRollbackLanguage();
    failures += TestReadFailureUsesSetupPromptAndWindowsError();
    failures += TestAddressRangeFailureIsASetupError();
    return failures == 0 ? 0 : 1;
}
