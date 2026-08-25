#include "SystemPath/TtxInitGuard.h"

#include "SystemPath/StartupFatal.h"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace gc::system_path {
namespace {

constexpr DWORD kTtxRuntimeFailureExitCode = 22;
constexpr char kTtxFallbackLog[] =
    "TtxUpdateDownloader initialization failed; startup was terminated "
    "before the unsafe status call";
constexpr wchar_t kTtxFallbackModal[] =
    L"TtxUpdateDownloader could not initialize. Check the loader log and "
    L"verify that the configured system path is writable.";
constexpr wchar_t kTtxFatalTitle[] =
    L"TtxUpdateDownloader initialization error";

std::uint32_t SafetyHookErrorCode(
    const safetyhook::InlineHook::Error& error) noexcept
{
    return static_cast<std::uint32_t>(error.type);
}

std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
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
    if (required <= 0) {
        throw std::runtime_error{"invalid UTF-8 path"};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            size,
            result.data(),
            required) != required) {
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
    if (size == 0) {
        return L"unknown error";
    }
    result.resize(size);
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' ||
            result.back() == L' ')) {
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
    if (actions.call_original == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    const int result = actions.call_original(
        actions.context,
        priority,
        game_version,
        update_step,
        update_options);
    if (result != 0) {
        return result;
    }

    const DWORD error = actions.get_last_error == nullptr
        ? ERROR_INVALID_FUNCTION
        : actions.get_last_error(actions.context);
    if (actions.publish_failure != nullptr) {
        actions.publish_failure(actions.context, error, root);
    }
    return result;
}

std::expected<void, TtxGuardInstallError> InstallTtxInitGuard(
    const TtxGuardInstallActions& actions) noexcept
{
    if (actions.detour == nullptr || actions.get_module == nullptr ||
        actions.get_export == nullptr || actions.get_last_error == nullptr ||
        actions.create_disabled == nullptr || actions.enable == nullptr ||
        actions.reset == nullptr) {
        return std::unexpected(TtxGuardInstallError{
            .stage = TtxGuardInstallStage::invalid_actions,
            .win32_error = ERROR_INVALID_PARAMETER,
        });
    }

    const HMODULE module =
        actions.get_module(actions.context, kTtxModuleName);
    if (module == nullptr) {
        const DWORD error = actions.get_last_error(actions.context);
        return std::unexpected(TtxGuardInstallError{
            .stage = TtxGuardInstallStage::resolve_module,
            .win32_error = error,
        });
    }

    const FARPROC target =
        actions.get_export(actions.context, module, kTtxUdlInitExport);
    if (target == nullptr) {
        const DWORD error = actions.get_last_error(actions.context);
        return std::unexpected(TtxGuardInstallError{
            .stage = TtxGuardInstallStage::resolve_export,
            .win32_error = error,
        });
    }

    const auto created = actions.create_disabled(
        actions.context,
        reinterpret_cast<void*>(target),
        actions.detour);
    if (!created) {
        const auto error = created.error();
        actions.reset(actions.context);
        return std::unexpected(TtxGuardInstallError{
            .stage = TtxGuardInstallStage::create_hook,
            .safetyhook_error = error,
        });
    }

    const auto enabled = actions.enable(actions.context);
    if (!enabled) {
        const auto error = enabled.error();
        actions.reset(actions.context);
        return std::unexpected(TtxGuardInstallError{
            .stage = TtxGuardInstallStage::enable_hook,
            .safetyhook_error = error,
        });
    }
    return {};
}

TtxInitGuard::TtxInitGuard(RuntimeRoot root)
    : root_{std::move(root)}
{
}

TtxInitGuard::~TtxInitGuard() noexcept
{
    Reset();
}

std::expected<void, TtxGuardInstallError> TtxInitGuard::Install() noexcept
{
    Reset();
    return InstallTtxInitGuard(TtxGuardInstallActions{
        .context = this,
        .detour = reinterpret_cast<void*>(&TtxInitGuard::Detour),
        .get_module = +[](void*, LPCWSTR name) noexcept {
            return GetModuleHandleW(name);
        },
        .get_export = +[](void*, HMODULE module, LPCSTR name) noexcept {
            return GetProcAddress(module, name);
        },
        .get_last_error = +[](void*) noexcept {
            return GetLastError();
        },
        .create_disabled = +[](
            void* context,
            void* target,
            void* detour) noexcept
            -> std::expected<void, std::uint32_t> {
            auto& self = *static_cast<TtxInitGuard*>(context);
            try {
                auto created = safetyhook::InlineHook::create(
                    target,
                    detour,
                    safetyhook::InlineHook::StartDisabled);
                if (!created) {
                    return std::unexpected(
                        SafetyHookErrorCode(created.error()));
                }
                self.hook_ = std::move(*created);
                active_.store(&self, std::memory_order_release);
                return {};
            } catch (...) {
                return std::unexpected(
                    static_cast<std::uint32_t>(
                        safetyhook::InlineHook::Error::BAD_ALLOCATION));
            }
        },
        .enable = +[](void* context) noexcept
            -> std::expected<void, std::uint32_t> {
            auto& self = *static_cast<TtxInitGuard*>(context);
            try {
                auto enabled = self.hook_.enable();
                if (!enabled) {
                    return std::unexpected(
                        SafetyHookErrorCode(enabled.error()));
                }
                return {};
            } catch (...) {
                return std::unexpected(
                    static_cast<std::uint32_t>(
                        safetyhook::InlineHook::Error::BAD_ALLOCATION));
            }
        },
        .reset = +[](void* context) noexcept {
            static_cast<TtxInitGuard*>(context)->Reset();
        },
    });
}

void TtxInitGuard::Reset() noexcept
{
    try {
        hook_.reset();
    } catch (...) {
    }
    auto* expected = this;
    static_cast<void>(active_.compare_exchange_strong(
        expected,
        nullptr,
        std::memory_order_acq_rel,
        std::memory_order_acquire));
}

int __cdecl TtxInitGuard::Detour(
    unsigned int priority,
    unsigned int game_version,
    unsigned int update_step,
    unsigned int update_options) noexcept
{
    auto* active = active_.load(std::memory_order_acquire);
    if (active == nullptr) {
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
                unsigned int original_update_options) noexcept {
                auto& self = *static_cast<TtxInitGuard*>(context);
                return self.hook_.unsafe_ccall<int>(
                    original_priority,
                    original_game_version,
                    original_update_step,
                    original_update_options);
            },
            .get_last_error = +[](void*) noexcept {
                return GetLastError();
            },
            .publish_failure = +[](
                void* context,
                DWORD error,
                const RuntimeRoot&) noexcept {
                static_cast<TtxInitGuard*>(context)->PublishFailure(error);
            },
        });
}

void TtxInitGuard::PublishFailure(DWORD error) noexcept
{
    try {
        const auto configured = Utf8ToWide(root_.configured_path);
        const auto resolved = root_.resolved_path.wstring();
        const auto error_text = Win32ErrorText(error);

        std::ostringstream log;
        log << "TtxUpdateDownloader initialization failed export="
            << kTtxUdlInitExport
            << " configured=" << root_.configured_path
            << " resolved=" << PathToUtf8(root_.resolved_path)
            << " win32_error=" << error
            << "; startup terminated before TtxUDLGetStatus could use "
               "uninitialized state";

        std::wostringstream modal;
        modal << L"TtxUpdateDownloader could not initialize.\n\n"
              << L"Configured system path: " << configured << L"\n"
              << L"Resolved system path: " << resolved << L"\n"
              << L"Windows error " << error << L": " << error_text
              << L"\n\nChoose a writable custom path or use .\\system, then "
                 L"restart the game.";

        PublishStartupFatal(
            failure_published_,
            log.str(),
            modal.str(),
            kTtxFatalTitle,
            kTtxRuntimeFailureExitCode);
        return;
    } catch (...) {
    }

    PublishStartupFatal(
        failure_published_,
        kTtxFallbackLog,
        kTtxFallbackModal,
        kTtxFatalTitle,
        kTtxRuntimeFailureExitCode);
}

} // namespace gc::system_path
