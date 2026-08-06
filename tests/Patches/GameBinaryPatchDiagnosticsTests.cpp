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

int TestIdentityMismatchUsesUnsupportedVersionPrompt() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::IdentityMismatch,
            .identity_field = GameBinaryIdentityField::Timestamp,
            .expected_identity = 0x5FA90825U,
            .actual_identity = 0x5FA90826U,
        });

    return Expect(
        diagnostic.title == L"GCLoader unsupported game version" &&
            diagnostic.exit_code == 26 &&
            diagnostic.log.starts_with("GameBinaryPatch: startup failed") &&
            Contains(diagnostic.log, "stage=identity_mismatch") &&
            Contains(diagnostic.log, "identity_field=timestamp") &&
            Contains(diagnostic.log, "expected_identity=0x5fa90825") &&
            Contains(diagnostic.log, "actual_identity=0x5fa90826") &&
            Contains(
                diagnostic.modal,
                L"supports only the verified decrypted Groove Coaster executable") &&
            Contains(diagnostic.modal, L"Timestamp") &&
            Contains(diagnostic.modal, L"game_decrypted.exe") &&
            Contains(diagnostic.modal, L"loader-log.txt"),
        "identity mismatch produces the unsupported-version diagnostic");
}

int TestUnknownBytesShowsSiteRvaAndAllPatterns() {
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
            Contains(diagnostic.log, "stage=unknown_bytes") &&
            Contains(diagnostic.log, "site=rfid_com_port") &&
            Contains(diagnostic.log, "rva=0x002f7ac3") &&
            Contains(diagnostic.log, "expected_clean=31") &&
            Contains(diagnostic.log, "expected_patched=32") &&
            Contains(diagnostic.log, "actual=33") &&
            Contains(diagnostic.modal, L"Patch site: rfid_com_port") &&
            Contains(diagnostic.modal, L"RVA: 0x002f7ac3") &&
            Contains(diagnostic.modal, L"game_decrypted.exe") &&
            Contains(diagnostic.modal, L"loader-log.txt"),
        "unknown bytes expose the complete unsupported-site evidence");
}

int TestMixedStateUsesUnsupportedVersionPrompt() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::MixedState,
            .site = GameBinaryPatchSite::DongleSecurityTransmit,
            .rva = 0x00103EE6U,
            .expected_clean = TestPattern({0xE8, 0x45, 0xF6, 0xFF, 0xFF}),
            .expected_patched = TestPattern({0x90, 0x90, 0x90, 0x90, 0x90}),
            .actual = TestPattern({0x90, 0x90, 0x90, 0x90, 0x90}),
        });

    return Expect(
        diagnostic.title == L"GCLoader unsupported game version" &&
            Contains(diagnostic.log, "stage=mixed_state") &&
            Contains(diagnostic.log, "site=dongle_security_transmit") &&
            Contains(diagnostic.log, "rva=0x00103ee6") &&
            Contains(
                diagnostic.modal,
                L"remove other executable modifications"),
        "mixed clean and patched sites use the unsupported-version prompt");
}

int TestWriteFailureUsesSetupPromptAndRollbackEvidence() {
    const auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(
        GameBinaryPatchError{
            .stage = GameBinaryPatchStage::SiteWrite,
            .site = GameBinaryPatchSite::DongleFailure,
            .rva = 0x00102C7BU,
            .memory_stage = GameBinaryMemoryStage::RestoreProtection,
            .win32_error = ERROR_ACCESS_DENIED,
            .rollback_attempted = true,
            .rollback_complete = false,
        });

    return Expect(
        diagnostic.title == L"GCLoader game patch setup error" &&
            diagnostic.exit_code == 26 &&
            Contains(diagnostic.log, "stage=site_write") &&
            Contains(diagnostic.log, "site=dongle_failure") &&
            Contains(diagnostic.log, "rva=0x00102c7b") &&
            Contains(diagnostic.log, "memory_stage=restore_protection") &&
            Contains(diagnostic.log, "win32_error=5") &&
            Contains(diagnostic.log, "rollback=incomplete") &&
            Contains(
                diagnostic.modal,
                L"could not apply the required in-memory game patches") &&
            Contains(diagnostic.modal, L"Windows error: 5") &&
            Contains(diagnostic.modal, L"Rollback: incomplete") &&
            Contains(diagnostic.modal, L"loader-log.txt"),
        "write failure produces setup and incomplete-rollback evidence");
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
            Contains(diagnostic.log, "rollback=not_attempted") &&
            Contains(diagnostic.modal, L"Stage: site_read") &&
            Contains(diagnostic.modal, L"Rollback: not attempted"),
        "read failure produces setup and Windows-error evidence");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestIdentityMismatchUsesUnsupportedVersionPrompt();
    failures += TestUnknownBytesShowsSiteRvaAndAllPatterns();
    failures += TestMixedStateUsesUnsupportedVersionPrompt();
    failures += TestWriteFailureUsesSetupPromptAndRollbackEvidence();
    failures += TestReadFailureUsesSetupPromptAndWindowsError();
    return failures == 0 ? 0 : 1;
}
