#include "Config/ConfigError.h"

#include <sstream>
#include <utility>

namespace gc::config
{
    ConfigPath::ConfigPath(
        std::initializer_list<ConfigPathSegment> segments)
        : segments_(segments)
    {
    }

    ConfigPath::ConfigPath(std::vector<ConfigPathSegment> segments)
        : segments_(std::move(segments))
    {
    }

    ConfigPath ConfigPath::Child(std::string field) const
    {
        auto segments = segments_;
        segments.emplace_back(std::move(field));
        return ConfigPath{std::move(segments)};
    }

    ConfigPath ConfigPath::Index(std::size_t index) const
    {
        auto segments = segments_;
        segments.emplace_back(index);
        return ConfigPath{std::move(segments)};
    }

    std::string ConfigPath::Render() const
    {
        std::ostringstream rendered;
        bool first_field = true;
        for (const auto& segment : segments_)
        {
            if (const auto* field = std::get_if<std::string>(&segment))
            {
                if (!first_field)
                {
                    rendered << '.';
                }
                rendered << *field;
                first_field = false;
            }
            else
            {
                rendered << '[' << std::get<std::size_t>(segment) << ']';
            }
        }
        return rendered.str();
    }

    std::string FormatConfigErrors(std::span<const ConfigError> errors)
    {
        std::ostringstream rendered;
        for (std::size_t index = 0; index < errors.size(); ++index)
        {
            if (index != 0)
            {
                rendered << '\n';
            }
            rendered << errors[index].path.Render()
                << ": " << errors[index].message;
        }
        return rendered.str();
    }
} // namespace gc::config
