#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"
#include "Patches/WindowedWidescreen/PassClassifier.h"
#include "Patches/WindowedWidescreen/ProjectionPolicy.h"
#include "Patches/WindowedWidescreen/RenderSpacePolicy.h"
#include "Patches/WindowedWidescreen/ResolutionModel.h"
#include "Patches/WindowedWidescreen/StageClipPolicy.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void ResolutionCentersTheNativeCanvasWithHalfOpenMapping()
    {
        const auto model =
            gc::windowed_widescreen::ResolutionModel::Create(1137, 1281);
        Expect(model.has_value(), "valid odd-sized output creates a model");
        if (!model)
        {
            return;
        }

        const auto output = model->output_size();
        Expect(
            output.width == 1137 && output.height == 1281,
            "model preserves physical output size");

        const auto native = model->native_rect();
        Expect(
            native.left == 208 && native.top == 0 &&
                native.right == 928 && native.bottom == 1280,
            "odd remainder is assigned to the right and bottom bars");

        const auto first = model->ClientToNative(208, 0);
        Expect(
            first.has_value() && first->x == 0 && first->y == 0,
            "native top-left maps inclusively");
        const auto last = model->ClientToNative(927, 1279);
        Expect(
            last.has_value() && last->x == 719 && last->y == 1279,
            "native bottom-right pixel maps inclusively");
        Expect(
            !model->ClientToNative(207, 0).has_value(),
            "left bar is outside native input");
        Expect(
            !model->ClientToNative(928, 0).has_value(),
            "right edge is half-open");
        Expect(
            !model->ClientToNative(208, 1280).has_value(),
            "bottom edge is half-open");
    }

    void ResolutionRejectsUnsafeGeometry()
    {
        using gc::windowed_widescreen::ResolutionError;
        using gc::windowed_widescreen::ResolutionModel;

        const auto narrow = ResolutionModel::Create(719, 1280);
        Expect(
            !narrow && narrow.error() == ResolutionError::width_below_native,
            "width below the native canvas is rejected");

        const auto short_output = ResolutionModel::Create(720, 1279);
        Expect(
            !short_output &&
                short_output.error() == ResolutionError::height_below_native,
            "height below the native canvas is rejected");

        const auto signed_overflow = ResolutionModel::Create(
            std::numeric_limits<std::uint32_t>::max(),
            1280);
        Expect(
            !signed_overflow &&
                signed_overflow.error() == ResolutionError::signed_range,
            "geometry beyond signed client coordinates is rejected");

        const auto area_overflow = ResolutionModel::Create(65'536, 65'536);
        Expect(
            !area_overflow &&
                area_overflow.error() == ResolutionError::arithmetic_overflow,
            "physical pixel-area overflow is rejected");
    }

    void ProjectionExpandsOnlyForOutputHeight()
    {
        using gc::windowed_widescreen::ProjectionError;
        using gc::windowed_widescreen::TransformProjectionScale;

        const auto identity = TransformProjectionScale(1.0F, 1280);
        Expect(
            identity.has_value() && *identity == 1.0F,
            "native height preserves the native projection scale exactly");

        const auto taller = TransformProjectionScale(1.0F, 1600);
        Expect(
            taller.has_value() && std::abs(*taller - 1.16815F) < 0.0001F,
            "1600-high output expands the vertical field of view");

        const auto scaled_identity = TransformProjectionScale(2.0F, 1280);
        Expect(
            scaled_identity.has_value() && *scaled_identity == 2.0F,
            "native height preserves caller-provided scale exactly");

        const auto invalid = TransformProjectionScale(0.0F, 1280);
        Expect(
            !invalid && invalid.error() == ProjectionError::invalid_scale,
            "non-positive projection scale is rejected");

        const auto non_finite = TransformProjectionScale(
            std::numeric_limits<float>::infinity(),
            1280);
        Expect(
            !non_finite &&
                non_finite.error() == ProjectionError::invalid_scale,
            "non-finite projection scale is rejected");

        const auto native_limit = TransformProjectionScale(2.3F, 1280);
        Expect(
            !native_limit &&
                native_limit.error() == ProjectionError::fov_limit,
            "native field of view at or beyond the safety limit is rejected");

        const auto expanded_limit = TransformProjectionScale(
            1.0F,
            std::numeric_limits<std::uint32_t>::max());
        Expect(
            !expanded_limit &&
                expanded_limit.error() == ProjectionError::fov_limit,
            "expanded field of view at the safety limit is rejected");
    }

    void WindowPlacementPrefersPrimaryThenEnumerationOrder()
    {
        using gc::windowed_widescreen::MonitorWorkArea;
        using gc::windowed_widescreen::SelectWindowPlacement;
        using gc::windowed_widescreen::WindowOuterSize;

        const std::array monitors{
            MonitorWorkArea{
                .left = -1920,
                .top = 0,
                .right = 0,
                .bottom = 1440,
                .primary = false,
            },
            MonitorWorkArea{
                .left = 0,
                .top = 0,
                .right = 2560,
                .bottom = 1440,
                .primary = true,
            },
        };
        const auto primary = SelectWindowPlacement(
            monitors,
            WindowOuterSize{.width = 1000, .height = 1280});
        Expect(primary.has_value(), "a fitting primary monitor is selected");
        if (primary)
        {
            Expect(
                primary->monitor_index == 1 &&
                    primary->x == 780 && primary->y == 80,
                "primary fit wins regardless of enumeration order");
        }

        const std::array fallback_monitors{
            MonitorWorkArea{
                .left = 0,
                .top = 0,
                .right = 800,
                .bottom = 600,
                .primary = true,
            },
            MonitorWorkArea{
                .left = -1920,
                .top = 0,
                .right = 0,
                .bottom = 1280,
                .primary = false,
            },
            MonitorWorkArea{
                .left = 1920,
                .top = 0,
                .right = 3840,
                .bottom = 1280,
                .primary = false,
            },
        };
        const auto fallback = SelectWindowPlacement(
            fallback_monitors,
            WindowOuterSize{.width = 1920, .height = 1280});
        Expect(fallback.has_value(), "a non-primary exact-edge fit is found");
        if (fallback)
        {
            Expect(
                fallback->monitor_index == 1 &&
                    fallback->x == -1920 && fallback->y == 0,
                "first enumerated fallback preserves negative origin");
            Expect(
                fallback->size.width == 1920 &&
                    fallback->size.height == 1280,
                "placement preserves requested outer size");
        }
    }

    void WindowPlacementRejectsInvalidOrUnfittableRequests()
    {
        using gc::windowed_widescreen::MonitorWorkArea;
        using gc::windowed_widescreen::NativeWindowPolicyError;
        using gc::windowed_widescreen::SelectWindowPlacement;
        using gc::windowed_widescreen::WindowOuterSize;

        const std::array monitors{
            MonitorWorkArea{
                .left = 0,
                .top = 0,
                .right = 1920,
                .bottom = 1080,
                .primary = true,
            },
        };

        const auto invalid = SelectWindowPlacement(
            monitors,
            WindowOuterSize{.width = 0, .height = 1280});
        Expect(
            !invalid &&
                invalid.error() ==
                    NativeWindowPolicyError::invalid_outer_size,
            "zero outer width is rejected");

        const auto too_large = SelectWindowPlacement(
            monitors,
            WindowOuterSize{.width = 1920, .height = 1280});
        Expect(
            !too_large &&
                too_large.error() ==
                    NativeWindowPolicyError::no_fitting_work_area,
            "window that fits no work area is rejected");
    }

    void ClipPolicySelectsOneStableGateAction()
    {
        using namespace gc::windowed_widescreen;
        Expect(
            SelectClipGateAction(StageClipPolicy::authored) ==
                ClipGateAction::continue_authored,
            "authored policy continues the original clip branch");
        Expect(
            SelectClipGateAction(StageClipPolicy::live_frustum) ==
                ClipGateAction::jump_live_frustum,
            "live-frustum policy skips authored clip loading");
    }

    struct ClassifierDiagnostics
    {
        std::size_t identity_count{};
        std::size_t overflow_count{};

        static void UnknownIdentity(
            void* context,
            const std::uintptr_t) noexcept
        {
            ++static_cast<ClassifierDiagnostics*>(context)->identity_count;
        }

        static void UnknownOverflow(void* context) noexcept
        {
            ++static_cast<ClassifierDiagnostics*>(context)->overflow_count;
        }
    };

    void PassClassifierUsesOnlyVerifiedStableIdentities()
    {
        using namespace gc::windowed_widescreen;
        constexpr std::uintptr_t image_base = 0x00400000;
        ClassifierDiagnostics diagnostics;
        PassClassifier classifier{
            image_base,
            PassClassifierDiagnosticSink{
                .context = &diagnostics,
                .unknown_identity =
                    &ClassifierDiagnostics::UnknownIdentity,
                .unknown_capacity_exhausted =
                    &ClassifierDiagnostics::UnknownOverflow,
            },
        };

        Expect(
            classifier.ClassifyTask(image_base + 0x002F9AFC) ==
                RenderSpace::native_2d,
            "relocated CCommon2DTask vtable selects native space");
        Expect(
            classifier.ClassifyTask(image_base + 0x002FB218) ==
                RenderSpace::physical_3d,
            "relocated CCommon3DTask vtable selects physical space");
        Expect(
            PassClassifier::ClassifyGameplay(
                GameplayPass::orthographic_background) ==
                RenderSpace::native_2d,
            "orthographic gameplay background is native");
        Expect(
            PassClassifier::ClassifyGameplay(
                GameplayPass::perspective_track) ==
                RenderSpace::physical_3d,
            "perspective gameplay track is physical");
        Expect(
            PassClassifier::ClassifyGameplay(
                GameplayPass::orthographic_effects) ==
                RenderSpace::native_2d,
            "orthographic gameplay effects are native");

        constexpr std::uintptr_t unknown = 0x12345678;
        Expect(
            classifier.ClassifyTask(unknown) == RenderSpace::native_2d &&
                classifier.ClassifyTask(unknown) == RenderSpace::native_2d,
            "unknown stable identities fail closed to native space");
        Expect(
            diagnostics.identity_count == 1 &&
                classifier.unknown_identity_count() == 1,
            "one unknown identity emits only one diagnostic");

        for (std::size_t index = 1;
             index < PassClassifier::unknown_identity_capacity();
             ++index)
        {
            (void)classifier.ClassifyTask(unknown + index * 4);
        }
        (void)classifier.ClassifyTask(0x22345678);
        (void)classifier.ClassifyTask(0x32345678);
        Expect(
            classifier.unknown_identity_count() ==
                PassClassifier::unknown_identity_capacity(),
            "unknown identity storage is fixed-capacity");
        Expect(
            diagnostics.overflow_count == 1 &&
                classifier.unknown_capacity_exhausted(),
            "unknown table overflow emits one aggregate diagnostic");
    }

    struct FakeThreadId
    {
        std::uint32_t value{};

        static std::uint32_t Current(void* context) noexcept
        {
            return static_cast<FakeThreadId*>(context)->value;
        }
    };

    void RenderSpacePolicyOwnsFrameAndThreadState()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread{.value = 41};
        RenderSpacePolicy policy{
            OutputSize{.width = 1920, .height = 1600},
            RenderThreadIdProvider{
                .context = &thread,
                .current = &FakeThreadId::Current,
            },
        };

        const auto begin = policy.BeginFrame();
        Expect(begin.has_value(), "first frame captures the render thread");
        Expect(
            policy.CurrentSpace().has_value() &&
                *policy.CurrentSpace() == RenderSpace::physical_3d,
            "each frame begins in physical space");

        const auto nested = policy.BeginFrame();
        Expect(
            !nested && nested.error() == RenderSpaceError::nested_frame,
            "nested frame begin is rejected");

        const auto transition = policy.PublishSpace(RenderSpace::native_2d);
        Expect(transition.has_value(), "same-thread transition is accepted");
        Expect(
            policy.CurrentSpace().has_value() &&
                *policy.CurrentSpace() == RenderSpace::native_2d,
            "published native space becomes current");

        thread.value = 42;
        const auto wrong_thread =
            policy.PublishSpace(RenderSpace::physical_3d);
        Expect(
            !wrong_thread &&
                wrong_thread.error() == RenderSpaceError::wrong_thread,
            "transition from a different thread is rejected");
        const auto wrong_end = policy.EndFrame();
        Expect(
            !wrong_end && wrong_end.error() == RenderSpaceError::wrong_thread,
            "frame end from a different thread is rejected");

        thread.value = 41;
        Expect(policy.EndFrame().has_value(), "owner thread closes the frame");
        const auto outside = policy.PublishSpace(RenderSpace::physical_3d);
        Expect(
            !outside && outside.error() == RenderSpaceError::outside_frame,
            "transition outside a frame is rejected");

        thread.value = 42;
        const auto later_wrong_begin = policy.BeginFrame();
        Expect(
            !later_wrong_begin &&
                later_wrong_begin.error() == RenderSpaceError::wrong_thread,
            "later frames remain owned by the first render thread");
    }

    void RenderDimensionsFollowLogicalSpace()
    {
        using namespace gc::windowed_widescreen;
        const OutputSize output{.width = 1920, .height = 1600};

        const auto physical =
            SelectRenderDimensions(RenderSpace::physical_3d, output);
        Expect(
            physical.has_value() && physical->width == 1920 &&
                physical->height == 1600 &&
                physical->width_float == 1920.0F &&
                physical->height_float == 1600.0F,
            "physical dimensions report exact integer and float output");

        const auto native =
            SelectRenderDimensions(RenderSpace::native_2d, output);
        Expect(
            native.has_value() && native->width == 720 &&
                native->height == 1280 &&
                native->width_float == 720.0F &&
                native->height_float == 1280.0F,
            "native dimensions report exact integer and float canvas");

        const auto compositor =
            SelectRenderDimensions(RenderSpace::compositor, output);
        Expect(
            !compositor &&
                compositor.error() ==
                    RenderSpaceError::compositor_dimensions,
            "loader-owned compositor cannot answer game dimensions");
    }
} // namespace

int main()
{
    ResolutionCentersTheNativeCanvasWithHalfOpenMapping();
    ResolutionRejectsUnsafeGeometry();
    ProjectionExpandsOnlyForOutputHeight();
    WindowPlacementPrefersPrimaryThenEnumerationOrder();
    WindowPlacementRejectsInvalidOrUnfittableRequests();
    ClipPolicySelectsOneStableGateAction();
    PassClassifierUsesOnlyVerifiedStableIdentities();
    RenderSpacePolicyOwnsFrameAndThreadState();
    RenderDimensionsFollowLogicalSpace();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
