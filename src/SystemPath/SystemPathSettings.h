#pragma once

#include <string>
#include <utility>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::system_path
{
    class SystemPathSettings final
    {
    public:
        [[nodiscard]] bool registry_enabled() const noexcept
        {
            return registry_enabled_;
        }

        [[nodiscard]] const std::string& configured_path() const noexcept
        {
            return configured_path_;
        }

    private:
        SystemPathSettings(
            bool registry_enabled,
            std::string configured_path)
            : registry_enabled_(registry_enabled),
              configured_path_(std::move(configured_path))
        {
        }

        friend class gc::config::ConfigCompiler;
        bool registry_enabled_{};
        std::string configured_path_;
    };
} // namespace gc::system_path
