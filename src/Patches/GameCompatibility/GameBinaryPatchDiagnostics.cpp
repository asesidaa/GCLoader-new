#include "Patches/GameCompatibility/GameBinaryPatchDiagnostics.h"

#include <cstddef>
#include <format>
#include <iterator>
#include <sstream>
#include <string>

namespace gc::game_compatibility
{
    namespace
    {
        [[nodiscard]] bool IsUnsupportedVersion(
            GameBinaryPatchStage stage) noexcept
        {
            return stage == GameBinaryPatchStage::UnknownBytes;
        }

        [[nodiscard]] std::string HexRva(std::uint32_t value)
        {
            return std::format("0x{:08x}", value);
        }

        [[nodiscard]] std::wstring WideHexRva(std::uint32_t value)
        {
            return std::format(L"0x{:08x}", value);
        }

        [[nodiscard]] std::string PatternHex(
            const GameBinaryBytePattern& pattern)
        {
            std::string text;
            for (const auto value : pattern.view())
            {
                std::format_to(
                    std::back_inserter(text),
                    "{:02x}",
                    std::to_integer<unsigned int>(value));
            }
            return text;
        }

        void AppendSiteLog(
            std::ostringstream& log,
            const GameBinaryPatchError& error)
        {
            if (error.site == GameBinaryPatchSite::None)
            {
                return;
            }
            log << " site=" << GameBinaryPatchSiteName(error.site)
                << " rva=" << HexRva(error.rva);
        }

        void AppendPatternLog(
            std::ostringstream& log,
            const GameBinaryPatchError& error)
        {
            if (error.expected_clean.size != 0)
            {
                log << " expected_clean=" << PatternHex(error.expected_clean);
            }
            if (error.expected_patched.size != 0)
            {
                log << " expected_patched=" << PatternHex(error.expected_patched);
            }
            if (error.actual.size != 0)
            {
                log << " actual=" << PatternHex(error.actual);
            }
        }
    } // namespace

    GameBinaryPatchFatalDiagnostic BuildGameBinaryPatchFatalDiagnostic(
        const GameBinaryPatchError& error)
    {
        const bool unsupported = IsUnsupportedVersion(error.stage);

        std::ostringstream log;
        log << "GameBinaryPatch: startup failed"
            << " stage=" << GameBinaryPatchStageName(error.stage);
        AppendSiteLog(log, error);
        AppendPatternLog(log, error);
        if (error.memory_stage != GameBinaryMemoryStage::None)
        {
            log << " memory_stage="
                << GameBinaryMemoryStageName(error.memory_stage);
        }
        if (error.win32_error != ERROR_SUCCESS)
        {
            log << " win32_error=" << error.win32_error;
        }

        std::wostringstream modal;
        if (unsupported)
        {
            modal
                << L"This executable does not contain supported bytes at a "
                L"required GCLoader patch site.\n\n";
            if (error.site != GameBinaryPatchSite::None)
            {
                modal
                    << L"Patch site: "
                    << GameBinaryPatchSiteName(error.site) << L"\n"
                    << L"RVA: " << WideHexRva(error.rva) << L"\n";
            }
            modal
                << L"\nEvery required patch site must be either clean or "
                L"already patched.\n\n"
                << L"See loader-log.txt for the exact byte comparison.";
        }
        else
        {
            modal
                << L"GCLoader could not apply the required in-memory game "
                L"patches.\n\n"
                << L"Stage: " << GameBinaryPatchStageName(error.stage) << L"\n";
            if (error.site != GameBinaryPatchSite::None)
            {
                modal
                    << L"Patch site: "
                    << GameBinaryPatchSiteName(error.site) << L"\n"
                    << L"RVA: " << WideHexRva(error.rva) << L"\n";
            }
            if (error.memory_stage != GameBinaryMemoryStage::None)
            {
                modal
                    << L"Memory stage: "
                    << GameBinaryMemoryStageName(error.memory_stage) << L"\n";
            }
            if (error.win32_error != ERROR_SUCCESS)
            {
                modal << L"Windows error: " << error.win32_error << L"\n";
            }
            modal
                << L"\nCheck loader-log.txt and verify that security software "
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
