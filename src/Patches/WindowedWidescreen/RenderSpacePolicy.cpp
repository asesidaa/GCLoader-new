#include "Patches/WindowedWidescreen/RenderSpacePolicy.h"

namespace gc::windowed_widescreen
{
    std::expected<RenderDimensions, RenderSpaceError>
    SelectRenderDimensions(
        const RenderSpace space,
        const OutputSize output_size) noexcept
    {
        switch (space)
        {
        case RenderSpace::physical_3d:
            return RenderDimensions{
                .width = output_size.width,
                .height = output_size.height,
                .width_float = static_cast<float>(output_size.width),
                .height_float = static_cast<float>(output_size.height),
            };
        case RenderSpace::native_2d:
            return RenderDimensions{
                .width = kNativeWidth,
                .height = kNativeHeight,
                .width_float = static_cast<float>(kNativeWidth),
                .height_float = static_cast<float>(kNativeHeight),
            };
        case RenderSpace::compositor:
            return std::unexpected(RenderSpaceError::compositor_dimensions);
        }
        return std::unexpected(RenderSpaceError::compositor_dimensions);
    }

    RenderSpacePolicy::RenderSpacePolicy(
        const OutputSize output_size,
        const RenderThreadIdProvider thread_id_provider) noexcept
        : output_size_{output_size},
          thread_id_provider_{thread_id_provider}
    {
    }

    std::expected<void, RenderSpaceError>
    RenderSpacePolicy::ValidateThread() const noexcept
    {
        if (thread_id_provider_.current == nullptr)
        {
            return std::unexpected(
                RenderSpaceError::invalid_thread_provider);
        }
        if (render_thread_captured_ &&
            thread_id_provider_.current(thread_id_provider_.context) !=
                render_thread_id_)
        {
            return std::unexpected(RenderSpaceError::wrong_thread);
        }
        return {};
    }

    std::expected<void, RenderSpaceError>
    RenderSpacePolicy::BeginFrame() noexcept
    {
        if (thread_id_provider_.current == nullptr)
        {
            return std::unexpected(
                RenderSpaceError::invalid_thread_provider);
        }
        if (frame_active_)
        {
            return std::unexpected(RenderSpaceError::nested_frame);
        }

        const auto current_thread =
            thread_id_provider_.current(thread_id_provider_.context);
        if (!render_thread_captured_)
        {
            render_thread_id_ = current_thread;
            render_thread_captured_ = true;
        }
        else if (current_thread != render_thread_id_)
        {
            return std::unexpected(RenderSpaceError::wrong_thread);
        }

        current_space_ = RenderSpace::physical_3d;
        frame_active_ = true;
        return {};
    }

    std::expected<void, RenderSpaceError>
    RenderSpacePolicy::EndFrame() noexcept
    {
        if (!frame_active_)
        {
            return std::unexpected(RenderSpaceError::outside_frame);
        }
        if (const auto valid = ValidateThread(); !valid)
        {
            return valid;
        }

        frame_active_ = false;
        return {};
    }

    std::expected<void, RenderSpaceError>
    RenderSpacePolicy::PublishSpace(const RenderSpace space) noexcept
    {
        if (!frame_active_)
        {
            return std::unexpected(RenderSpaceError::outside_frame);
        }
        if (const auto valid = ValidateThread(); !valid)
        {
            return valid;
        }

        current_space_ = space;
        return {};
    }

    std::expected<RenderSpace, RenderSpaceError>
    RenderSpacePolicy::CurrentSpace() const noexcept
    {
        if (!frame_active_)
        {
            return std::unexpected(RenderSpaceError::outside_frame);
        }
        if (const auto valid = ValidateThread(); !valid)
        {
            return std::unexpected(valid.error());
        }
        return current_space_;
    }

    std::expected<RenderDimensions, RenderSpaceError>
    RenderSpacePolicy::CurrentDimensions() const noexcept
    {
        const auto space = CurrentSpace();
        if (!space)
        {
            return std::unexpected(space.error());
        }
        return SelectRenderDimensions(*space, output_size_);
    }
} // namespace gc::windowed_widescreen
