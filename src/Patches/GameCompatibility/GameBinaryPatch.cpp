#include "Patches/GameCompatibility/GameBinaryPatch.h"

#include "Diagnostics/FatalProcess.h"
#include "Patches/GameCompatibility/GameBinaryPatchDiagnostics.h"

#include <array>
#include <utility>

namespace gc::game_compatibility {
namespace {

using runtime_image::BytePatch;
using runtime_image::BytePatchState;
using runtime_image::PatternOf;

// Order is the original compatibility install order. Native evidence:
// .codex-tmp/loader_cleanup_foundation_native.py, 2026-09-05 bounded IDA batch.
constexpr std::array<BytePatch, kGameBinaryPatchSiteCount> kContracts{{
    {{"GameCompatibility", "native_mouse_events", 0x000B0896U},
     PatternOf<0x75, 0x02>(), PatternOf<0x90, 0x90>()},
    {{"GameCompatibility", "dongle_failure", 0x00102C7BU},
     PatternOf<0x75, 0x3B>(), PatternOf<0xEB, 0x3B>()},
    {{"GameCompatibility", "dongle_security_transmit", 0x00103EE6U},
     PatternOf<0xE8, 0x45, 0xF6, 0xFF, 0xFF>(), PatternOf<0x90, 0x90, 0x90, 0x90, 0x90>()},
    {{"GameCompatibility", "rfid_com_port", 0x002F7AC3U},
     PatternOf<0x31>(), PatternOf<0x32>(), runtime_image::MemoryKind::data},
}};
constexpr std::array kSites{
    GameBinaryPatchSite::NativeMouseEvents,
    GameBinaryPatchSite::DongleFailure,
    GameBinaryPatchSite::DongleSecurityTransmit,
    GameBinaryPatchSite::RfidComPort,
};

GameBinaryPatchError SiteError(GameBinaryPatchStage stage, std::size_t index) noexcept {
    const auto& contract = kContracts[index];
    return {.stage = stage, .site = kSites[index], .rva = contract.identity.rva,
            .expected_clean = contract.original, .expected_patched = contract.replacement};
}

[[noreturn]] void AbortWrite(const GameBinaryPatchError& error) noexcept {
    try {
        auto diagnostic = BuildGameBinaryPatchFatalDiagnostic(error);
        diagnostics::AbortProcess({std::move(diagnostic.log),
            std::move(diagnostic.modal), std::move(diagnostic.title)});
    } catch (...) {
        diagnostics::AbortProcess({});
    }
}

} // namespace

std::expected<GameBinaryPatchResult, GameBinaryPatchError>
InstallGameBinaryPatch(const runtime_image::RuntimeImage& image) noexcept {
    std::array<BytePatchState, kGameBinaryPatchSiteCount> states{};
    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto state = image.Inspect(kContracts[index]);
        if (!state) {
            auto error = SiteError(GameBinaryPatchStage::SiteRead, index);
            error.memory = state.error();
            return std::unexpected(error);
        }
        states[index] = *state;
        if (*state == BytePatchState::mismatch) {
            auto error = SiteError(GameBinaryPatchStage::UnknownBytes, index);
            const auto actual = image.Read(kContracts[index].identity, kContracts[index].original.size);
            if (actual) {
                error.actual = *actual;
            } else {
                error.memory = actual.error();
            }
            return std::unexpected(error);
        }
    }

    for (std::size_t index = 1; index < states.size(); ++index) {
        if (states[index] != states[0]) {
            auto error = SiteError(GameBinaryPatchStage::MixedState, index);
            error.actual = states[index] == BytePatchState::original
                ? kContracts[index].original : kContracts[index].replacement;
            return std::unexpected(error);
        }
    }
    if (states[0] == BytePatchState::installed) {
        return GameBinaryPatchResult{GameBinaryImageState::AlreadyPatchedImage, kContracts.size()};
    }

    for (std::size_t index = 0; index < kContracts.size(); ++index) {
        const auto& contract = kContracts[index];
        const auto written = image.Write(contract.identity, contract.replacement, contract.memory_kind);
        if (!written) {
            auto error = SiteError(GameBinaryPatchStage::SiteWrite, index);
            error.memory = written.error();
            error.actual = written.error().observed;
            AbortWrite(error);
        }
    }
    return GameBinaryPatchResult{GameBinaryImageState::PatchedImage, kContracts.size()};
}

std::expected<GameBinaryPatchResult, GameBinaryPatchError> GameBinaryPatchInit() noexcept {
    const auto image = runtime_image::RuntimeImage::MainModule();
    if (!image) {
        return std::unexpected(GameBinaryPatchError{
            .stage = GameBinaryPatchStage::ResolveModule, .memory = image.error()});
    }
    return InstallGameBinaryPatch(*image);
}

const char* GameBinaryPatchStageName(GameBinaryPatchStage stage) noexcept {
    switch (stage) {
    case GameBinaryPatchStage::None: return "none";
    case GameBinaryPatchStage::ResolveModule: return "resolve_module";
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
    case GameBinaryPatchSite::NativeMouseEvents: return "native_mouse_events";
    case GameBinaryPatchSite::DongleFailure: return "dongle_failure";
    case GameBinaryPatchSite::DongleSecurityTransmit: return "dongle_security_transmit";
    case GameBinaryPatchSite::RfidComPort: return "rfid_com_port";
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
