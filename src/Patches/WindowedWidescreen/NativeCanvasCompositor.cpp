#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"

namespace gc::windowed_widescreen
{
    NativeCanvasCompositor::NativeCanvasCompositor(
        const OutputSize output_size,
        const RenderThreadIdProvider thread_id_provider,
        const CompositorDeviceActions actions) noexcept
        : render_space_policy_{output_size, thread_id_provider},
          actions_{actions}
    {
    }

    bool NativeCanvasCompositor::ActionsAreComplete() const noexcept
    {
        return actions_.bind_wide_scene != nullptr &&
            actions_.bind_native_canvas != nullptr &&
            actions_.bind_real_backbuffer != nullptr &&
            actions_.capture_game_state != nullptr &&
            actions_.restore_game_state != nullptr &&
            actions_.draw_scene_center_to_native != nullptr &&
            actions_.draw_native_to_scene_center != nullptr &&
            actions_.draw_scene_to_backbuffer != nullptr &&
            actions_.set_full_viewport_and_scissor != nullptr &&
            actions_.native_depth_state_is_disabled != nullptr &&
            actions_.flush_native_batches != nullptr &&
            actions_.native_batches_are_empty != nullptr &&
            actions_.attempt_restore_after_failure != nullptr;
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::PolicyFailure(
        const RenderSpaceError error,
        const RenderSpace requested_space) const noexcept
    {
        return std::unexpected(CompositorError{
            .stage = CompositorStage::render_policy,
            .policy_error = error,
            .stable_space = last_published_space_,
            .requested_space = requested_space,
        });
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::FailAction(
        const CompositorStage stage,
        const RenderSpace stable_space,
        const RenderSpace requested_space,
        const bool close_frame) noexcept
    {
        const bool restored = actions_.attempt_restore_after_failure(
            actions_.context,
            stable_space);

        if (render_space_policy_.frame_active())
        {
            (void)render_space_policy_.PublishSpace(stable_space);
            if (close_frame)
            {
                (void)render_space_policy_.EndFrame();
            }
        }
        last_published_space_ = stable_space;

        return std::unexpected(CompositorError{
            .stage = stage,
            .stable_space = stable_space,
            .requested_space = requested_space,
            .restoration_attempted = true,
            .restoration_succeeded = restored,
        });
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::BeginFrame() noexcept
    {
        if (!ActionsAreComplete())
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_actions,
                .stable_space = last_published_space_,
                .requested_space = RenderSpace::physical_3d,
            });
        }

        if (const auto begun = render_space_policy_.BeginFrame(); !begun)
        {
            return PolicyFailure(
                begun.error(),
                RenderSpace::physical_3d);
        }
        last_published_space_ = RenderSpace::physical_3d;

        if (!actions_.bind_wide_scene(actions_.context))
        {
            return FailAction(
                CompositorStage::bind_wide_scene,
                RenderSpace::physical_3d,
                RenderSpace::physical_3d,
                true);
        }
        if (!actions_.set_full_viewport_and_scissor(
                actions_.context,
                RenderSpace::physical_3d))
        {
            return FailAction(
                CompositorStage::set_viewport_and_scissor,
                RenderSpace::physical_3d,
                RenderSpace::physical_3d,
                true);
        }
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::RequestSpace(
        const RenderSpace requested_space) noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(current.error(), requested_space);
        }
        if (requested_space == RenderSpace::compositor)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = requested_space,
            });
        }
        if (*current == requested_space)
        {
            return {};
        }
        return Transition(*current, requested_space);
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::Transition(
        const RenderSpace stable_space,
        const RenderSpace requested_space) noexcept
    {
        if (!actions_.flush_native_batches(actions_.context))
        {
            return FailAction(
                CompositorStage::flush_native_batches,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.native_batches_are_empty(actions_.context))
        {
            return FailAction(
                CompositorStage::pending_native_batches,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.capture_game_state(actions_.context))
        {
            return FailAction(
                CompositorStage::capture_game_state,
                stable_space,
                requested_space,
                false);
        }

        if (const auto entered =
                render_space_policy_.PublishSpace(RenderSpace::compositor);
            !entered)
        {
            return FailAction(
                CompositorStage::render_policy,
                stable_space,
                requested_space,
                false);
        }

        if (requested_space == RenderSpace::native_2d)
        {
            if (!actions_.bind_native_canvas(actions_.context))
            {
                return FailAction(
                    CompositorStage::bind_native_canvas,
                    stable_space,
                    requested_space,
                    false);
            }
            if (!actions_.draw_scene_center_to_native(actions_.context))
            {
                return FailAction(
                    CompositorStage::draw_scene_center_to_native,
                    stable_space,
                    requested_space,
                    false);
            }
        }
        else
        {
            if (!actions_.bind_wide_scene(actions_.context))
            {
                return FailAction(
                    CompositorStage::bind_wide_scene,
                    stable_space,
                    requested_space,
                    false);
            }
            if (!actions_.draw_native_to_scene_center(actions_.context))
            {
                return FailAction(
                    CompositorStage::draw_native_to_scene_center,
                    stable_space,
                    requested_space,
                    false);
            }
        }

        if (!actions_.restore_game_state(actions_.context))
        {
            return FailAction(
                CompositorStage::restore_game_state,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.set_full_viewport_and_scissor(
                actions_.context,
                requested_space))
        {
            return FailAction(
                CompositorStage::set_viewport_and_scissor,
                stable_space,
                requested_space,
                false);
        }
        if (requested_space == RenderSpace::native_2d &&
            !actions_.native_depth_state_is_disabled(actions_.context))
        {
            return FailAction(
                CompositorStage::native_depth_state,
                stable_space,
                requested_space,
                false);
        }

        if (const auto published =
                render_space_policy_.PublishSpace(requested_space);
            !published)
        {
            return FailAction(
                CompositorStage::render_policy,
                stable_space,
                requested_space,
                false);
        }
        last_published_space_ = requested_space;
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::CopySceneToBackbuffer() noexcept
    {
        constexpr auto stable_space = RenderSpace::physical_3d;
        constexpr auto requested_space = RenderSpace::physical_3d;

        if (!actions_.flush_native_batches(actions_.context))
        {
            return FailAction(
                CompositorStage::flush_native_batches,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.native_batches_are_empty(actions_.context))
        {
            return FailAction(
                CompositorStage::pending_native_batches,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.capture_game_state(actions_.context))
        {
            return FailAction(
                CompositorStage::capture_game_state,
                stable_space,
                requested_space,
                false);
        }
        if (const auto entered =
                render_space_policy_.PublishSpace(RenderSpace::compositor);
            !entered)
        {
            return FailAction(
                CompositorStage::render_policy,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.bind_real_backbuffer(actions_.context))
        {
            return FailAction(
                CompositorStage::bind_real_backbuffer,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.draw_scene_to_backbuffer(actions_.context))
        {
            return FailAction(
                CompositorStage::draw_scene_to_backbuffer,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.restore_game_state(actions_.context))
        {
            return FailAction(
                CompositorStage::restore_game_state,
                stable_space,
                requested_space,
                false);
        }
        if (!actions_.set_full_viewport_and_scissor(
                actions_.context,
                RenderSpace::physical_3d))
        {
            return FailAction(
                CompositorStage::set_viewport_and_scissor,
                stable_space,
                requested_space,
                false);
        }
        if (const auto published =
                render_space_policy_.PublishSpace(RenderSpace::physical_3d);
            !published)
        {
            return FailAction(
                CompositorStage::render_policy,
                stable_space,
                requested_space,
                false);
        }
        last_published_space_ = RenderSpace::physical_3d;
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::EndFrame() noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(
                current.error(),
                RenderSpace::physical_3d);
        }

        if (*current == RenderSpace::native_2d)
        {
            if (const auto closed = Transition(
                    RenderSpace::native_2d,
                    RenderSpace::physical_3d);
                !closed)
            {
                return closed;
            }
        }
        else if (*current != RenderSpace::physical_3d)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = last_published_space_,
                .requested_space = RenderSpace::physical_3d,
            });
        }

        if (const auto copied = CopySceneToBackbuffer(); !copied)
        {
            return copied;
        }
        if (const auto ended = render_space_policy_.EndFrame(); !ended)
        {
            return PolicyFailure(
                ended.error(),
                RenderSpace::physical_3d);
        }
        return {};
    }
} // namespace gc::windowed_widescreen
