#include "Patches/GameCompatibility/GameBinaryPatchDiagnostics.h"

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace gc::game_compatibility {
namespace {

[[nodiscard]] bool IsUnsupportedVersion(
    GameBinaryPatchStage stage) noexcept {
    return stage == GameBinaryPatchStage::IdentityMismatch ||
        stage == GameBinaryPatchStage::UnknownBytes ||
        stage == GameBinaryPatchStage::MixedState;
}

[[nodiscard]] const wchar_t* IdentityFieldDisplayName(
    GameBinaryIdentityField field) noexcept {
    switch (field) {
    case GameBinaryIdentityField::None: return L"None";
    case GameBinaryIdentityField::DosMagic: return L"DOS magic";
    case GameBinaryIdentityField::NtSignature: return L"NT signature";
    case GameBinaryIdentityField::OptionalHeaderMagic:
        return L"Optional header magic";
    case GameBinaryIdentityField::Machine: return L"Machine";
    case GameBinaryIdentityField::Timestamp: return L"Timestamp";
    case GameBinaryIdentityField::PreferredImageBase:
        return L"Preferred image base";
    case GameBinaryIdentityField::EntryPointRva: return L"Entry point RVA";
    case GameBinaryIdentityField::SizeOfImage: return L"Size of image";
    case GameBinaryIdentityField::SizeOfHeaders: return L"Size of headers";
    case GameBinaryIdentityField::SectionCount: return L"Section count";
    }
    return L"Unknown";
}

[[nodiscard]] const char* RollbackLogName(
    const GameBinaryPatchError& error) noexcept {
    if (!error.rollback_attempted) {
        return "not_attempted";
    }
    return error.rollback_complete ? "complete" : "incomplete";
}

[[nodiscard]] const wchar_t* RollbackDisplayName(
    const GameBinaryPatchError& error) noexcept {
    if (!error.rollback_attempted) {
        return L"not attempted";
    }
    return error.rollback_complete ? L"complete" : L"incomplete";
}

[[nodiscard]] std::string HexIdentity(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << value;
    return stream.str();
}

[[nodiscard]] std::wstring WideHexIdentity(std::uint64_t value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::nouppercase << value;
    return stream.str();
}

[[nodiscard]] std::string HexRva(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase
           << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] std::wstring WideHexRva(std::uint32_t value) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::nouppercase
           << std::setw(8) << std::setfill(L'0') << value;
    return stream.str();
}

[[nodiscard]] std::string PatternHex(
    const GameBinaryBytePattern& pattern) {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setfill('0');
    for (const auto value : pattern.view()) {
        stream << std::setw(2)
               << std::to_integer<unsigned int>(value);
    }
    return stream.str();
}

void AppendSiteLog(
    std::ostringstream& log,
    const GameBinaryPatchError& error) {
    if (error.site == GameBinaryPatchSite::None) {
        return;
    }
    log << " site=" << GameBinaryPatchSiteName(error.site)
        << " rva=" << HexRva(error.rva);
}

void AppendPatternLog(
    std::ostringstream& log,
    const GameBinaryPatchError& error) {
    if (error.expected_clean.size != 0) {
        log << " expected_clean=" << PatternHex(error.expected_clean);
    }
    if (error.expected_patched.size != 0) {
        log << " expected_patched=" << PatternHex(error.expected_patched);
    }
    if (error.actual.size != 0) {
        log << " actual=" << PatternHex(error.actual);
    }
}

} // namespace

GameBinaryPatchFatalDiagnostic BuildGameBinaryPatchFatalDiagnostic(
    const GameBinaryPatchError& error) {
    const bool unsupported = IsUnsupportedVersion(error.stage);

    std::ostringstream log;
    log << "GameBinaryPatch: startup failed"
        << " stage=" << GameBinaryPatchStageName(error.stage);
    if (error.identity_field != GameBinaryIdentityField::None) {
        log << " identity_field="
            << GameBinaryIdentityFieldName(error.identity_field)
            << " expected_identity="
            << HexIdentity(error.expected_identity)
            << " actual_identity="
            << HexIdentity(error.actual_identity);
    }
    AppendSiteLog(log, error);
    AppendPatternLog(log, error);
    if (error.memory_stage != GameBinaryMemoryStage::None) {
        log << " memory_stage="
            << GameBinaryMemoryStageName(error.memory_stage);
    }
    if (error.win32_error != ERROR_SUCCESS) {
        log << " win32_error=" << error.win32_error;
    }
    log << " rollback=" << RollbackLogName(error);

    std::wostringstream modal;
    if (unsupported) {
        modal
            << L"This GCLoader build supports only the verified decrypted "
               L"Groove Coaster executable.\n\n";
        if (error.identity_field != GameBinaryIdentityField::None) {
            modal
                << L"Identity field: "
                << IdentityFieldDisplayName(error.identity_field) << L"\n"
                << L"Expected: "
                << WideHexIdentity(error.expected_identity) << L"\n"
                << L"Actual: "
                << WideHexIdentity(error.actual_identity) << L"\n";
        } else if (error.site != GameBinaryPatchSite::None) {
            modal
                << L"Patch site: "
                << GameBinaryPatchSiteName(error.site) << L"\n"
                << L"RVA: " << WideHexRva(error.rva) << L"\n";
        }
        modal
            << L"\nUse the supported game_decrypted.exe and remove other "
               L"executable modifications.\n\n"
            << L"See loader-log.txt for the exact comparison.";
    } else {
        modal
            << L"GCLoader could not apply the required in-memory game "
               L"patches.\n\n"
            << L"Stage: " << GameBinaryPatchStageName(error.stage) << L"\n";
        if (error.site != GameBinaryPatchSite::None) {
            modal
                << L"Patch site: "
                << GameBinaryPatchSiteName(error.site) << L"\n"
                << L"RVA: " << WideHexRva(error.rva) << L"\n";
        }
        if (error.memory_stage != GameBinaryMemoryStage::None) {
            modal
                << L"Memory stage: "
                << GameBinaryMemoryStageName(error.memory_stage) << L"\n";
        }
        if (error.win32_error != ERROR_SUCCESS) {
            modal << L"Windows error: " << error.win32_error << L"\n";
        }
        modal << L"Rollback: " << RollbackDisplayName(error)
              << L"\n\nCheck loader-log.txt and verify that security software "
                 L"is not blocking executable-memory changes.";
    }

    return {
        .log = log.str(),
        .modal = modal.str(),
        .title = unsupported
            ? L"GCLoader unsupported game version"
            : L"GCLoader game patch setup error",
        .exit_code = 26,
    };
}

} // namespace gc::game_compatibility
