#pragma once

#include <cstdint>
#include <expected>

namespace gc::windowed_widescreen
{
    enum class ProjectionError
    {
        invalid_scale,
        fov_limit,
    };

    [[nodiscard]] std::expected<float, ProjectionError>
    TransformProjectionScale(
        float native_scale,
        std::uint32_t output_height) noexcept;
} // namespace gc::windowed_widescreen
