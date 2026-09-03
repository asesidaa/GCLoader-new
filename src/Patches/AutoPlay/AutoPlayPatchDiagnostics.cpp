#include "AutoPlayPatchDiagnostics.h"

#include "SystemPath/StartupFatal.h"

#include <atomic>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace gc::auto_play
{
    namespace
    {
        constexpr DWORD kAutoPlayFailureExitCode{30};
        constexpr std::wstring_view kSetupTitle{
            L"GCLoader auto play setup failed"};
        constexpr std::string_view kSetupFallbackLog{
            "AutoPlayPatch: startup failure formatting failed"};
        constexpr std::wstring_view kSetupModal{
            L"GCLoader refused to continue because it could not guarantee "
            L"both save suppression and the visible marker. Check "
            L"loader-log.txt for the exact contract failure."};
        constexpr std::string_view kMarkerFailureLog{
            "AutoPlayPatch: mandatory marker rendering failed"};
        constexpr std::wstring_view kMarkerFailureModal{
            L"GCLoader cannot continue playable auto play without its "
            L"mandatory visible marker. Check loader-log.txt for details."};
        constexpr std::wstring_view kMarkerFailureTitle{
            L"GCLoader auto play marker failed"};

        std::atomic_bool g_setup_fatal_published{};
        std::atomic_bool g_marker_fatal_published{};

        struct AutoPlayFatalDiagnostic
        {
            std::string log;
            std::wstring modal;
            std::wstring title;
            DWORD exit_code{};
        };

        [[nodiscard]] std::string HexRva(const std::uint32_t rva)
        {
            std::ostringstream output;
            output << "0x" << std::uppercase << std::hex << rva;
            return output.str();
        }

        [[nodiscard]] std::string PatternHex(
            const AutoPlayBytePattern& pattern)
        {
            std::ostringstream output;
            output << std::uppercase << std::hex << std::setfill('0');
            bool first = true;
            for (const auto value : pattern.view())
            {
                if (!first)
                {
                    output << ' ';
                }
                first = false;
                output << std::setw(2)
                       << std::to_integer<unsigned int>(value);
            }
            return output.str();
        }

        void AppendPattern(
            std::ostringstream& output,
            const std::string_view name,
            const AutoPlayBytePattern& pattern)
        {
            if (pattern.size != 0)
            {
                output << ' ' << name << '=' << PatternHex(pattern);
            }
        }

        [[nodiscard]] AutoPlayFatalDiagnostic BuildDiagnostic(
            const AutoPlayPatchError& error)
        {
            std::ostringstream log;
            log << "AutoPlayPatch: startup failed"
                << " stage=" << AutoPlayPatchStageName(error.stage);
            if (error.site != AutoPlayContractSite::none)
            {
                log << " site=" << AutoPlayContractSiteName(error.site)
                    << " rva=" << HexRva(error.rva);
            }
            AppendPattern(log, "expected_clean", error.expected_clean);
            AppendPattern(log, "expected_patched", error.expected_patched);
            AppendPattern(log, "actual", error.actual);
            if (error.memory_stage !=
                game_compatibility::GameBinaryMemoryStage::None)
            {
                log << " memory_stage="
                    << game_compatibility::GameBinaryMemoryStageName(
                        error.memory_stage);
            }
            if (error.win32_error != ERROR_SUCCESS)
            {
                log << " win32_error=" << error.win32_error;
            }
            if (error.safetyhook_error != 0)
            {
                log << " safetyhook_error=" << error.safetyhook_error;
            }
            log << " rollback_attempted=" << error.rollback_attempted
                << " rollback_complete=" << error.rollback_complete;
            if (error.rollback_site != AutoPlayContractSite::none)
            {
                log << " rollback_site="
                    << AutoPlayContractSiteName(error.rollback_site);
            }
            if (error.rollback_memory_stage !=
                game_compatibility::GameBinaryMemoryStage::None)
            {
                log << " rollback_memory_stage="
                    << game_compatibility::GameBinaryMemoryStageName(
                        error.rollback_memory_stage);
            }
            if (error.rollback_win32_error != ERROR_SUCCESS)
            {
                log << " rollback_win32_error="
                    << error.rollback_win32_error;
            }

            return {
                .log = log.str(),
                .modal = std::wstring{kSetupModal},
                .title = std::wstring{kSetupTitle},
                .exit_code = kAutoPlayFailureExitCode,
            };
        }
    } // namespace

    const char* AutoPlayPatchStageName(
        const AutoPlayPatchStage stage) noexcept
    {
        switch (stage)
        {
        case AutoPlayPatchStage::none: return "none";
        case AutoPlayPatchStage::resolve_image_base:
            return "resolve_image_base";
        case AutoPlayPatchStage::address_range: return "address_range";
        case AutoPlayPatchStage::preflight_read: return "preflight_read";
        case AutoPlayPatchStage::byte_mismatch: return "byte_mismatch";
        case AutoPlayPatchStage::hook_install: return "hook_install";
        case AutoPlayPatchStage::direct_write: return "direct_write";
        }
        return "unknown";
    }

    const char* AutoPlayContractSiteName(
        const AutoPlayContractSite site) noexcept
    {
        switch (site)
        {
        case AutoPlayContractSite::none: return "none";
        case AutoPlayContractSite::do_not_save_card_data:
            return "do_not_save_card_data";
        case AutoPlayContractSite::complete_is_mute:
            return "complete_is_mute";
        case AutoPlayContractSite::native_auto_play:
            return "native_auto_play";
        case AutoPlayContractSite::marker_seam: return "marker_seam";
        case AutoPlayContractSite::native_debug_text:
            return "native_debug_text";
        }
        return "unknown";
    }

    void PublishAutoPlaySetupFatal(
        const AutoPlayPatchError& error) noexcept
    {
        try
        {
            const auto diagnostic = BuildDiagnostic(error);
            system_path::PublishStartupFatal(
                g_setup_fatal_published,
                diagnostic.log,
                diagnostic.modal,
                diagnostic.title,
                diagnostic.exit_code);
            return;
        }
        catch (...)
        {
        }
        PublishAutoPlaySetupFallbackFatal();
    }

    void PublishAutoPlaySetupFallbackFatal() noexcept
    {
        system_path::PublishStartupFatal(
            g_setup_fatal_published,
            kSetupFallbackLog,
            kSetupModal,
            kSetupTitle,
            kAutoPlayFailureExitCode);
    }

    void PublishAutoPlayMarkerRuntimeFatal() noexcept
    {
        system_path::PublishStartupFatal(
            g_marker_fatal_published,
            kMarkerFailureLog,
            kMarkerFailureModal,
            kMarkerFailureTitle,
            kAutoPlayFailureExitCode);
    }
} // namespace gc::auto_play
