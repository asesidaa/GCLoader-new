#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

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

    enum class DeviceCall
    {
        bind_wide,
        bind_native,
        bind_backbuffer,
        capture_state,
        restore_state,
        draw_scene_to_native,
        draw_native_to_scene,
        draw_scene_to_backbuffer,
        viewport_physical,
        viewport_native,
        depth_disabled,
        flush_batches,
        batches_empty,
        recover_physical,
        recover_native,
    };

    struct FakeThreadId
    {
        std::uint32_t value{71};

        static std::uint32_t Current(void* context) noexcept
        {
            return static_cast<FakeThreadId*>(context)->value;
        }
    };

    struct FakeDeviceActions
    {
        std::vector<DeviceCall> calls;
        std::optional<DeviceCall> fail_call;
        bool batches_pending{};
        bool recovery_succeeds{true};

        bool Run(const DeviceCall call)
        {
            calls.push_back(call);
            return !fail_call.has_value() || *fail_call != call;
        }

        static bool BindWide(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::bind_wide);
        }

        static bool BindNative(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::bind_native);
        }

        static bool BindBackbuffer(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::bind_backbuffer);
        }

        static bool CaptureState(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::capture_state);
        }

        static bool RestoreState(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::restore_state);
        }

        static bool DrawSceneToNative(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::draw_scene_to_native);
        }

        static bool DrawNativeToScene(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::draw_native_to_scene);
        }

        static bool DrawSceneToBackbuffer(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::draw_scene_to_backbuffer);
        }

        static bool SetViewport(
            void* context,
            const gc::windowed_widescreen::RenderSpace space) noexcept
        {
            auto* self = static_cast<FakeDeviceActions*>(context);
            return self->Run(
                space == gc::windowed_widescreen::RenderSpace::native_2d
                    ? DeviceCall::viewport_native
                    : DeviceCall::viewport_physical);
        }

        static bool NativeDepthDisabled(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::depth_disabled);
        }

        static bool FlushBatches(void* context) noexcept
        {
            return static_cast<FakeDeviceActions*>(context)->Run(
                DeviceCall::flush_batches);
        }

        static bool BatchesEmpty(void* context) noexcept
        {
            auto* self = static_cast<FakeDeviceActions*>(context);
            return self->Run(DeviceCall::batches_empty) &&
                !self->batches_pending;
        }

        static bool AttemptRecovery(
            void* context,
            const gc::windowed_widescreen::RenderSpace stable_space) noexcept
        {
            auto* self = static_cast<FakeDeviceActions*>(context);
            const auto call =
                stable_space ==
                    gc::windowed_widescreen::RenderSpace::native_2d
                ? DeviceCall::recover_native
                : DeviceCall::recover_physical;
            self->calls.push_back(call);
            return self->recovery_succeeds;
        }

        [[nodiscard]] gc::windowed_widescreen::CompositorDeviceActions
        Actions() noexcept
        {
            return {
                .context = this,
                .bind_wide_scene = &BindWide,
                .bind_native_canvas = &BindNative,
                .bind_real_backbuffer = &BindBackbuffer,
                .capture_game_state = &CaptureState,
                .restore_game_state = &RestoreState,
                .draw_scene_center_to_native = &DrawSceneToNative,
                .draw_native_to_scene_center = &DrawNativeToScene,
                .draw_scene_to_backbuffer = &DrawSceneToBackbuffer,
                .set_full_viewport_and_scissor = &SetViewport,
                .native_depth_state_is_disabled = &NativeDepthDisabled,
                .flush_native_batches = &FlushBatches,
                .native_batches_are_empty = &BatchesEmpty,
                .attempt_restore_after_failure = &AttemptRecovery,
            };
        }
    };

    [[nodiscard]] gc::windowed_widescreen::NativeCanvasCompositor
    MakeCompositor(FakeThreadId& thread, FakeDeviceActions& device)
    {
        return gc::windowed_widescreen::NativeCanvasCompositor{
            gc::windowed_widescreen::OutputSize{
                .width = 1920,
                .height = 1280,
            },
            gc::windowed_widescreen::RenderThreadIdProvider{
                .context = &thread,
                .current = &FakeThreadId::Current,
            },
            device.Actions(),
        };
    }

    void ExpectCalls(
        const std::vector<DeviceCall>& actual,
        const std::initializer_list<DeviceCall> expected,
        const std::string_view message)
    {
        Expect(
            actual == std::vector<DeviceCall>{expected},
            message);
    }

    void TransitionOrderAndSameSpaceNoOpAreExact()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread;
        FakeDeviceActions device;
        auto compositor = MakeCompositor(thread, device);

        Expect(compositor.BeginFrame().has_value(), "frame begin succeeds");
        ExpectCalls(
            device.calls,
            {DeviceCall::bind_wide, DeviceCall::viewport_physical},
            "frame begin binds physical scene before native begin");

        device.calls.clear();
        Expect(
            compositor.RequestSpace(RenderSpace::physical_3d).has_value(),
            "same physical request succeeds");
        Expect(device.calls.empty(), "same-space request performs no actions");

        Expect(
            compositor.RequestSpace(RenderSpace::native_2d).has_value(),
            "physical-to-native transition succeeds");
        ExpectCalls(
            device.calls,
            {
                DeviceCall::flush_batches,
                DeviceCall::batches_empty,
                DeviceCall::capture_state,
                DeviceCall::bind_native,
                DeviceCall::draw_scene_to_native,
                DeviceCall::restore_state,
                DeviceCall::viewport_native,
                DeviceCall::depth_disabled,
            },
            "physical-to-native transition order is exact");

        device.calls.clear();
        Expect(
            compositor.RequestSpace(RenderSpace::native_2d).has_value(),
            "same native request succeeds");
        Expect(device.calls.empty(), "contiguous native tasks do not copy");

        Expect(
            compositor.RequestSpace(RenderSpace::physical_3d).has_value(),
            "native-to-physical transition succeeds");
        ExpectCalls(
            device.calls,
            {
                DeviceCall::flush_batches,
                DeviceCall::batches_empty,
                DeviceCall::capture_state,
                DeviceCall::bind_wide,
                DeviceCall::draw_native_to_scene,
                DeviceCall::restore_state,
                DeviceCall::viewport_physical,
            },
            "native-to-physical transition order is exact");

        device.calls.clear();
        Expect(compositor.EndFrame().has_value(), "frame end succeeds");
        ExpectCalls(
            device.calls,
            {
                DeviceCall::flush_batches,
                DeviceCall::batches_empty,
                DeviceCall::capture_state,
                DeviceCall::bind_backbuffer,
                DeviceCall::draw_scene_to_backbuffer,
                DeviceCall::restore_state,
                DeviceCall::viewport_physical,
            },
            "frame end copies the wide scene to the real backbuffer");
        Expect(!compositor.frame_active(), "successful frame end closes state");
    }

    void FrameEndClosesNativeSpaceBeforePresentationCopy()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread;
        FakeDeviceActions device;
        auto compositor = MakeCompositor(thread, device);
        Expect(compositor.BeginFrame().has_value(), "native-end begin succeeds");
        Expect(
            compositor.RequestSpace(RenderSpace::native_2d).has_value(),
            "native-end setup transition succeeds");
        device.calls.clear();

        Expect(compositor.EndFrame().has_value(), "native frame end succeeds");
        ExpectCalls(
            device.calls,
            {
                DeviceCall::flush_batches,
                DeviceCall::batches_empty,
                DeviceCall::capture_state,
                DeviceCall::bind_wide,
                DeviceCall::draw_native_to_scene,
                DeviceCall::restore_state,
                DeviceCall::viewport_physical,
                DeviceCall::flush_batches,
                DeviceCall::batches_empty,
                DeviceCall::capture_state,
                DeviceCall::bind_backbuffer,
                DeviceCall::draw_scene_to_backbuffer,
                DeviceCall::restore_state,
                DeviceCall::viewport_physical,
            },
            "native frame end closes native then copies the complete scene");
    }

    void BeginAndTransitionFailuresRestorePublishedSpace()
    {
        using namespace gc::windowed_widescreen;

        {
            FakeThreadId thread;
            FakeDeviceActions device;
            device.fail_call = DeviceCall::bind_wide;
            auto compositor = MakeCompositor(thread, device);
            const auto result = compositor.BeginFrame();
            Expect(
                !result &&
                    result.error().stage ==
                        CompositorStage::bind_wide_scene &&
                    result.error().restoration_attempted &&
                    result.error().restoration_succeeded,
                "begin bind failure is structured and restored");
            Expect(!compositor.frame_active(), "failed begin closes frame state");
        }

        const std::array physical_to_native_failures{
            std::pair{DeviceCall::flush_batches,
                      CompositorStage::flush_native_batches},
            std::pair{DeviceCall::batches_empty,
                      CompositorStage::pending_native_batches},
            std::pair{DeviceCall::capture_state,
                      CompositorStage::capture_game_state},
            std::pair{DeviceCall::bind_native,
                      CompositorStage::bind_native_canvas},
            std::pair{DeviceCall::draw_scene_to_native,
                      CompositorStage::draw_scene_center_to_native},
            std::pair{DeviceCall::restore_state,
                      CompositorStage::restore_game_state},
            std::pair{DeviceCall::viewport_native,
                      CompositorStage::set_viewport_and_scissor},
            std::pair{DeviceCall::depth_disabled,
                      CompositorStage::native_depth_state},
        };

        for (const auto& [call, stage] : physical_to_native_failures)
        {
            FakeThreadId thread;
            FakeDeviceActions device;
            auto compositor = MakeCompositor(thread, device);
            Expect(compositor.BeginFrame().has_value(), "failure setup begins");
            device.calls.clear();
            device.fail_call = call;
            const auto result = compositor.RequestSpace(RenderSpace::native_2d);
            Expect(
                !result && result.error().stage == stage,
                "physical-to-native failure reports exact stage");
            const auto current = compositor.CurrentSpace();
            Expect(
                current.has_value() && *current == RenderSpace::physical_3d,
                "failed transition retains published physical space");
            Expect(
                result.error().restoration_attempted &&
                    result.error().restoration_succeeded &&
                    !device.calls.empty() &&
                    device.calls.back() == DeviceCall::recover_physical,
                "failed transition restores the last physical target");
        }
    }

    void ReverseAndFinalCopyFailuresRestoreTheirStableTarget()
    {
        using namespace gc::windowed_widescreen;
        const std::array reverse_failures{
            std::pair{DeviceCall::bind_wide,
                      CompositorStage::bind_wide_scene},
            std::pair{DeviceCall::draw_native_to_scene,
                      CompositorStage::draw_native_to_scene_center},
            std::pair{DeviceCall::viewport_physical,
                      CompositorStage::set_viewport_and_scissor},
        };

        for (const auto& [call, stage] : reverse_failures)
        {
            FakeThreadId thread;
            FakeDeviceActions device;
            auto compositor = MakeCompositor(thread, device);
            Expect(compositor.BeginFrame().has_value(), "reverse setup begins");
            Expect(
                compositor.RequestSpace(RenderSpace::native_2d).has_value(),
                "reverse setup enters native");
            device.calls.clear();
            device.fail_call = call;
            const auto result =
                compositor.RequestSpace(RenderSpace::physical_3d);
            Expect(
                !result && result.error().stage == stage,
                "native-to-physical failure reports exact stage");
            const auto current = compositor.CurrentSpace();
            Expect(
                current.has_value() && *current == RenderSpace::native_2d,
                "failed reverse transition retains native space");
            Expect(
                device.calls.back() == DeviceCall::recover_native,
                "failed reverse transition restores native target");
        }

        const std::array final_failures{
            std::pair{DeviceCall::bind_backbuffer,
                      CompositorStage::bind_real_backbuffer},
            std::pair{DeviceCall::draw_scene_to_backbuffer,
                      CompositorStage::draw_scene_to_backbuffer},
        };
        for (const auto& [call, stage] : final_failures)
        {
            FakeThreadId thread;
            FakeDeviceActions device;
            auto compositor = MakeCompositor(thread, device);
            Expect(compositor.BeginFrame().has_value(), "final setup begins");
            device.calls.clear();
            device.fail_call = call;
            const auto result = compositor.EndFrame();
            Expect(
                !result && result.error().stage == stage,
                "final copy failure reports exact stage");
            const auto current = compositor.CurrentSpace();
            Expect(
                compositor.frame_active() && current.has_value() &&
                    *current == RenderSpace::physical_3d,
                "failed final copy keeps the physical frame open");
            Expect(
                device.calls.back() == DeviceCall::recover_physical,
                "failed final copy restores physical target");
        }
    }

    void PendingBatchesAndFailedRecoveryAreFatalState()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread;
        FakeDeviceActions device;
        auto compositor = MakeCompositor(thread, device);
        Expect(compositor.BeginFrame().has_value(), "pending setup begins");
        device.calls.clear();
        device.batches_pending = true;
        device.recovery_succeeds = false;
        const auto result = compositor.RequestSpace(RenderSpace::native_2d);
        Expect(
            !result &&
                result.error().stage ==
                    CompositorStage::pending_native_batches &&
                result.error().restoration_attempted &&
                !result.error().restoration_succeeded,
            "pending batches plus failed recovery remain explicit");
        const auto current = compositor.CurrentSpace();
        Expect(
            current.has_value() && *current == RenderSpace::physical_3d,
            "failed recovery still does not publish destination space");
    }

    void RuntimeRejectsInvalidFrameAndThreadCalls()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread;
        FakeDeviceActions device;
        auto compositor = MakeCompositor(thread, device);

        const auto outside = compositor.RequestSpace(RenderSpace::native_2d);
        Expect(
            !outside && outside.error().stage == CompositorStage::render_policy &&
                outside.error().policy_error == RenderSpaceError::outside_frame,
            "transition outside frame reports policy error");

        Expect(compositor.BeginFrame().has_value(), "invariant setup begins");
        const auto nested = compositor.BeginFrame();
        Expect(
            !nested && nested.error().policy_error ==
                RenderSpaceError::nested_frame,
            "nested begin reports policy error");

        thread.value = 72;
        const auto wrong = compositor.RequestSpace(RenderSpace::native_2d);
        Expect(
            !wrong && wrong.error().policy_error ==
                RenderSpaceError::wrong_thread,
            "wrong-thread transition reports policy error");
        Expect(
            device.calls == std::vector<DeviceCall>{
                DeviceCall::bind_wide,
                DeviceCall::viewport_physical,
            },
            "policy rejection performs no device actions");
    }

    void MissingDeviceActionRejectsBeforeFrameMutation()
    {
        using namespace gc::windowed_widescreen;
        FakeThreadId thread;
        FakeDeviceActions device;
        auto actions = device.Actions();
        actions.draw_scene_to_backbuffer = nullptr;
        NativeCanvasCompositor compositor{
            OutputSize{.width = 1920, .height = 1280},
            RenderThreadIdProvider{
                .context = &thread,
                .current = &FakeThreadId::Current,
            },
            actions,
        };
        const auto result = compositor.BeginFrame();
        Expect(
            !result &&
                result.error().stage == CompositorStage::invalid_actions &&
                !compositor.frame_active() && device.calls.empty(),
            "missing action rejects before frame or device mutation");
    }
} // namespace

int main()
{
    TransitionOrderAndSameSpaceNoOpAreExact();
    FrameEndClosesNativeSpaceBeforePresentationCopy();
    BeginAndTransitionFailuresRestorePublishedSpace();
    ReverseAndFinalCopyFailuresRestoreTheirStableTarget();
    PendingBatchesAndFailedRecoveryAreFatalState();
    RuntimeRejectsInvalidFrameAndThreadCalls();
    MissingDeviceActionRejectsBeforeFrameMutation();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
