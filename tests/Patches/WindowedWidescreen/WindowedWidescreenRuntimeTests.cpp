#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"

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

    struct FakeRendererResource
    {
        std::size_t create_count{};
        std::size_t release_count{};
        std::uintptr_t last_renderer_owner{};
        bool create_succeeds{true};

        static bool Create(
            void* context,
            const std::uintptr_t renderer_owner) noexcept
        {
            auto* self = static_cast<FakeRendererResource*>(context);
            ++self->create_count;
            self->last_renderer_owner = renderer_owner;
            return self->create_succeeds;
        }

        static void Release(void* context) noexcept
        {
            ++static_cast<FakeRendererResource*>(context)->release_count;
        }

        [[nodiscard]] gc::renderer_device_loss::RendererResourceParticipant
        Participant() noexcept
        {
            return {
                .context = this,
                .create = &Create,
                .release = &Release,
            };
        }
    };

    void RendererResourceLifecycleReleasesAndRecreatesExactlyOnce()
    {
        using namespace gc::renderer_device_loss;
        RendererResourceLifecycle lifecycle;
        FakeRendererResource resource;

        Expect(
            lifecycle.state() == RendererResourceState::disabled,
            "resource lifecycle starts disabled");
        Expect(
            lifecycle.Attach(resource.Participant()).has_value() &&
                lifecycle.state() == RendererResourceState::awaiting_device,
            "attach waits for the renderer-owned device");
        Expect(
            lifecycle.OnDeviceCreated(0x12340000).has_value() &&
                lifecycle.state() == RendererResourceState::active &&
                resource.create_count == 1 &&
                resource.last_renderer_owner == 0x12340000,
            "device-created activates one participant once");

        Expect(
            lifecycle.BeforeReset().has_value() &&
                lifecycle.state() == RendererResourceState::awaiting_reset &&
                resource.release_count == 1,
            "pre-reset releases all owned references once");
        Expect(
            lifecycle.BeforeReset().has_value() &&
                resource.release_count == 1,
            "repeated pre-reset after native Reset failure is idempotent");
        Expect(
            lifecycle.AfterReset(0x12340000).has_value() &&
                lifecycle.state() == RendererResourceState::active &&
                resource.create_count == 2,
            "successful post-reset recreates exactly once");

        lifecycle.Detach();
        Expect(
            lifecycle.state() == RendererResourceState::disabled &&
                resource.release_count == 2,
            "detach releases active resources and disables lifecycle");
        lifecycle.Detach();
        Expect(
            resource.release_count == 2,
            "repeated detach is release-idempotent");
    }

    void RendererResourceLifecycleRetainsRecoverableFailureState()
    {
        using namespace gc::renderer_device_loss;
        RendererResourceLifecycle lifecycle;
        FakeRendererResource resource;
        Expect(
            lifecycle.Attach(resource.Participant()).has_value(),
            "failure lifecycle attaches");

        const auto invalid_pre = lifecycle.BeforeReset();
        Expect(
            !invalid_pre &&
                invalid_pre.error() == RendererResourceError::invalid_order,
            "pre-reset before device creation is rejected");

        resource.create_succeeds = false;
        const auto create_failure = lifecycle.OnDeviceCreated(0x12340000);
        Expect(
            !create_failure &&
                create_failure.error() ==
                    RendererResourceError::create_failed &&
                lifecycle.state() == RendererResourceState::awaiting_device &&
                resource.create_count == 1 && resource.release_count == 0,
            "initial create failure never publishes active state");

        resource.create_succeeds = true;
        Expect(
            lifecycle.OnDeviceCreated(0x12340000).has_value(),
            "initial create may be retried while awaiting device");
        Expect(lifecycle.BeforeReset().has_value(), "retry setup releases");

        resource.create_succeeds = false;
        const auto recreate_failure = lifecycle.AfterReset(0x12340000);
        Expect(
            !recreate_failure &&
                recreate_failure.error() ==
                    RendererResourceError::create_failed &&
                lifecycle.state() == RendererResourceState::awaiting_reset &&
                resource.release_count == 1,
            "failed recreation remains released and awaiting reset");
        Expect(
            lifecycle.BeforeReset().has_value() &&
                resource.release_count == 1,
            "pre-reset after recreate failure performs no second release");

        resource.create_succeeds = true;
        Expect(
            lifecycle.AfterReset(0x12340000).has_value() &&
                lifecycle.state() == RendererResourceState::active,
            "later post-reset recreation can recover once");
    }

    void RendererResourceLifecycleRejectsInvalidParticipation()
    {
        using namespace gc::renderer_device_loss;
        RendererResourceLifecycle lifecycle;
        const auto missing = lifecycle.Attach({});
        Expect(
            !missing &&
                missing.error() ==
                    RendererResourceError::invalid_participant,
            "missing participant actions are rejected");

        FakeRendererResource resource;
        Expect(
            lifecycle.Attach(resource.Participant()).has_value(),
            "valid participant attaches once");
        const auto duplicate = lifecycle.Attach(resource.Participant());
        Expect(
            !duplicate &&
                duplicate.error() == RendererResourceError::invalid_order,
            "second participant is rejected");
        const auto invalid_post = lifecycle.AfterReset(0x12340000);
        Expect(
            !invalid_post &&
                invalid_post.error() == RendererResourceError::invalid_order,
            "post-reset before active release is rejected");
    }

    struct FakeOwnedReferenceSet
    {
        static constexpr std::size_t reference_count = 7;

        std::size_t fail_at{reference_count};
        std::size_t live_references{};
        std::size_t cleanup_count{};

        static bool Create(void* context, std::uintptr_t) noexcept
        {
            auto* self = static_cast<FakeOwnedReferenceSet*>(context);
            self->live_references = 0;
            for (std::size_t index = 0; index < reference_count; ++index)
            {
                ++self->live_references;
                if (index == self->fail_at)
                {
                    self->live_references = 0;
                    ++self->cleanup_count;
                    return false;
                }
            }
            return true;
        }

        static void Release(void* context) noexcept
        {
            auto* self = static_cast<FakeOwnedReferenceSet*>(context);
            self->live_references = 0;
            ++self->cleanup_count;
        }

        [[nodiscard]] gc::renderer_device_loss::RendererResourceParticipant
        Participant() noexcept
        {
            return {
                .context = this,
                .create = &Create,
                .release = &Release,
            };
        }
    };

    void PartialResourceCreationNeverPublishesOrLeaks()
    {
        using namespace gc::renderer_device_loss;
        for (std::size_t fail_at = 0;
             fail_at < FakeOwnedReferenceSet::reference_count;
             ++fail_at)
        {
            RendererResourceLifecycle lifecycle;
            FakeOwnedReferenceSet resources{.fail_at = fail_at};
            Expect(
                lifecycle.Attach(resources.Participant()).has_value(),
                "partial resource participant attaches");
            const auto created = lifecycle.OnDeviceCreated(0x12340000);
            Expect(
                !created &&
                    created.error() == RendererResourceError::create_failed &&
                    lifecycle.state() ==
                        RendererResourceState::awaiting_device,
                "failure at each allocation does not publish active state");
            Expect(
                resources.live_references == 0 &&
                    resources.cleanup_count == 1,
                "partial allocation failure releases every owned reference");
        }
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
    RendererResourceLifecycleReleasesAndRecreatesExactlyOnce();
    RendererResourceLifecycleRetainsRecoverableFailureState();
    RendererResourceLifecycleRejectsInvalidParticipation();
    PartialResourceCreationNeverPublishesOrLeaks();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
