#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"

namespace gc::windowed_widescreen
{
    namespace
    {
        [[nodiscard]] bool UsesNativeTarget(
            const RenderSpace space) noexcept
        {
            return space == RenderSpace::native_2d;
        }

        [[nodiscard]] bool NeedsDisabledDepth(
            const RenderSpace space) noexcept
        {
            return space == RenderSpace::native_2d ||
                space == RenderSpace::gameplay_hud;
        }
    } // namespace

    NativeCanvasCompositor::NativeCanvasCompositor(
        const OutputSize output_size,
        const GameplayHudPlacement base_gameplay_hud_placement,
        const RenderThreadIdProvider thread_id_provider,
        const CompositorDeviceActions actions) noexcept
        : render_space_policy_{output_size, thread_id_provider},
          actions_{actions},
          base_gameplay_hud_placement_{base_gameplay_hud_placement},
          gameplay_hud_placement_{base_gameplay_hud_placement}
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
            actions_.set_gameplay_hud_viewport != nullptr &&
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
        if (stable_space == RenderSpace::gameplay_hud)
        {
            gameplay_hud_placement_ = base_gameplay_hud_placement_;
        }

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
        gameplay_hud_placement_ = base_gameplay_hud_placement_;
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
    NativeCanvasCompositor::SetGameplayHudPlacement(
        const GameplayHudPlacement placement) noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(current.error(), RenderSpace::gameplay_hud);
        }
        if (*current != RenderSpace::gameplay_hud)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::gameplay_hud,
            });
        }

        const auto active_viewport = ResolveGameplayHudViewport(
            render_space_policy_.output_size(),
            gameplay_hud_placement_);
        const auto requested_viewport = ResolveGameplayHudViewport(
            render_space_policy_.output_size(),
            placement);
        if (!active_viewport || !requested_viewport)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::gameplay_hud,
            });
        }
        if (*active_viewport == *requested_viewport)
        {
            gameplay_hud_placement_ = placement;
            return {};
        }

        return ReapplyGameplayHudPlacement(placement);
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::ReapplyGameplayHudPlacement(
        const GameplayHudPlacement placement) noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(current.error(), RenderSpace::gameplay_hud);
        }
        if (*current != RenderSpace::gameplay_hud)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::gameplay_hud,
            });
        }

        const auto requested_viewport = ResolveGameplayHudViewport(
            render_space_policy_.output_size(),
            placement);
        if (!requested_viewport)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::gameplay_hud,
            });
        }

        if (!actions_.flush_native_batches(actions_.context))
        {
            return FailAction(
                CompositorStage::flush_native_batches,
                *current,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.native_batches_are_empty(actions_.context))
        {
            return FailAction(
                CompositorStage::pending_native_batches,
                *current,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.set_gameplay_hud_viewport(
                actions_.context,
                placement))
        {
            return FailAction(
                CompositorStage::set_gameplay_hud_viewport,
                *current,
                RenderSpace::gameplay_hud,
                false);
        }

        gameplay_hud_placement_ = placement;
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::BeginPhysicalGameplayHudOverlay(
        const GameplayHudPlacement placement) noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(
                current.error(),
                RenderSpace::gameplay_hud);
        }
        if (*current != RenderSpace::physical_3d ||
            physical_gameplay_hud_overlay_active_)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::gameplay_hud,
            });
        }

        if (!actions_.flush_native_batches(actions_.context))
        {
            return FailAction(
                CompositorStage::flush_native_batches,
                RenderSpace::physical_3d,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.native_batches_are_empty(actions_.context))
        {
            return FailAction(
                CompositorStage::pending_native_batches,
                RenderSpace::physical_3d,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.capture_game_state(actions_.context))
        {
            return FailAction(
                CompositorStage::capture_game_state,
                RenderSpace::physical_3d,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.set_gameplay_hud_viewport(
                actions_.context,
                placement))
        {
            return FailAction(
                CompositorStage::set_gameplay_hud_viewport,
                RenderSpace::physical_3d,
                RenderSpace::gameplay_hud,
                false);
        }
        if (!actions_.native_depth_state_is_disabled(actions_.context))
        {
            return FailAction(
                CompositorStage::native_depth_state,
                RenderSpace::physical_3d,
                RenderSpace::gameplay_hud,
                false);
        }

        physical_gameplay_hud_overlay_active_ = true;
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::EndPhysicalGameplayHudOverlay() noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current)
        {
            return PolicyFailure(
                current.error(),
                RenderSpace::physical_3d);
        }
        if (*current != RenderSpace::physical_3d ||
            !physical_gameplay_hud_overlay_active_)
        {
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current,
                .requested_space = RenderSpace::physical_3d,
            });
        }

        const auto fail = [this](
                              const CompositorStage stage)
            -> std::expected<void, CompositorError>
        {
            physical_gameplay_hud_overlay_active_ = false;
            return FailAction(
                stage,
                RenderSpace::physical_3d,
                RenderSpace::physical_3d,
                false);
        };

        if (!actions_.flush_native_batches(actions_.context))
        {
            return fail(CompositorStage::flush_native_batches);
        }
        if (!actions_.native_batches_are_empty(actions_.context))
        {
            return fail(CompositorStage::pending_native_batches);
        }
        if (!actions_.restore_game_state(actions_.context))
        {
            return fail(CompositorStage::restore_game_state);
        }

        physical_gameplay_hud_overlay_active_ = false;
        return {};
    }


    std::expected<void, CompositorError>
    NativeCanvasCompositor::BeginGameplayHudDraw(
        const GameplayHudPlacement placement) noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current) return PolicyFailure(current.error(), RenderSpace::gameplay_hud);
        if ((*current != RenderSpace::physical_3d && *current != RenderSpace::gameplay_hud) ||
            gameplay_hud_draw_active_ || physical_gameplay_hud_overlay_active_ ||
            !actions_.capture_hud_draw_state || !actions_.restore_hud_draw_state ||
            !actions_.flush_hud_draw_batches)
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current, .requested_space = RenderSpace::gameplay_hud});
        if (!actions_.flush_hud_draw_batches(actions_.context))
            return FailAction(CompositorStage::flush_native_batches, *current, *current, false);
        if (!actions_.native_batches_are_empty(actions_.context))
            return FailAction(CompositorStage::pending_native_batches, *current, *current, false);
        if (!actions_.capture_hud_draw_state(actions_.context))
            return FailAction(CompositorStage::capture_game_state, *current, *current, false);
        if (!actions_.set_gameplay_hud_viewport(actions_.context, placement))
            return FailAction(CompositorStage::set_gameplay_hud_viewport, *current, *current, false);
        enclosing_hud_placement_ = gameplay_hud_placement_;
        gameplay_hud_placement_ = placement;
        gameplay_hud_draw_active_ = true;
        return {};
    }

    std::expected<void, CompositorError>
    NativeCanvasCompositor::EndGameplayHudDraw() noexcept
    {
        const auto current = render_space_policy_.CurrentSpace();
        if (!current) return PolicyFailure(current.error(), RenderSpace::gameplay_hud);
        if (!gameplay_hud_draw_active_ || !actions_.restore_hud_draw_state)
            return std::unexpected(CompositorError{
                .stage = CompositorStage::invalid_destination,
                .stable_space = *current, .requested_space = *current});
        if (!actions_.flush_hud_draw_batches(actions_.context))
            return FailAction(CompositorStage::flush_native_batches, *current, *current, false);
        if (!actions_.native_batches_are_empty(actions_.context))
            return FailAction(CompositorStage::pending_native_batches, *current, *current, false);
        if (!actions_.restore_hud_draw_state(actions_.context))
            return FailAction(CompositorStage::restore_game_state, *current, *current, false);
        gameplay_hud_placement_ = enclosing_hud_placement_;
        gameplay_hud_draw_active_ = false;
        return {};
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
        const bool target_changes =
            UsesNativeTarget(stable_space) !=
            UsesNativeTarget(requested_space);
        if (target_changes)
        {
            if (!actions_.capture_game_state(actions_.context))
            {
                return FailAction(
                    CompositorStage::capture_game_state,
                    stable_space,
                    requested_space,
                    false);
            }

            if (const auto entered = render_space_policy_.PublishSpace(
                    RenderSpace::compositor);
                !entered)
            {
                return FailAction(
                    CompositorStage::render_policy,
                    stable_space,
                    requested_space,
                    false);
            }

            if (UsesNativeTarget(requested_space))
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
        if (NeedsDisabledDepth(requested_space) &&
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
        gameplay_hud_placement_ = base_gameplay_hud_placement_;
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

        if (*current == RenderSpace::native_2d ||
            *current == RenderSpace::gameplay_hud)
        {
            if (const auto closed = Transition(
                    *current,
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

    void NativeCanvasCompositor::ResetForDeviceLoss() noexcept
    {
        gameplay_hud_draw_active_ = false;
        enclosing_hud_placement_ = base_gameplay_hud_placement_;
        render_space_policy_.ResetForDeviceLoss();
        last_published_space_ = RenderSpace::physical_3d;
        gameplay_hud_placement_ = base_gameplay_hud_placement_;
        physical_gameplay_hud_overlay_active_ = false;
    }
} // namespace gc::windowed_widescreen
