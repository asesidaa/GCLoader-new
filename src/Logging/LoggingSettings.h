#pragma once

#include <cstdint>
#include <string_view>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::logging
{
    enum class LoaderLogLevel : std::uint8_t
    {
        Info,
        Debug,
        Verbose,
    };

    [[nodiscard]] constexpr std::string_view LoaderLogLevelName(
        LoaderLogLevel level) noexcept
    {
        switch (level)
        {
        case LoaderLogLevel::Info:
            return "Info";
        case LoaderLogLevel::Debug:
            return "Debug";
        case LoaderLogLevel::Verbose:
            return "Verbose";
        }
        return "Unknown";
    }

    class LoggingSettings final
    {
    public:
        [[nodiscard]] LoaderLogLevel level() const noexcept
        {
            return level_;
        }

    private:
        explicit LoggingSettings(LoaderLogLevel level) noexcept
            : level_(level)
        {
        }

        friend class gc::config::ConfigCompiler;
        LoaderLogLevel level_{};
    };
} // namespace gc::logging
