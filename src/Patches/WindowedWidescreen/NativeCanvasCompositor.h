#pragma once

#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include "Patches/WindowedWidescreen/RenderSpacePolicy.h"

#include <cstdint>
#include <expected>
#include <optional>

namespace gc::windowed_widescreen
{
    struct CompositorDeviceActions final
    {
        void* context{};
        bool (*bind_wide_scene)(void*) noexcept{};
        bool (*bind_native_canvas)(void*) noexcept{};
        bool (*bind_real_backbuffer)(void*) noexcept{};
        bool (*capture_game_state)(void*) noexcept{};
        bool (*restore_game_state)(void*) noexcept{};
        bool (*draw_scene_center_to_native)(void*) noexcept{};
        bool (*draw_native_to_scene_center)(void*) noexcept{};
        bool (*draw_scene_to_backbuffer)(void*) noexcept{};
        bool (*set_full_viewport_and_scissor)(
            void*, RenderSpace) noexcept{};
        bool (*set_gameplay_hud_viewport)(
            void*, GameplayHudPlacement) noexcept{};
        bool (*native_depth_state_is_disabled)(void*) noexcept{};
        bool (*flush_native_batches)(void*) noexcept{};
        bool (*native_batches_are_empty)(void*) noexcept{};
        bool (*attempt_restore_after_failure)(
            void*, RenderSpace) noexcept{};
    };

    enum class CompositorStage : std::uint8_t
    {
        invalid_actions,
        render_policy,
        invalid_destination,
        bind_wide_scene,
        bind_native_canvas,
        bind_real_backbuffer,
        capture_game_state,
        restore_game_state,
        draw_scene_center_to_native,
        draw_native_to_scene_center,
        draw_scene_to_backbuffer,
        set_viewport_and_scissor,
        set_gameplay_hud_viewport,
        native_depth_state,
        flush_native_batches,
        pending_native_batches,
    };

    struct CompositorError
    {
        CompositorStage stage{CompositorStage::render_policy};
        std::optional<RenderSpaceError> policy_error;
        RenderSpace stable_space{RenderSpace::physical_3d};
        RenderSpace requested_space{RenderSpace::physical_3d};
        bool restoration_attempted{};
        bool restoration_succeeded{};
    };

    class NativeCanvasCompositor final
    {
    public:
        NativeCanvasCompositor(
            OutputSize output_size,
            RenderThreadIdProvider thread_id_provider,
            CompositorDeviceActions actions) noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        BeginFrame() noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        RequestSpace(RenderSpace requested_space) noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        SetGameplayHudPlacement(GameplayHudPlacement placement) noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        BeginPhysicalGameplayHudOverlay(
            GameplayHudPlacement placement) noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        EndPhysicalGameplayHudOverlay() noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        EndFrame() noexcept;

        void ResetForDeviceLoss() noexcept;

        [[nodiscard]] std::expected<RenderSpace, RenderSpaceError>
        CurrentSpace() const noexcept
        {
            return render_space_policy_.CurrentSpace();
        }

        [[nodiscard]] std::expected<RenderDimensions, RenderSpaceError>
        CurrentDimensions() const noexcept
        {
            return render_space_policy_.CurrentDimensions();
        }

        [[nodiscard]] bool frame_active() const noexcept
        {
            return render_space_policy_.frame_active();
        }

        [[nodiscard]] GameplayHudPlacement gameplay_hud_placement()
            const noexcept
        {
            return gameplay_hud_placement_;
        }

        [[nodiscard]] bool physical_gameplay_hud_overlay_active()
            const noexcept
        {
            return physical_gameplay_hud_overlay_active_;
        }

    private:
        [[nodiscard]] bool ActionsAreComplete() const noexcept;

        [[nodiscard]] std::expected<void, CompositorError> Transition(
            RenderSpace stable_space,
            RenderSpace requested_space) noexcept;

        [[nodiscard]] std::expected<void, CompositorError>
        CopySceneToBackbuffer() noexcept;

        [[nodiscard]] std::expected<void, CompositorError> FailAction(
            CompositorStage stage,
            RenderSpace stable_space,
            RenderSpace requested_space,
            bool close_frame) noexcept;

        [[nodiscard]] std::expected<void, CompositorError> PolicyFailure(
            RenderSpaceError error,
            RenderSpace requested_space) const noexcept;

        RenderSpacePolicy render_space_policy_;
        CompositorDeviceActions actions_{};
        RenderSpace last_published_space_{RenderSpace::physical_3d};
        GameplayHudPlacement gameplay_hud_placement_{
            GameplayHudPlacement::centered};
        bool physical_gameplay_hud_overlay_active_{};
    };
} // namespace gc::windowed_widescreen
