#include "Config/RegistryConfig.h"

#include <Windows.h>

#include <filesystem>
#include <limits>
#include <utility>

namespace
{
    constexpr std::size_t kMaximumRegistryPathBytes = 259;

    std::unexpected<std::string> PathError(std::string detail)
    {
        return std::unexpected(
            "Invalid [registry].system_path: " + std::move(detail));
    }

    std::expected<std::wstring, std::string> Utf8ToWide(
        std::string_view value)
    {
        if (value.empty())
        {
            return PathError(
                "the system root must not be empty; use '.\\system' for "
                "the current config directory");
        }
        if (value.find('\0') != std::string_view::npos ||
            value.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
        {
            return PathError("the system root is not valid UTF-8 path text");
        }

        const auto source_size = static_cast<int>(value.size());
        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            nullptr,
            0);
        if (required <= 0)
        {
            return PathError("the system root is not valid UTF-8 path text");
        }

        std::wstring converted(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            converted.data(),
            required) != required)
        {
            return PathError("the system root could not be decoded from UTF-8");
        }
        return converted;
    }

    std::expected<std::string, std::string> WideToServiceAnsi(
        const std::filesystem::path& value)
    {
        const std::wstring native = value.native();
        if (native.empty() ||
            native.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
        {
            return PathError("a derived NESYS service path is invalid");
        }

        BOOL used_default_char = FALSE;
        const int required = WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            native.data(),
            static_cast<int>(native.size()),
            nullptr,
            0,
            nullptr,
            &used_default_char);
        if (required <= 0 || used_default_char != FALSE)
        {
            return PathError(
                "the NESYS service uses ANSI registry paths and this root "
                "cannot be represented losslessly; use an ASCII path such "
                "as '.\\system'");
        }
        if (static_cast<std::size_t>(required) >
            kMaximumRegistryPathBytes)
        {
            return PathError(
                "a derived NESYS service ANSI path exceeds 259 bytes; "
                "use a shorter root such as '.\\system'");
        }

        std::string converted(static_cast<std::size_t>(required), '\0');
        used_default_char = FALSE;
        const int written = WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            native.data(),
            static_cast<int>(native.size()),
            converted.data(),
            required,
            nullptr,
            &used_default_char);
        if (written != required || used_default_char != FALSE)
        {
            return PathError(
                "the NESYS service uses ANSI registry paths and this root "
                "cannot be represented losslessly; use an ASCII path such "
                "as '.\\system'");
        }
        return converted;
    }
} // namespace

namespace gc::registry_config
{
    std::expected<DerivedNesysPaths, std::string> DeriveNesysPaths(
        std::string_view system_path) noexcept
    {
        try
        {
            const auto wide_root = Utf8ToWide(system_path);
            if (!wide_root)
            {
                return std::unexpected(wide_root.error());
            }

            const std::filesystem::path root{*wide_root};
            auto news = WideToServiceAnsi(root / L"DUA" / L"news");
            if (!news)
            {
                return std::unexpected(news.error());
            }
            auto event = WideToServiceAnsi(root / L"DUA" / L"event");
            if (!event)
            {
                return std::unexpected(event.error());
            }
            auto log = WideToServiceAnsi(root / L"CmdFile" / L"log");
            if (!log)
            {
                return std::unexpected(log.error());
            }
            return DerivedNesysPaths{
                std::move(*news),
                std::move(*event),
                std::move(*log),
            };
        }
        catch (...)
        {
            return PathError("the derived NESYS service paths could not be built");
        }
    }
} // namespace gc::registry_config
