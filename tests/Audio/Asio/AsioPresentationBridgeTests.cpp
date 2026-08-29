#include "Audio/Asio/AsioPresentationBridge.h"

#include "Audio/Logical/LogicalPresentedOutputClock.h"
#include "Audio/Mixer/AudioSnapshot.h"
#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <Windows.h>
#include <mmreg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using gc::audio::AsioPresentationBridge;
    using gc::audio::AsioPresentationBridgeConfig;
    using gc::audio::AsioPresentationBridgeFault;
    using gc::audio::AsioPresentationBridgeState;
    using gc::audio::AsioRenderRequest;
    using gc::audio::AudioCursorTimeline;
    using gc::audio::AudioRenderCore;
    using gc::audio::AudioSnapshot;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::LogicalPresentationClock;
    using gc::audio::LogicalPresentedOutputClock;
    using gc::audio::LogicalPresentedOutputClockActions;
    using gc::audio::LogicalRenderOwner;
    using gc::audio::LogicalRenderStream;
    using gc::audio::NormalizedSourceFormat;
    using gc::audio::VoiceUsage;

    constexpr std::uint32_t kPeriodFrames = 192;
    constexpr std::uint32_t kDriverOutputLatencyFrames = 384;
    constexpr std::uint32_t kTimestampQuantumNs = 1'000'000;
    constexpr std::uint64_t kTimelineGeneration = 71;
    constexpr std::uint64_t kPhysicalGeneration = 19;
    constexpr std::uint64_t kSimulationSeconds = 180;
    constexpr std::int32_t kDriftPpm = 250;
    constexpr std::uint64_t kFixedInputTimestampMs = 90'000;
    constexpr std::uint64_t kImpulseFrame =
        kDriverOutputLatencyFrames + 64;
    constexpr std::uint64_t kImpulseSourceFrames =
        kImpulseFrame + 8 * kPeriodFrames;

    int failures = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    struct FakeNow final
    {
        std::uint32_t raw_ms{};

        static std::uint32_t Read(void* context) noexcept
        {
            return static_cast<FakeNow*>(context)->raw_ms;
        }
    };

    struct ExactCoordinate final
    {
        std::int64_t numerator{};
        std::uint64_t denominator{};
        bool valid{};
    };

    struct ScenarioResult final
    {
        ExactCoordinate fixed_input_time{};
        std::optional<std::uint64_t> impulse_output_frame{};
    };

    NormalizedSourceFormat NativeStereoPcm16(const std::uint32_t rate)
    {
        constexpr std::uint16_t channels = 2;
        constexpr std::uint16_t bits_per_sample = 16;
        constexpr std::uint16_t block_align =
            channels * (bits_per_sample / 8);
        WAVEFORMATEX wave{
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = channels,
            .nSamplesPerSec = rate,
            .nAvgBytesPerSec = rate * block_align,
            .nBlockAlign = block_align,
            .wBitsPerSample = bits_per_sample,
            .cbSize = 0,
        };
        NormalizedSourceFormat normalized{};
        Expect(SUCCEEDED(gc::audio::NormalizeSourceFormat(
                   &wave, &normalized)),
               "production source format normalization succeeds");
        return normalized;
    }

    std::shared_ptr<AudioSnapshot> ImpulseSnapshot()
    {
        constexpr std::uint32_t channels = 2;
        constexpr std::uint32_t bytes_per_sample = 2;
        constexpr std::uint32_t block_align =
            channels * bytes_per_sample;
        constexpr auto byte_length = static_cast<std::uint32_t>(
            kImpulseSourceFrames * block_align);

        std::vector<std::int16_t> samples(
            static_cast<std::size_t>(kImpulseSourceFrames * channels));
        samples[static_cast<std::size_t>(kImpulseFrame * channels)] =
            (std::numeric_limits<std::int16_t>::max)();
        samples[static_cast<std::size_t>(
            kImpulseFrame * channels + 1)] =
            (std::numeric_limits<std::int16_t>::max)();

        auto snapshot =
            std::make_shared<AudioSnapshot>(byte_length, block_align);
        gc::audio::AudioLockRegions regions{};
        Expect(SUCCEEDED(snapshot->Lock(
                   0, byte_length, 0, &regions)),
               "impulse snapshot lock succeeds");
        const auto bytes = std::as_bytes(std::span{samples});
        if (regions.first != nullptr)
        {
            std::memcpy(
                regions.first, bytes.data(), regions.first_bytes);
        }
        if (regions.second != nullptr)
        {
            std::memcpy(
                regions.second,
                bytes.data() + regions.first_bytes,
                regions.second_bytes);
        }
        Expect(SUCCEEDED(snapshot->Unlock(
                   regions.first,
                   regions.first_bytes,
                   regions.second,
                   regions.second_bytes)),
               "impulse snapshot publication succeeds");
        return snapshot;
    }

    std::uint64_t SimulationCallbackCount(
        const std::uint32_t rate,
        const std::int32_t oscillator_error_ppm)
    {
        const long double physical_rate =
            static_cast<long double>(rate) *
            (1.0L + static_cast<long double>(oscillator_error_ppm) /
                        1'000'000.0L);
        return static_cast<std::uint64_t>(std::ceil(
            static_cast<long double>(kSimulationSeconds) *
            physical_rate /
            static_cast<long double>(kPeriodFrames)));
    }

    std::uint64_t QuantizedSystemTimeNanoseconds(
        const std::uint64_t sample_position,
        const std::uint32_t rate,
        const std::int32_t oscillator_error_ppm)
    {
        const long double physical_rate =
            static_cast<long double>(rate) *
            (1.0L + static_cast<long double>(oscillator_error_ppm) /
                        1'000'000.0L);
        const long double milliseconds =
            static_cast<long double>(sample_position) *
            1'000.0L / physical_rate;
        return static_cast<std::uint64_t>(
                   std::floor(milliseconds)) *
            kTimestampQuantumNs;
    }

    ExactCoordinate ResolveFixedInputTime(
        const LogicalPresentationClock& clock,
        const std::uint32_t logical_rate)
    {
        const auto resolved = clock.Resolve(
            {
                .qpc_ticks = 0,
                .multimedia_time_ms =
                    static_cast<std::uint32_t>(
                        kFixedInputTimestampMs),
            },
            ExactClockResolveIntent::FinalizedTimestamp);
        Expect(resolved.status == ExactClockStatus::Resolved,
               "fixed captured input resolves through logical provider");
        if (resolved.status != ExactClockStatus::Resolved ||
            !resolved.logical_output_frame)
        {
            return {};
        }

        const auto logical_seconds =
            resolved.logical_output_frame->Multiply(1, logical_rate);
        Expect(logical_seconds.has_value(),
               "resolved logical coordinate converts exactly to seconds");
        if (!logical_seconds)
        {
            return {};
        }
        return {
            .numerator = logical_seconds->numerator(),
            .denominator = logical_seconds->denominator(),
            .valid = true,
        };
    }

    ScenarioResult RunScenario(
        const std::uint32_t logical_rate,
        const std::int32_t oscillator_error_ppm,
        const bool include_impulse)
    {
        const auto logical_clock = LogicalPresentationClock::Create(
            kTimelineGeneration, 0, logical_rate, 10'000'000);
        Expect(logical_clock != nullptr,
               "production logical clock initializes");
        if (!logical_clock)
        {
            return {};
        }

        FakeNow now{};
        auto presented_clock =
            std::make_unique<LogicalPresentedOutputClock>(
                LogicalPresentedOutputClockActions{
                    .context = &now,
                    .time_get_time_ms = &FakeNow::Read,
                },
                logical_clock);

        ma_result core_result = MA_ERROR;
        auto core = AudioRenderCore::Create(
            kPeriodFrames,
            logical_rate,
            {},
            std::move(presented_clock),
            &core_result);
        Expect(core != nullptr && core_result == MA_SUCCESS,
               "production render core initializes");
        if (!core)
        {
            return {};
        }

        std::shared_ptr<AudioCursorTimeline> history;
        std::unique_ptr<gc::audio::MixerVoice> voice;
        if (include_impulse)
        {
            history = std::make_shared<AudioCursorTimeline>();
            Expect(history->AssignBufferInstanceId(29),
                   "impulse history receives a buffer identity");
            Expect(history->ConfigureExactPlaybackHistory(
                       29, kTimelineGeneration),
                   "impulse history uses the logical timeline");
            Expect(history->ExpectExactPlaybackGeneration(1),
                   "impulse history expects one playback epoch");

            ma_result voice_result = MA_ERROR;
            voice = core->CreateVoice(
                NativeStereoPcm16(logical_rate),
                ImpulseSnapshot(),
                history,
                VoiceUsage::GameplayNativeCandidate,
                &voice_result);
            Expect(voice != nullptr && voice_result == MA_SUCCESS,
                   "production impulse voice initializes");
            if (!voice)
            {
                return {};
            }
            Expect(SUCCEEDED(voice->Play(false, 1)),
                   "single impulse playback starts");
        }

        auto stream = LogicalRenderStream::Create(*core);
        Expect(stream != nullptr,
               "production logical render stream initializes");
        if (!stream)
        {
            return {};
        }
        const auto lease =
            stream->AcquireInitial(LogicalRenderOwner::AsioBridge);
        Expect(lease.has_value(),
               "bridge acquires the initial logical render lease");
        if (!lease)
        {
            return {};
        }

        auto bridge = AsioPresentationBridge::Create(
            AsioPresentationBridgeConfig{
                .physical_session_generation = kPhysicalGeneration,
                .logical_rate = logical_rate,
                .driver_rate = logical_rate,
                .period_frames = kPeriodFrames,
                .driver_output_latency_frames =
                    kDriverOutputLatencyFrames,
                .timestamp_quantum_ns = kTimestampQuantumNs,
            },
            logical_clock,
            *stream,
            {});
        Expect(bridge != nullptr,
               "production ASIO presentation bridge initializes");
        if (!bridge)
        {
            return {};
        }

        const auto armed = bridge->Arm(*lease, 0);
        Expect(armed.has_value(),
               "bridge arms only at the exact logical tail");
        if (!armed)
        {
            return {};
        }

        ScenarioResult scenario{
            .fixed_input_time =
                ResolveFixedInputTime(*logical_clock, logical_rate),
        };
        std::array<float, kPeriodFrames * 2> output{};
        bool running_committed = false;
        std::uint64_t audible_callbacks = 0;
        std::uint64_t inferred_rendered_frames = 0;
        float maximum_impulse_sample = 0.0F;

        const auto callback_count = SimulationCallbackCount(
            logical_rate, oscillator_error_ppm);
        for (std::uint64_t callback = 0;
             callback < callback_count;
             ++callback)
        {
            const auto sample_position =
                callback * kPeriodFrames;
            const auto system_time_ns =
                QuantizedSystemTimeNanoseconds(
                    sample_position,
                    logical_rate,
                    oscillator_error_ppm);
            now.raw_ms = static_cast<std::uint32_t>(
                system_time_ns / kTimestampQuantumNs);
            const auto observed =
                logical_clock->ObserveNow(now.raw_ms);
            Expect(observed.has_value(),
                   "logical clock observation remains continuous");

            std::fill(
                output.begin(),
                output.end(),
                (std::numeric_limits<float>::quiet_NaN)());
            const auto tail_before = stream->committed_tail();
            const auto processed = bridge->Process(
                AsioRenderRequest{
                    .buffer_index =
                        static_cast<long>(callback & 1U),
                    .direct_process = ASIOFalse,
                    .has_system_time = true,
                    .sample_position = sample_position,
                    .system_time_ns = system_time_ns,
                },
                output);
            const auto tail_after = stream->committed_tail();

            Expect(tail_after >= tail_before,
                   "logical render tail never moves backwards");
            Expect((tail_after - tail_before) %
                       kPeriodFrames ==
                   0,
                   "each callback commits only complete logical blocks");
            inferred_rendered_frames +=
                tail_after - tail_before;

            if (running_committed)
            {
                Expect(processed.audible,
                       "every callback after running commit is audible");
                Expect(processed.output_frames == kPeriodFrames,
                       "every running callback produces one full period");
            }
            if (processed.audible)
            {
                running_committed = true;
                ++audible_callbacks;
            }

            Expect(std::all_of(
                       output.begin(),
                       output.end(),
                       [](const float sample)
                       {
                           return std::isfinite(sample);
                       }),
                   "bridge overwrites the complete output with finite samples");

            if (include_impulse)
            {
                for (std::uint32_t frame = 0;
                     frame < kPeriodFrames;
                     ++frame)
                {
                    const auto sample = std::max(
                        std::abs(output[frame * 2]),
                        std::abs(output[frame * 2 + 1]));
                    if (sample > maximum_impulse_sample)
                    {
                        maximum_impulse_sample = sample;
                        scenario.impulse_output_frame =
                            callback * kPeriodFrames + frame;
                    }
                }
            }
        }

        const auto snapshot = bridge->Snapshot();
        Expect(running_committed && audible_callbacks != 0,
               "bridge commits running after aligned priming");
        Expect(snapshot.state == AsioPresentationBridgeState::Running,
               "bridge remains running for the complete simulation");
        Expect(snapshot.first_fault == AsioPresentationBridgeFault::None,
               "bridge latches no structural or conversion fault");
        Expect(snapshot.running_callbacks == audible_callbacks,
               "running callback diagnostics match observed output");
        Expect(snapshot.logical_rendered_frames ==
                   inferred_rendered_frames,
               "bridge diagnostics equal independently observed tail movement");
        Expect(stream->committed_tail() ==
                   inferred_rendered_frames,
               "all logical render origins are contiguous from zero");
        Expect(snapshot.input_underflows == 0,
               "final-output FIFO never underflows");
        Expect(snapshot.input_overflows == 0,
               "final-output FIFO never overflows");
        Expect(snapshot.conversion_failures == 0,
               "final-output rate conversion never fails");
        Expect(snapshot.phase_envelope_violations == 0,
               "running phase never leaves production policy");
        Expect(snapshot.maximum_absolute_phase_error_frames <=
                   snapshot.phase_envelope_frames,
               "observed phase remains inside the independent envelope");
        Expect(std::abs(snapshot.final_rate_ratio_ppm) <=
                   gc::audio::kMaximumRateCorrectionPpm,
               "final rate correction stays inside production policy");
        Expect(snapshot.minimum_rate_ratio_ppm >=
                   -gc::audio::kMaximumRateCorrectionPpm &&
                   snapshot.maximum_rate_ratio_ppm <=
                       gc::audio::kMaximumRateCorrectionPpm,
               "all rate corrections stay inside production policy");
        Expect(
            oscillator_error_ppm > 0
                ? snapshot.final_rate_ratio_ppm < 0.0
                : snapshot.final_rate_ratio_ppm > 0.0,
            "rate matcher moves opposite the physical oscillator error");

        if (include_impulse)
        {
            Expect(maximum_impulse_sample > 0.5F,
                   "tagged impulse reaches physical output");
            // Independent miniaudio linear-resampler model:
            // input K appears at raw output K + one input-latency frame.
            // Priming advances driver latency + that one group-delay frame.
            // Therefore the captured driver buffer contains it at K - latency.
            constexpr auto expected_impulse_output_frame =
                kImpulseFrame - kDriverOutputLatencyFrames;
            Expect(scenario.impulse_output_frame ==
                       expected_impulse_output_frame,
                   "driver latency and resampler group delay are compensated exactly once");
        }

        return scenario;
    }

    void ProductionBridgeTracksBothOscillatorDirections()
    {
        const std::array scenarios{
            RunScenario(44'100, kDriftPpm, false),
            RunScenario(44'100, -kDriftPpm, false),
            RunScenario(48'000, kDriftPpm, true),
            RunScenario(48'000, -kDriftPpm, false),
        };

        Expect(std::all_of(
                   scenarios.begin(),
                   scenarios.end(),
                   [](const ScenarioResult& scenario)
                   {
                       return scenario.fixed_input_time.valid;
                   }),
               "all physical simulations resolve the captured input");
        if (!scenarios.front().fixed_input_time.valid)
        {
            return;
        }
        for (const auto& scenario : scenarios)
        {
            Expect(
                scenario.fixed_input_time.numerator ==
                        scenarios.front().fixed_input_time.numerator &&
                    scenario.fixed_input_time.denominator ==
                        scenarios.front().fixed_input_time.denominator,
                "fixed captured input is bit-identical across physical clocks");
        }
    }
} // namespace

int main()
{
    ProductionBridgeTracksBothOscillatorDirections();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
