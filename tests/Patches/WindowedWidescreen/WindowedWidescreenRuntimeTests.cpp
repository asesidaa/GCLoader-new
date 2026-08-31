#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
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

    enum class InstallEventKind
    {
        read,
        create,
        enable,
        reset,
        detach_resource,
        clear_context,
        publish,
    };

    struct InstallEvent
    {
        InstallEventKind kind{};
        gc::windowed_widescreen::WidescreenContractSite site{};

        bool operator==(const InstallEvent&) const = default;
    };

    struct FakeInstallEnvironment
    {
        struct MemoryRegion
        {
            std::uintptr_t address{};
            std::vector<std::byte> bytes;
        };

        std::vector<MemoryRegion> memory;
        std::vector<InstallEvent> events;
        std::optional<std::size_t> fail_create_index;
        std::optional<std::size_t> fail_enable_index;
        bool fail_reads_after_create{};
        bool publish_succeeds{true};
        bool owner_published{};
        std::size_t create_count{};
        std::size_t enable_count{};
        std::size_t reads_before_first_create{};

        void AddBytes(
            const std::uintptr_t address,
            const std::initializer_list<std::uint8_t> values)
        {
            MemoryRegion region{.address = address};
            for (const auto value : values)
            {
                region.bytes.push_back(static_cast<std::byte>(value));
            }
            memory.push_back(std::move(region));
        }

        void AddPointer(
            const std::uintptr_t address,
            const std::uint32_t value)
        {
            AddBytes(
                address,
                {
                    static_cast<std::uint8_t>(value & 0xFF),
                    static_cast<std::uint8_t>((value >> 8) & 0xFF),
                    static_cast<std::uint8_t>((value >> 16) & 0xFF),
                    static_cast<std::uint8_t>((value >> 24) & 0xFF),
                });
        }

        static bool Read(
            void* context,
            const std::uintptr_t address,
            const std::span<std::byte> output) noexcept
        {
            auto* self = static_cast<FakeInstallEnvironment*>(context);
            self->events.push_back({.kind = InstallEventKind::read});
            if (self->create_count == 0)
            {
                ++self->reads_before_first_create;
            }
            else if (self->fail_reads_after_create)
            {
                return false;
            }

            for (const auto& region : self->memory)
            {
                if (region.address == address &&
                    region.bytes.size() == output.size())
                {
                    std::copy(
                        region.bytes.begin(),
                        region.bytes.end(),
                        output.begin());
                    return true;
                }
            }
            return false;
        }

        static bool CreateDisabled(
            void* context,
            const gc::windowed_widescreen::WidescreenContractSite site,
            gc::windowed_widescreen::WidescreenHookKind,
            std::uintptr_t,
            void*) noexcept
        {
            auto* self = static_cast<FakeInstallEnvironment*>(context);
            const auto index = self->create_count++;
            self->events.push_back({InstallEventKind::create, site});
            return !self->fail_create_index.has_value() ||
                *self->fail_create_index != index;
        }

        static bool Enable(
            void* context,
            const gc::windowed_widescreen::WidescreenContractSite site) noexcept
        {
            auto* self = static_cast<FakeInstallEnvironment*>(context);
            const auto index = self->enable_count++;
            self->events.push_back({InstallEventKind::enable, site});
            return !self->fail_enable_index.has_value() ||
                *self->fail_enable_index != index;
        }

        static void Reset(
            void* context,
            const gc::windowed_widescreen::WidescreenContractSite site) noexcept
        {
            static_cast<FakeInstallEnvironment*>(context)->events.push_back(
                {InstallEventKind::reset, site});
        }

        static void ClearContext(void* context) noexcept
        {
            static_cast<FakeInstallEnvironment*>(context)->events.push_back(
                {.kind = InstallEventKind::clear_context});
        }

        static void DetachResource(void* context) noexcept
        {
            static_cast<FakeInstallEnvironment*>(context)->events.push_back(
                {.kind = InstallEventKind::detach_resource});
        }

        static bool Publish(void* context) noexcept
        {
            auto* self = static_cast<FakeInstallEnvironment*>(context);
            self->events.push_back({.kind = InstallEventKind::publish});
            if (self->publish_succeeds)
            {
                self->owner_published = true;
            }
            return self->publish_succeeds;
        }

        [[nodiscard]] gc::windowed_widescreen::WidescreenInstallActions
        Actions() noexcept
        {
            return {
                .context = this,
                .read = &Read,
                .create_disabled = &CreateDisabled,
                .enable = &Enable,
                .reset = &Reset,
                .detach_renderer_resource = &DetachResource,
                .clear_callback_context = &ClearContext,
                .publish_owner = &Publish,
            };
        }
    };

    struct SyntheticInstallFixture
    {
        static constexpr std::uintptr_t image_base = 0x00400000;

        std::array<gc::windowed_widescreen::WidescreenByteContract, 3>
            byte_contracts{
                gc::windowed_widescreen::WidescreenByteContract{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::config_apply,
                    .rva = 0x100,
                    .pattern = gc::windowed_widescreen::BytePatternOf<
                        0xAA, 0xBB>(),
                    .hook_kind = gc::windowed_widescreen::
                        WidescreenHookKind::inline_hook,
                },
                gc::windowed_widescreen::WidescreenByteContract{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::frame_begin,
                    .rva = 0x120,
                    .pattern = gc::windowed_widescreen::BytePatternOf<
                        0xCC, 0xDD, 0xEE>(),
                    .hook_kind = gc::windowed_widescreen::
                        WidescreenHookKind::mid_hook,
                },
                gc::windowed_widescreen::WidescreenByteContract{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::clip_default,
                    .rva = 0x140,
                    .pattern = gc::windowed_widescreen::BytePatternOf<
                        0x11>(),
                    .hook_kind = gc::windowed_widescreen::
                        WidescreenHookKind::read_only,
                },
            };
        std::array<gc::windowed_widescreen::WidescreenPointerContract, 1>
            pointer_contracts{
                gc::windowed_widescreen::WidescreenPointerContract{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::config_width_setter,
                    .pointer_rva = 0x160,
                    .target_rva = 0x500,
                },
            };
        std::array<gc::windowed_widescreen::WidescreenHookRequest, 2>
            requests{
                gc::windowed_widescreen::WidescreenHookRequest{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::config_apply,
                    .callback = reinterpret_cast<void*>(0x1111),
                },
                gc::windowed_widescreen::WidescreenHookRequest{
                    .site = gc::windowed_widescreen::
                        WidescreenContractSite::frame_begin,
                    .callback = reinterpret_cast<void*>(0x2222),
                },
            };
        FakeInstallEnvironment environment;

        SyntheticInstallFixture()
        {
            environment.AddBytes(image_base + 0x100, {0xAA, 0xBB});
            environment.AddBytes(image_base + 0x120, {0xCC, 0xDD, 0xEE});
            environment.AddBytes(image_base + 0x140, {0x11});
            environment.AddPointer(
                image_base + 0x160,
                static_cast<std::uint32_t>(image_base + 0x500));
        }

        [[nodiscard]] gc::windowed_widescreen::WidescreenContractManifest
        Manifest() const noexcept
        {
            return {
                .byte_contracts = byte_contracts,
                .pointer_contracts = pointer_contracts,
            };
        }
    };

    void HookTransactionPreflightsEverythingBeforeCreation()
    {
        using namespace gc::windowed_widescreen;
        SyntheticInstallFixture fixture;
        const auto result = InstallWindowedWidescreenHooks(
            fixture.image_base,
            fixture.Manifest(),
            fixture.requests,
            fixture.environment.Actions());
        Expect(result.has_value(), "synthetic hook transaction succeeds");
        Expect(
            fixture.environment.reads_before_first_create == 4,
            "all byte and pointer contracts read before first create");
        Expect(
            fixture.environment.owner_published,
            "owner publishes only after all hooks enable");
        Expect(
            fixture.environment.events == std::vector<InstallEvent>{
                {.kind = InstallEventKind::read},
                {.kind = InstallEventKind::read},
                {.kind = InstallEventKind::read},
                {.kind = InstallEventKind::read},
                {InstallEventKind::create,
                 WidescreenContractSite::config_apply},
                {InstallEventKind::create,
                 WidescreenContractSite::frame_begin},
                {InstallEventKind::enable,
                 WidescreenContractSite::config_apply},
                {InstallEventKind::enable,
                 WidescreenContractSite::frame_begin},
                {.kind = InstallEventKind::publish},
            },
            "transaction order is preflight, disabled create, enable, publish");
    }

    void HookTransactionRejectsBasePointerAndAddressErrorsEarly()
    {
        using namespace gc::windowed_widescreen;
        {
            SyntheticInstallFixture fixture;
            const auto wrong_base = InstallWindowedWidescreenHooks(
                fixture.image_base + 0x1000,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !wrong_base && wrong_base.error().stage ==
                    WidescreenInstallStage::unexpected_image_base &&
                    fixture.environment.events.empty(),
                "unexpected image base rejects before reads or hooks");
        }
        {
            SyntheticInstallFixture fixture;
            fixture.environment.memory.back().bytes[1] = std::byte{0x00};
            const auto pointer_mismatch = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !pointer_mismatch && pointer_mismatch.error().stage ==
                    WidescreenInstallStage::pointer_mismatch &&
                    fixture.environment.create_count == 0,
                "relocated pointer mismatch rejects before creation");
        }
        {
            SyntheticInstallFixture fixture;
            fixture.byte_contracts[0].rva =
                std::numeric_limits<std::uint32_t>::max();
            const auto overflow = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !overflow && overflow.error().stage ==
                    WidescreenInstallStage::address_overflow &&
                    fixture.environment.events.empty(),
                "checked address overflow rejects before memory access");
        }
    }

    void HookCreationAndEnableFailuresRollbackInReverse()
    {
        using namespace gc::windowed_widescreen;
        for (std::size_t fail_index = 0; fail_index < 2; ++fail_index)
        {
            SyntheticInstallFixture fixture;
            fixture.environment.fail_create_index = fail_index;
            const auto failed = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !failed && failed.error().stage ==
                    WidescreenInstallStage::hook_create &&
                    failed.error().rollback_attempted &&
                    failed.error().rollback_complete &&
                    !fixture.environment.owner_published,
                "failure at each create rolls back without owner publish");
            const auto reset_count = static_cast<std::size_t>(std::count_if(
                fixture.environment.events.begin(),
                fixture.environment.events.end(),
                [](const InstallEvent event)
                {
                    return event.kind == InstallEventKind::reset;
                }));
            Expect(
                reset_count == fail_index,
                "create failure resets every prior candidate once");
        }

        for (std::size_t fail_index = 0; fail_index < 2; ++fail_index)
        {
            SyntheticInstallFixture fixture;
            fixture.environment.fail_enable_index = fail_index;
            const auto failed = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !failed && failed.error().stage ==
                    WidescreenInstallStage::hook_enable &&
                    failed.error().rollback_attempted &&
                    failed.error().rollback_complete &&
                    !fixture.environment.owner_published,
                "failure at each enable rolls back without owner publish");

            std::vector<WidescreenContractSite> reset_sites;
            for (const auto event : fixture.environment.events)
            {
                if (event.kind == InstallEventKind::reset)
                {
                    reset_sites.push_back(event.site);
                }
            }
            Expect(
                reset_sites == std::vector<WidescreenContractSite>{
                    WidescreenContractSite::frame_begin,
                    WidescreenContractSite::config_apply,
                },
                "enable failure resets all candidates in reverse order");
        }
    }

    void HookRollbackReportsVerificationAndPublicationFailures()
    {
        using namespace gc::windowed_widescreen;
        {
            SyntheticInstallFixture fixture;
            fixture.environment.fail_enable_index = 0;
            fixture.environment.fail_reads_after_create = true;
            const auto failed = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !failed && failed.error().rollback_attempted &&
                    !failed.error().rollback_complete,
                "rollback verification failure remains explicit");
        }
        {
            SyntheticInstallFixture fixture;
            fixture.environment.publish_succeeds = false;
            const auto failed = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                fixture.environment.Actions());
            Expect(
                !failed && failed.error().stage ==
                    WidescreenInstallStage::owner_publish &&
                    failed.error().rollback_complete &&
                    !fixture.environment.owner_published,
            "owner publication failure rolls every hook back");
        }
    }

    void HookTransactionRejectsInvalidActionsAndCapacity()
    {
        using namespace gc::windowed_widescreen;
        {
            SyntheticInstallFixture fixture;
            auto actions = fixture.environment.Actions();
            actions.enable = nullptr;
            const auto invalid = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                fixture.requests,
                actions);
            Expect(
                !invalid && invalid.error().stage ==
                    WidescreenInstallStage::invalid_actions &&
                    fixture.environment.events.empty(),
                "missing install action rejects before memory access");
        }
        {
            SyntheticInstallFixture fixture;
            std::array<
                WidescreenHookRequest,
                kMaximumWidescreenHooks + 1> requests{};
            for (auto& request : requests)
            {
                request = fixture.requests.front();
            }
            const auto overflow = InstallWindowedWidescreenHooks(
                fixture.image_base,
                fixture.Manifest(),
                requests,
                fixture.environment.Actions());
            Expect(
                !overflow && overflow.error().stage ==
                    WidescreenInstallStage::capacity_overflow &&
                    fixture.environment.events.empty(),
                "hook capacity overflow rejects before memory access");
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
    HookTransactionPreflightsEverythingBeforeCreation();
    HookTransactionRejectsBasePointerAndAddressErrorsEarly();
    HookCreationAndEnableFailuresRollbackInReverse();
    HookRollbackReportsVerificationAndPublicationFailures();
    HookTransactionRejectsInvalidActionsAndCapacity();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
