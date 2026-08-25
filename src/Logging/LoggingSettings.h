#pragma once

#include <cstdint>

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
