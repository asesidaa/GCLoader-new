#pragma once

#include "Patches/WindowedWidescreen/ResolutionModel.h"

#include <cstdint>
#include <expected>

namespace gc::windowed_widescreen
{
    enum class RenderSpace : std::uint8_t
    {
        physical_3d,
        native_2d,
        compositor,
    };

    enum class RenderSpaceError : std::uint8_t
    {
        invalid_thread_provider,
        nested_frame,
        outside_frame,
        wrong_thread,
        compositor_dimensions,
    };

    struct RenderThreadIdProvider
    {
        void* context{};
        std::uint32_t (*current)(void*) noexcept{};
    };

    struct RenderDimensions
    {
        std::uint32_t width{};
        std::uint32_t height{};
        float width_float{};
        float height_float{};
    };

    [[nodiscard]] std::expected<RenderDimensions, RenderSpaceError>
    SelectRenderDimensions(
        RenderSpace space,
        OutputSize output_size) noexcept;

    class RenderSpacePolicy final
    {
    public:
        RenderSpacePolicy(
            OutputSize output_size,
            RenderThreadIdProvider thread_id_provider) noexcept;

        [[nodiscard]] std::expected<void, RenderSpaceError>
        BeginFrame() noexcept;

        [[nodiscard]] std::expected<void, RenderSpaceError>
        EndFrame() noexcept;

        void ResetForDeviceLoss() noexcept;

        [[nodiscard]] std::expected<void, RenderSpaceError>
        PublishSpace(RenderSpace space) noexcept;

        [[nodiscard]] std::expected<RenderSpace, RenderSpaceError>
        CurrentSpace() const noexcept;

        [[nodiscard]] std::expected<RenderDimensions, RenderSpaceError>
        CurrentDimensions() const noexcept;

        [[nodiscard]] bool frame_active() const noexcept
        {
            return frame_active_;
        }

        [[nodiscard]] OutputSize output_size() const noexcept
        {
            return output_size_;
        }

    private:
        [[nodiscard]] std::expected<void, RenderSpaceError>
        ValidateThread() const noexcept;

        OutputSize output_size_{};
        RenderThreadIdProvider thread_id_provider_{};
        std::uint32_t render_thread_id_{};
        bool render_thread_captured_{};
        bool frame_active_{};
        RenderSpace current_space_{RenderSpace::physical_3d};
    };
} // namespace gc::windowed_widescreen
