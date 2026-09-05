#include "Patches/Framerate/FramerateTimingRuntime.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <variant>

namespace gc::framerate {
    namespace detail
    {
        [[nodiscard]] bool UsesSharedGameplaySongClock(
            const GameplayAudioClockPlan plan) noexcept
        {
            return plan == GameplayAudioClockPlan::WasapiSharedSongClock ||
                plan == GameplayAudioClockPlan::AsioQpcSongClock;
        }

        GameplaySongClockInputSelection SelectGameplaySongClockInput(
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept
        {
            const auto* presented = cursor_observation.has_value()
                                        ? std::get_if<
                                              audio::
                                              PresentedOutputCursorObservation>(
                                              &cursor_observation->payload)
                                        : nullptr;
            if (presented != nullptr &&
                presented->state ==
                audio::GameplayAudioCursorState::Inactive)
            {
                return {
                    .state = GameplaySongClockInputState::Inactive,
                };
            }

            if (group_cursor_ms >= 0)
            {
                if (presented != nullptr &&
                    presented->state ==
                    audio::GameplayAudioCursorState::Exact)
                {
                    return {
                        .state = GameplaySongClockInputState::Exact,
                        .observation = SongClockObservation{
                            .kind =
                            SongClockObservationKind::ExactSourceFrame,
                            .position =
                            presented->source_frame_unwrapped,
                            .source_sample_rate =
                            presented->source_sample_rate,
                            .buffer_instance_id =
                            presented->buffer_instance_id,
                            .playback_generation =
                            presented->playback_generation,
                        },
                        .output_frame = presented->output_frame,
                    };
                }
                return {
                    .state = GameplaySongClockInputState::Rounded,
                    .observation = SongClockObservation{
                        .kind =
                        SongClockObservationKind::RoundedMilliseconds,
                        .position =
                        static_cast<std::uint64_t>(group_cursor_ms),
                    },
                };
            }

            return {
                .state = GameplaySongClockInputState::Failed,
            };
        }

        GameplaySongClockStepSelection ResolveGameplaySongClockStep(
            GameplaySongClock& clock,
            std::uint32_t current_tick,
            std::int32_t game_time_offset_ms,
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept
        {
            GameplaySongClockStepSelection result{
                .input = SelectGameplaySongClockInput(
                    group_cursor_ms, cursor_observation),
            };
            if (!result.input.observation.has_value())
            {
                return result;
            }

            const auto decision = clock.Observe(
                current_tick,
                game_time_offset_ms,
                result.input.observation.value());
            if (!decision)
            {
                result.observation_rejected = true;
                return result;
            }

            result.decision = decision.value();
            result.step = decision->step;
            return result;
        }

        std::expected<bool, FramerateTimingProfileError>
        ShouldRunGameplayCadence(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step,
            std::int32_t phase,
            std::uint32_t authored_period) noexcept
        {
            if (UsesSharedGameplaySongClock(audio_clock_plan))
            {
                return CrossesAuthored60Cadence(
                    profile,
                    current_tick,
                    step,
                    phase,
                    authored_period);
            }
            return ShouldRunAuthored60Cadence(
                profile, current_tick, phase, authored_period);
        }

        std::expected<std::uint32_t, FramerateTimingProfileError>
        CountGameplayEffectAdvances(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step) noexcept
        {
            if (UsesSharedGameplaySongClock(audio_clock_plan))
            {
                return CountCrossedAuthored60Ticks(
                    profile, current_tick, step);
            }

            const auto boundary =
                IsAuthored60FrameBoundary(profile, current_tick);
            if (!boundary)
            {
                return std::unexpected(boundary.error());
            }
            return boundary.value() ? 1U : 0U;
        }

        std::optional<GameplayCadenceHookSemantics>
        GetGameplayCadenceHookSemantics(
            FramerateHookId id) noexcept
        {
            using Register = GameplayCadenceTestRegister;
            switch (id)
            {
            case FramerateHookId::EffectCadence6:
                return GameplayCadenceHookSemantics{6, Register::Edx, false};
            case FramerateHookId::EffectCadence5:
                return GameplayCadenceHookSemantics{5, Register::Edx, false};
            case FramerateHookId::EffectCadence4:
                return GameplayCadenceHookSemantics{4, Register::Edx, false};
            case FramerateHookId::EffectCadence16A:
                return GameplayCadenceHookSemantics{16, Register::Edx, true};
            case FramerateHookId::EffectCadence16B:
                return GameplayCadenceHookSemantics{16, Register::Ecx, true};
            case FramerateHookId::EffectCadence8:
                return GameplayCadenceHookSemantics{8, Register::Eax, true};
            default:
                return std::nullopt;
            }
        }
    } // namespace detail


namespace detail {
std::optional<FramerateRuntimeState> g_runtime;
[[nodiscard]] std::uintptr_t NativeTarget(FramerateNativeTarget id) noexcept {
    return g_runtime->native_targets[static_cast<std::size_t>(id)];
}

[[nodiscard]] bool ReadU32Safe(
    std::uintptr_t address,
    std::uint32_t& value) noexcept
{
    __try
    {
        value = *reinterpret_cast<volatile std::uint32_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool WriteU32Safe(
    std::uintptr_t address,
    std::uint32_t value) noexcept
{
    __try
    {
        *reinterpret_cast<volatile std::uint32_t*>(address) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool ReadI32StackSafe(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t& value) noexcept
{
    std::uint32_t raw{};
    if (!ReadU32Safe(context.ebp + offset, raw))
    {
        return false;
    }
    value = static_cast<std::int32_t>(raw);
    return true;
}

[[nodiscard]] std::int32_t ReadI32Stack(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t fallback) noexcept
{
    std::int32_t value{};
    if (!ReadI32StackSafe(context, offset, value))
    {
        return fallback;
    }
    return value;
}

void SetZeroFlag(safetyhook::Context& context, bool is_zero) noexcept
{
    constexpr std::uint32_t kZeroFlag = 0x40;
    if (is_zero)
    {
        context.eflags |= kZeroFlag;
    }
    else
    {
        context.eflags &= ~kZeroFlag;
    }
}

void FatalRuntimeConversion(std::string_view operation) noexcept
{
    ReportFramerateRuntimeFailure(
        operation);
}

[[nodiscard]] bool IsAuthored60HzTick() noexcept
{
    return g_runtime->authored_60hz_tick.load(std::memory_order_acquire);
}

[[nodiscard]] bool ReadTuneFrame(
    const safetyhook::Context& context,
    std::intptr_t tune_stack_offset,
    std::uint32_t& frame) noexcept
{
    std::uint32_t tune{};
    return ReadU32Safe(context.ebp + tune_stack_offset, tune) &&
        tune != 0 && ReadU32Safe(tune + g_runtime->layout.tune_current_tick, frame);
}

[[nodiscard]] bool ReadTuneFrameAndStep(
    const safetyhook::Context& context,
    std::intptr_t tune_stack_offset,
    std::uint32_t& frame,
    std::uint32_t& step) noexcept
{
    std::uint32_t tune{};
    return ReadU32Safe(context.ebp + tune_stack_offset, tune) &&
        tune != 0 &&
        ReadU32Safe(tune + g_runtime->layout.tune_current_tick, frame) &&
        ReadU32Safe(tune + g_runtime->layout.tune_step, step);
}

}
}
