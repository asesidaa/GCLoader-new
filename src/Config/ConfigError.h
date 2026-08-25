#pragma once

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gc::config
{
    using ConfigPathSegment = std::variant<std::string, std::size_t>;

    class ConfigPath final
    {
    public:
        ConfigPath(std::initializer_list<ConfigPathSegment> segments);

        [[nodiscard]] ConfigPath Child(std::string field) const;
        [[nodiscard]] ConfigPath Index(std::size_t index) const;
        [[nodiscard]] std::string Render() const;

        auto operator<=>(const ConfigPath&) const = default;

    private:
        explicit ConfigPath(std::vector<ConfigPathSegment> segments);

        std::vector<ConfigPathSegment> segments_;
    };

    enum class ConfigErrorCode : std::uint8_t
    {
        invalid_value,
        unsupported_value,
        out_of_range,
        required_value,
        invalid_encoding,
        invalid_path,
        incompatible_fields,
        unmet_dependency,
    };

    struct ConfigError
    {
        ConfigPath path;
        ConfigErrorCode code{};
        std::string message;
        std::vector<ConfigPath> related_paths;
    };

    using ConfigErrors = std::vector<ConfigError>;

    [[nodiscard]] std::string FormatConfigErrors(
        std::span<const ConfigError> errors);
} // namespace gc::config
