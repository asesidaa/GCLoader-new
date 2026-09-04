#include "Patches/SongUnlock/SongUnlockPatch.h"

#include "Diagnostics/FatalProcess.h"
#include "Patches/RuntimeImage/RuntimeImage.h"
#include "plog/Log.h"

#include <exception>
#include <format>
#include <iterator>
#include <string>

namespace gc::song_unlock {
namespace {

constexpr runtime_image::BytePatch kAvailabilityBranch{
    {"SongUnlock", "availability_branch", 0x00257854U},
    runtime_image::PatternOf<0x0F, 0x85, 0x1D, 0x02, 0x00, 0x00>(),
    runtime_image::PatternOf<0xE9, 0x1E, 0x02, 0x00, 0x00, 0x90>(),
};

std::string FormatBytes(const runtime_image::BytePattern& pattern) {
    std::string text;
    for (auto byte : pattern.view()) {
        if (!text.empty()) {
            text.push_back(' ');
        }
        std::format_to(std::back_inserter(text), "{:02X}", std::to_integer<unsigned int>(byte));
    }
    return text;
}

std::string MemoryFailure(const char* stage, const runtime_image::RuntimeImageError& error) {
    return std::format(
        "SongUnlockPatch: startup failed stage={} rva=0x{:08X} address=0x{:08X} "
        "memory_stage={} win32_error={} memory_changed={} restore_attempted={} "
        "restore_succeeded={} expected={} observed={}",
        stage, error.identity.rva, error.address, runtime_image::MemoryStageName(error.stage),
        error.win32_error, error.memory_changed, error.restore_attempted, error.restore_succeeded,
        FormatBytes(error.expected), FormatBytes(error.observed));
}

[[noreturn]] void AbortWrite(const runtime_image::RuntimeImageError& error) noexcept {
    try {
        diagnostics::AbortProcess({MemoryFailure("site_write", error),
            L"GCLoader could not apply the song availability patch. Check loader-log.txt for details.",
            L"GCLoader song unlock setup error"});
    } catch (...) {
        diagnostics::AbortProcess({});
    }
}

} // namespace

bool SongUnlockPatchInit(bool enabled) noexcept {
    try {
        if (!enabled) {
            PLOG_INFO << "SongUnlockPatch: state=disabled";
            return true;
        }
        const auto image = runtime_image::RuntimeImage::MainModule();
        if (!image) {
            PLOG_ERROR << MemoryFailure("resolve_module", image.error());
            return false;
        }
        const auto state = image->Inspect(kAvailabilityBranch);
        if (!state) {
            PLOG_ERROR << MemoryFailure("site_read", state.error());
            return false;
        }
        if (*state == runtime_image::BytePatchState::installed) {
            PLOG_INFO << "SongUnlockPatch: state=already_patched rva=0x"
                      << std::hex << std::uppercase << kAvailabilityBranch.identity.rva << std::dec;
            return true;
        }
        if (*state == runtime_image::BytePatchState::mismatch) {
            const auto actual = image->Read(kAvailabilityBranch.identity, kAvailabilityBranch.original.size);
            if (!actual) {
                PLOG_ERROR << MemoryFailure("site_read", actual.error());
                return false;
            }
            PLOG_ERROR << "SongUnlockPatch: startup failed stage=unknown_bytes rva=0x"
                       << std::hex << std::uppercase << kAvailabilityBranch.identity.rva << std::dec
                       << " expected_clean=" << FormatBytes(kAvailabilityBranch.original)
                       << " expected_patched=" << FormatBytes(kAvailabilityBranch.replacement)
                       << " actual=" << FormatBytes(*actual);
            return false;
        }
        const auto written = image->Write(kAvailabilityBranch.identity,
            kAvailabilityBranch.replacement, kAvailabilityBranch.memory_kind);
        if (!written) {
            AbortWrite(written.error());
        }
        PLOG_INFO << "SongUnlockPatch: state=patched rva=0x"
                  << std::hex << std::uppercase << kAvailabilityBranch.identity.rva << std::dec;
        return true;
    } catch (const std::exception& error) {
        try {
            PLOG_ERROR << "SongUnlockPatch: startup failed stage=exception message=" << error.what();
        } catch (...) {
        }
        return false;
    } catch (...) {
        try {
            PLOG_ERROR << "SongUnlockPatch: startup failed stage=exception message=unknown";
        } catch (...) {
        }
        return false;
    }
}

} // namespace gc::song_unlock
