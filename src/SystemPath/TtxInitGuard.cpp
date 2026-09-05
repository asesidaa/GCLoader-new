#include "SystemPath/TtxInitGuard.h"

#include "SystemPath/StartupFatal.h"

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
        constexpr DWORD kTtxRuntimeFailureExitCode = 22;
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
            if (value.empty())
            {
                return {};
            }
            if (value.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
            {
                throw std::length_error{"UTF-8 path is too long"};
            }
            const int size = static_cast<int>(value.size());
            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                size,
                nullptr,
                0);
            if (required <= 0)
            {
                throw std::runtime_error{"invalid UTF-8 path"};
            }
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                size,
                result.data(),
                required) != required)
            {
                throw std::runtime_error{"UTF-8 path conversion failed"};
            }
            return result;
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
            std::wstring result(512, L'\0');
            const DWORD size = FormatMessageW(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                0,
                result.data(),
                static_cast<DWORD>(result.size()),
                nullptr);
            if (size == 0)
            {
                return L"unknown error";
            }
            result.resize(size);
            while (!result.empty() &&
                (result.back() == L'\r' || result.back() == L'\n' ||
                    result.back() == L' '))
            {
                result.pop_back();
            }
            return result;
        }
    } // namespace

    std::atomic<TtxInitGuard*> TtxInitGuard::active_{};

    int InvokeTtxUdlInitGuard(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options,
        const RuntimeRoot& root,
        const TtxGuardRuntimeActions& actions) noexcept
    {
        if (actions.call_original == nullptr)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }

        const int result = actions.call_original(
            actions.context,
            priority,
            game_version,
            update_step,
            update_options);
        if (result != 0)
        {
            return result;
        }

        const DWORD error = actions.get_last_error == nullptr
                                ? ERROR_INVALID_FUNCTION
                                : actions.get_last_error(actions.context);
        if (actions.publish_failure != nullptr)
        {
            actions.publish_failure(actions.context, error, root);
        }
        return result;
    }

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
        return InvokeTtxUdlInitGuard(
            priority,
            game_version,
            update_step,
            update_options,
            root_,
            TtxGuardRuntimeActions{
                .context = this,
                .call_original = +[](
                void* context,
                unsigned int original_priority,
                unsigned int original_game_version,
                unsigned int original_update_step,
                unsigned int original_update_options) noexcept
                {
                    auto& self = *static_cast<TtxInitGuard*>(context);
                    return self.original_(
                        original_priority,
                        original_game_version,
                        original_update_step,
                        original_update_options);
                },
                .get_last_error = +[](void*) noexcept
                {
                    return GetLastError();
                },
                .publish_failure = +[](
                void* context,
                DWORD error,
                const RuntimeRoot&) noexcept
                {
                    static_cast<TtxInitGuard*>(context)->PublishFailure(error);
                },
            });
    }

    void TtxInitGuard::PublishFailure(DWORD error) noexcept
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

            PublishStartupFatal(
                failure_published_,
                log,
                modal,
                kTtxFatalTitle,
                kTtxRuntimeFailureExitCode);
            return;
        }
        catch (...)
        {
        }

        PublishStartupFatal(
            failure_published_,
            kTtxFallbackLog,
            kTtxFallbackModal,
            kTtxFatalTitle,
            kTtxRuntimeFailureExitCode);
    }
} // namespace gc::system_path
