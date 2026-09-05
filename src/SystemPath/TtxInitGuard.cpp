#include "SystemPath/TtxInitGuard.h"
#include "Platform/Win32/Utf.h"
#include "Platform/Win32/Win32Error.h"

#include "Diagnostics/FatalProcess.h"

#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace gc::system_path
{
    namespace
    {
        constexpr char kTtxFallbackLog[] =
            "TtxUpdateDownloader initialization failed; startup was terminated "
            "before the unsafe status call";
        constexpr wchar_t kTtxFallbackModal[] =
            L"TtxUpdateDownloader could not initialize. Check the loader log and "
            L"verify that the configured system path is writable.";
        constexpr wchar_t kTtxFatalTitle[] =
            L"TtxUpdateDownloader initialization error";

        std::wstring Utf8ToWide(std::string_view value)
        {
            auto converted = gc::platform::win32::Utf8ToWide(value);
            if (!converted)
            {
                if (converted.error().stage == gc::platform::win32::UtfStage::length)
                    throw std::length_error{"UTF-8 path is too long"};
                throw std::runtime_error{converted.error().stage == gc::platform::win32::UtfStage::writing
                    ? "UTF-8 path conversion failed" : "invalid UTF-8 path"};
            }
            return std::move(*converted);
        }

        std::string PathToUtf8(const std::filesystem::path& path)
        {
            const auto utf8 = path.u8string();
            return {
                reinterpret_cast<const char*>(utf8.data()),
                utf8.size(),
            };
        }

        std::wstring Win32ErrorText(DWORD error)
        {
            auto result = gc::platform::win32::FormatWin32Error(error);
            if (!result) return L"unknown error";
            // TTX's presentation contract also trims spaces after system CR/LF.
            while (!result->empty() &&
                   (result->back() == L' ' || result->back() == L'\r' || result->back() == L'\n'))
                result->pop_back();
            return std::move(*result);
        }
    } // namespace

    std::atomic<TtxInitGuard*> TtxInitGuard::active_{};

    TtxInitGuard::TtxInitGuard(RuntimeRoot root)
        : root_{std::move(root)}
    {
    }

    std::expected<void, hooking::HookError> TtxInitGuard::AddHook(hooking::HookPlan& plan) noexcept
    {
        active_.store(this, std::memory_order_release);
        return plan.AddInlineExport({"TtxInitGuard", "TtxUDLInit"},
            {kTtxModuleName, kTtxUdlInitExport}, &TtxInitGuard::Detour, &original_);
    }

    int __cdecl TtxInitGuard::Detour(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept
    {
        auto* active = active_.load(std::memory_order_acquire);
        if (active == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return 0;
        }
        return active->Invoke(
            priority,
            game_version,
            update_step,
            update_options);
    }

    int TtxInitGuard::Invoke(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept
    {
        if (original_ == nullptr) PublishFailure(ERROR_INVALID_FUNCTION);
        const int result = original_(priority, game_version, update_step, update_options);
        if (result != 0) return result;
        const DWORD error = GetLastError();
        PublishFailure(error);
    }

    [[noreturn]] void TtxInitGuard::PublishFailure(DWORD error) noexcept
    {
        try
        {
            const auto configured = Utf8ToWide(root_.configured_path);
            const auto resolved = root_.resolved_path.wstring();
            const auto error_text = Win32ErrorText(error);

            const auto log = std::format(
                "TtxUpdateDownloader initialization failed export={} configured={} "
                "resolved={} win32_error={}; startup terminated before "
                "TtxUDLGetStatus could use uninitialized state",
                kTtxUdlInitExport,
                root_.configured_path,
                PathToUtf8(root_.resolved_path),
                error);

            const auto modal = std::format(
                L"TtxUpdateDownloader could not initialize.\n\n"
                L"Configured system path: {}\n"
                L"Resolved system path: {}\n"
                L"Windows error {}: {}\n\n"
                L"Choose a writable custom path or use .\\system, then restart "
                L"the game.",
                configured,
                resolved,
                error,
                error_text);

            diagnostics::AbortProcess({
                log,
                modal,
                kTtxFatalTitle});
        }
        catch (...)
        {
        }

        diagnostics::AbortProcess({
            kTtxFallbackLog,
            kTtxFallbackModal,
            kTtxFatalTitle});
    }
} // namespace gc::system_path
