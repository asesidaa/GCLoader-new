#include "MiniaudioMixer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioCursorResolution;
using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioLockRegions;
using gc::audio::AudioSnapshot;
using gc::audio::MiniaudioMixer;
using gc::audio::MixerRenderTimeline;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;
using gc::audio::kFallbackEndpointSampleRate;
using gc::audio::kGamePrimarySampleRate;

constexpr std::uint32_t kPeriodFrames = 8;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << '\n';
    return 1;
}

int ExpectNear(float actual, float expected, std::string_view name) {
    return Expect(std::abs(actual - expected) < 0.015F, name);
}

bool ResolvesTo(
    const AudioCursorResolution& resolution,
    std::uint64_t source_frame) noexcept {
    return resolution.kind == AudioCursorResolutionKind::Resolved &&
        resolution.source_frame == source_frame;
}

bool DoesNotResolve(const AudioCursorResolution& resolution) noexcept {
    return resolution.kind != AudioCursorResolutionKind::Resolved;
}

WAVEFORMATEX Pcm(WORD channels, DWORD rate, WORD bits) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = bits;
    format.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    return format;
}

std::vector<std::byte> Pcm16Bytes(
    std::initializer_list<std::int16_t> samples) {
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.begin(), bytes.size());
    return bytes;
}

std::vector<std::byte> Pcm16Bytes(
    const std::vector<std::int16_t>& samples) {
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<std::byte> Pcm24Bytes(
    std::initializer_list<std::int32_t> samples) {
    std::vector<std::byte> bytes(samples.size() * 3);
    std::size_t offset = 0;
    for (const auto sample : samples) {
        const auto encoded = static_cast<std::uint32_t>(sample);
        bytes[offset++] = static_cast<std::byte>(encoded & 0xFFU);
        bytes[offset++] = static_cast<std::byte>((encoded >> 8U) & 0xFFU);
        bytes[offset++] = static_cast<std::byte>((encoded >> 16U) & 0xFFU);
    }
    return bytes;
}

struct TestSource {
    TestSource(
        const NormalizedSourceFormat& source_format,
        std::span<const std::byte> bytes)
        : format(source_format),
          snapshot(std::make_shared<AudioSnapshot>(
              static_cast<std::uint32_t>(bytes.size()),
              source_format.block_align)),
          timeline(std::make_shared<AudioCursorTimeline>()) {
        AudioLockRegions regions{};
        const auto lock_result = snapshot->Lock(
            0,
            static_cast<DWORD>(bytes.size()),
            DSBLOCK_ENTIREBUFFER,
            &regions);
        initialized = lock_result == DS_OK;
        if (!initialized) {
            return;
        }

        std::memcpy(regions.first, bytes.data(), regions.first_bytes);
        if (regions.second_bytes != 0) {
            std::memcpy(
                regions.second,
                bytes.data() + regions.first_bytes,
                regions.second_bytes);
        }
        initialized = snapshot->Unlock(
            regions.first,
            regions.first_bytes,
            regions.second,
            regions.second_bytes) == DS_OK;
    }

    NormalizedSourceFormat format{};
    std::shared_ptr<AudioSnapshot> snapshot;
    std::shared_ptr<AudioCursorTimeline> timeline;
    bool initialized{};
};

std::unique_ptr<TestSource> MakeSource(
    const WAVEFORMATEX& wave,
    std::span<const std::byte> bytes,
    int& failures,
    std::string_view name) {
    NormalizedSourceFormat normalized{};
    failures += Expect(
        gc::audio::NormalizeSourceFormat(&wave, &normalized) == DS_OK,
        name);
    auto source = std::make_unique<TestSource>(normalized, bytes);
    failures += Expect(source->initialized, "synthetic snapshot publication");
    return source;
}

std::unique_ptr<MixerVoice> MakeVoice(
    MiniaudioMixer& mixer,
    TestSource& source,
    VoiceUsage usage,
    int& failures,
    std::string_view name) {
    ma_result result = MA_ERROR;
    auto voice = mixer.CreateVoice(
        source.format,
        source.snapshot,
        source.timeline,
        usage,
        &result);
    failures += Expect(
        result == MA_SUCCESS && voice != nullptr,
        name);
    return voice;
}

struct AllocationProbe {
    std::atomic_bool enabled{};
    std::atomic_uint64_t callback_count{};

    static void* Allocate(std::size_t size, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.callback_count.fetch_add(1, std::memory_order_relaxed);
        }
        return std::malloc(size == 0 ? 1 : size);
    }

    static void* Reallocate(
        void* pointer,
        std::size_t size,
        void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.callback_count.fetch_add(1, std::memory_order_relaxed);
        }
        return std::realloc(pointer, size == 0 ? 1 : size);
    }

    static void Free(void* pointer, void* user_data) {
        auto& probe = *static_cast<AllocationProbe*>(user_data);
        if (probe.enabled.load(std::memory_order_relaxed)) {
            probe.callback_count.fetch_add(1, std::memory_order_relaxed);
        }
        std::free(pointer);
    }

    ma_allocation_callbacks Callbacks() noexcept {
        return {this, Allocate, Reallocate, Free};
    }

    void Begin() noexcept {
        callback_count.store(0, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_seq_cst);
    }

    std::uint64_t End() noexcept {
        enabled.store(false, std::memory_order_seq_cst);
        return callback_count.load(std::memory_order_relaxed);
    }
};

struct OwnershipAllocationProbe {
    std::array<void*, 256> live{};
    std::size_t live_count{};
    bool invalid_release{};

    static void* Allocate(std::size_t size, void* user_data) {
        auto& probe = *static_cast<OwnershipAllocationProbe*>(user_data);
        void* pointer = std::malloc(size == 0 ? 1 : size);
        if (pointer == nullptr || probe.live_count == probe.live.size()) {
            return pointer;
        }
        probe.live[probe.live_count++] = pointer;
        return pointer;
    }

    static void* Reallocate(
        void* pointer,
        std::size_t size,
        void* user_data) {
        auto& probe = *static_cast<OwnershipAllocationProbe*>(user_data);
        const auto replacement = std::realloc(pointer, size == 0 ? 1 : size);
        if (replacement == nullptr) {
            return nullptr;
        }
        if (pointer == nullptr) {
            if (probe.live_count < probe.live.size()) {
                probe.live[probe.live_count++] = replacement;
            }
            return replacement;
        }
        for (std::size_t index = 0; index < probe.live_count; ++index) {
            if (probe.live[index] == pointer) {
                probe.live[index] = replacement;
                return replacement;
            }
        }
        probe.invalid_release = true;
        return replacement;
    }

    static void Free(void* pointer, void* user_data) {
        auto& probe = *static_cast<OwnershipAllocationProbe*>(user_data);
        for (std::size_t index = 0; index < probe.live_count; ++index) {
            if (probe.live[index] == pointer) {
                probe.live[index] = probe.live[--probe.live_count];
                std::free(pointer);
                return;
            }
        }
        probe.invalid_release = pointer != nullptr;
        std::free(pointer);
    }

    ma_allocation_callbacks Callbacks() noexcept {
        return {this, Allocate, Reallocate, Free};
    }
};

int ExpectRender(
    MiniaudioMixer& mixer,
    std::span<float> output,
    std::uint64_t output_frame_begin,
    std::string_view name) {
    const auto rendered = mixer.Render(
        output,
        MixerRenderTimeline{output_frame_begin, 0});
    return Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == kPeriodFrames,
        name);
}

int ExpectRender(
    MiniaudioMixer& mixer,
    std::span<float> output,
    const MixerRenderTimeline& timeline,
    std::string_view name) {
    const auto rendered = mixer.Render(output, timeline);
    return Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == kPeriodFrames,
        name);
}

int ExpectStereoMono(std::span<const float> output, std::string_view name) {
    for (std::size_t frame = 0; frame < output.size() / 2; ++frame) {
        if (std::abs(output[frame * 2] - output[frame * 2 + 1]) >=
            0.000001F) {
            return Expect(false, name);
        }
    }
    return 0;
}

int ExpectExceptionalLinear(
    std::span<const float> output,
    std::uint32_t source_rate,
    bool positive,
    std::string_view name) {
    int failures = 0;
    failures += ExpectStereoMono(output, name);
    failures += ExpectNear(output[0], 0.0F, name);

    const auto first_source = positive
        ? 24000.0F / 32768.0F
        : -28000.0F / 32768.0F;
    std::size_t monotonic_begin = 1;
    if (source_rate == 22050) {
        failures += ExpectNear(output[2], first_source * 0.5F, name);
        monotonic_begin = 2;
    } else {
        failures += ExpectNear(output[2], first_source, name);
    }

    for (std::size_t frame = monotonic_begin;
         frame < output.size() / 2;
         ++frame) {
        const auto sample = output[frame * 2];
        failures += Expect(
            positive ? sample > 0.0F : sample < 0.0F,
            name);
        if (frame > monotonic_begin) {
            failures += Expect(
                sample + 0.000001F >= output[(frame - 1) * 2],
                name);
        }
    }
    return failures;
}

int ExpectContinuedExceptionalLinear(
    std::span<const float> output,
    bool positive,
    std::string_view name) {
    int failures = ExpectStereoMono(output, name);
    for (std::size_t frame = 0; frame < output.size() / 2; ++frame) {
        const auto sample = output[frame * 2];
        const auto in_region = positive ? sample > 0.0F : sample < 0.0F;
        failures += Expect(in_region, name);
        if (frame != 0) {
            failures += Expect(
                sample + 0.000001F >= output[(frame - 1) * 2],
                name);
        }
    }
    return failures;
}

std::vector<std::int16_t> RadicalRegions(
    std::size_t split,
    std::size_t count) {
    std::vector<std::int16_t> samples(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (index < split) {
            samples[index] = static_cast<std::int16_t>(
                -28000 + static_cast<int>(index) * 80);
        } else {
            samples[index] = static_cast<std::int16_t>(
                24000 + static_cast<int>(index - split) * 80);
        }
    }
    return samples;
}

std::vector<std::int16_t> IdentifiedSamples(std::size_t count) {
    std::vector<std::int16_t> samples(count);
    for (std::size_t index = 0; index < count; ++index) {
        samples[index] = static_cast<std::int16_t>((index + 1) * 400);
    }
    return samples;
}

float IdentifiedSample(std::size_t index) {
    return static_cast<float>((index + 1) * 400) / 32768.0F;
}

int TestVoiceRetainsSourceOwners() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "source-owner mixer creation");

    const auto bytes = Pcm16Bytes({
        16384, 8192, 4096, 2048,
        1024, 512, 256, 128,
    });
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "source-owner PCM16 normalization");
    std::weak_ptr<AudioSnapshot> snapshot_weak = source->snapshot;
    std::weak_ptr<AudioCursorTimeline> timeline_weak = source->timeline;
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "source-owner voice creation");
    source.reset();

    failures += Expect(
        !snapshot_weak.expired() && !timeline_weak.expired(),
        "voice keeps snapshot and timeline alive after caller release");
    failures += Expect(
        voice->Play(false, 70) == DS_OK,
        "retained-source voice play");
    std::vector<float> output(kPeriodFrames * 2);
    failures += ExpectRender(
        *mixer,
        output,
        700,
        "retained-source render after caller release");
    failures += ExpectNear(
        output[0],
        0.5F,
        "retained snapshot supplies audio");
    {
        const auto timeline = timeline_weak.lock();
        failures += Expect(
            timeline != nullptr &&
                ResolvesTo(timeline->ResolveSourceFrame(700, 70, 8), 0),
            "retained timeline receives render span");
    }

    voice.reset();
    failures += Expect(
        snapshot_weak.expired() && timeline_weak.expired(),
        "source owners release after voice destruction");
    return failures;
}

int TestVoiceRetainsMixerState() {
    int failures = 0;
    OwnershipAllocationProbe probe;
    const auto callbacks = probe.Callbacks();
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames,
        kGamePrimarySampleRate,
        &callbacks,
        &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "mixer-owner mixer creation");

    const auto bytes = Pcm16Bytes({16384, 8192, 4096, 2048});
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "mixer-owner PCM16 normalization");
    std::weak_ptr<AudioSnapshot> snapshot_weak = source->snapshot;
    std::weak_ptr<AudioCursorTimeline> timeline_weak = source->timeline;
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "mixer-owner voice creation");
    failures += Expect(
        voice->Play(true, 80) == DS_OK,
        "mixer-owner voice play");
    source.reset();

    const auto live_before_public_mixer_release = probe.live_count;
    mixer.reset();
    failures += Expect(
        live_before_public_mixer_release != 0 &&
            probe.live_count == live_before_public_mixer_release &&
            !probe.invalid_release,
        "voice retains engine allocations after public mixer destruction");
    failures += Expect(
        !snapshot_weak.expired() && !timeline_weak.expired(),
        "voice retains source owners with public mixer gone");

    voice->Stop();
    voice.reset();
    failures += Expect(
        probe.live_count == 0 && !probe.invalid_release,
        "final voice release destroys each miniaudio allocation exactly once");
    failures += Expect(
        snapshot_weak.expired() && timeline_weak.expired(),
        "final voice release destroys retained source state");
    return failures;
}

int TestConcurrentVoiceStateAccounting() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "concurrent-state mixer creation");
    const auto bytes = Pcm16Bytes({
        16384, 8192, 4096, 2048,
        1024, 512, 256, 128,
    });
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "concurrent-state PCM16 normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "concurrent-state voice creation");

    constexpr std::uint32_t iterations = 100000;
    std::atomic_uint32_t ready{};
    std::atomic_uint32_t done{};
    std::atomic_bool start{};
    std::atomic_bool failed{};
    const auto controller = [&](std::uint64_t epoch_base) {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint32_t index = 0; index < iterations; ++index) {
            if (voice->Play(false, epoch_base + index) != DS_OK) {
                failed.store(true, std::memory_order_relaxed);
            }
            if ((index & 7U) == 0) {
                std::this_thread::yield();
            }
            voice->Stop();
        }
        done.fetch_add(1, std::memory_order_release);
    };

    std::thread first(controller, 1000);
    std::thread second(controller, 1000000);
    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    std::vector<float> output(kPeriodFrames * 2);
    std::uint64_t output_frame{};
    while (done.load(std::memory_order_acquire) != 2) {
        const auto rendered = mixer->Render(
            output,
            MixerRenderTimeline{output_frame, 0});
        output_frame += kPeriodFrames;
        const auto diagnostics = mixer->diagnostics();
        if (rendered.result != MA_SUCCESS ||
            diagnostics.active_voices > 1 ||
            diagnostics.maximum_simultaneous_voices > 1) {
            failed.store(true, std::memory_order_relaxed);
        }
    }
    first.join();
    second.join();
    voice->Stop();

    const auto diagnostics = mixer->diagnostics();
    failures += Expect(
        !failed.load(std::memory_order_relaxed),
        "concurrent play/stop/end never overflows one-voice accounting");
    failures += Expect(
        diagnostics.active_voices == 0 &&
            diagnostics.maximum_simultaneous_voices == 1 &&
            !voice->playing(),
        "concurrent voice finishes in one coherent stopped state");
    return failures;
}

int TestStaleRenderCannotEndNewPlaybackRun() {
    int failures = 0;
    gc::audio::detail::VoicePlaybackStateMachine playback;
    std::uint32_t active{};

    const auto first = playback.BeginPlay();
    active += first.needs_active_increment ? 1U : 0U;
    playback.CommitPlay(first.run_token);
    const auto stale_render_run = playback.CapturePlayingRun();

    const auto stopped_run = playback.BeginStop();
    failures += Expect(
        stopped_run == first.run_token,
        "first playback run enters its own stop transition");
    if (stopped_run != 0) {
        --active;
        playback.CompleteStop(stopped_run);
    }

    const auto second = playback.BeginPlay();
    active += second.needs_active_increment ? 1U : 0U;
    playback.CommitPlay(second.run_token);

    const auto stale_end_won = playback.BeginEnd(stale_render_run);
    failures += Expect(
        !stale_end_won && active == 1 &&
            playback.CapturePlayingRun() == second.run_token,
        "stale render completion cannot end or decrement the new run");

    const auto current_end_won = playback.BeginEnd(second.run_token);
    if (current_end_won) {
        --active;
        playback.CompleteEnd(second.run_token);
    }
    failures += Expect(
        current_end_won && active == 0 && !playback.playing(),
        "current render completion ends exactly its own playback run");
    return failures;
}

int TestAudibleDrainPublicationRejectsStaleRunAndEpoch() {
    gc::audio::detail::AudibleDrainPublication publication;
    gc::audio::detail::VoicePlaybackStateMachine playback;
    int failures = 0;

    const auto old_play = playback.BeginPlay();
    playback.CommitPlay(old_play.run_token);
    failures += Expect(
        playback.BeginEnd(old_play.run_token),
        "old playback run enters ending state");
    playback.CompleteEnd(old_play.run_token);
    const auto replay = playback.BeginPlay();
    playback.CommitPlay(replay.run_token);

    publication.Publish({120, old_play.run_token, 3});
    failures += Expect(
        replay.run_token != old_play.run_token &&
            !publication.Observe(replay.run_token, 3).has_value(),
        "old ending run published after replay is unobservable");

    publication.Publish({180, replay.run_token, 3});
    failures += Expect(
        publication.Observe(replay.run_token, 3) == 180,
        "matching run and epoch observe coherent drain boundary");

    publication.Publish({220, replay.run_token, 4});
    failures += Expect(
        !publication.Observe(replay.run_token, 5).has_value(),
        "old seek epoch published after accepted seek is unobservable");
    publication.Publish({240, replay.run_token, 5});
    failures += Expect(
        publication.Observe(replay.run_token, 5) == 240,
        "new seek epoch final span becomes observable");

    failures += Expect(
        playback.BeginEnd(replay.run_token),
        "terminal drain run enters ending state");
    playback.CompleteEnd(replay.run_token);
    failures += Expect(
        playback.CaptureDrainingRun() == replay.run_token &&
            publication.Observe(playback.CaptureDrainingRun(), 5) == 240,
        "ended playback exposes only its terminal drain run");
    const auto stopped_run = playback.BeginStop();
    failures += Expect(
        stopped_run == 0 && playback.CaptureDrainingRun() == 0 &&
            !publication.Observe(playback.CaptureDrainingRun(), 5).has_value(),
        "stopped playback invalidates current drain record");
    return failures;
}

int TestConcurrentSeeksKeepTerminalDrainObservable() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "concurrent-seek mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }
    const auto bytes = Pcm16Bytes({
        16384, 8192, 4096, 2048,
        1024, 512, 256, 128,
    });
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "concurrent-seek PCM16 normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "concurrent-seek voice creation");
    if (voice == nullptr) {
        return failures + 1;
    }

    constexpr std::uint32_t worker_count = 8;
    constexpr std::uint32_t iterations = 50'000;
    std::barrier start_round(worker_count + 1);
    std::barrier finish_round(worker_count + 1);
    std::atomic_bool seek_failed{};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::uint32_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&, worker] {
            for (std::uint32_t round = 0; round < iterations; ++round) {
                start_round.arrive_and_wait();
                const auto epoch = std::uint64_t{2} +
                    static_cast<std::uint64_t>(round) * worker_count + worker;
                if (voice->Seek(worker, epoch) != DS_OK) {
                    seek_failed.store(true, std::memory_order_relaxed);
                }
                finish_round.arrive_and_wait();
            }
        });
    }

    std::vector<float> output(kPeriodFrames * 2);
    bool every_terminal_drain_observable = true;
    std::uint64_t output_frame{};
    for (std::uint32_t round = 0; round < iterations; ++round) {
        const auto play_epoch = std::uint64_t{2} +
            static_cast<std::uint64_t>(round) * worker_count;
        if (voice->Play(false, play_epoch) != DS_OK) {
            seek_failed.store(true, std::memory_order_relaxed);
        }
        start_round.arrive_and_wait();
        finish_round.arrive_and_wait();
        const auto rendered = mixer->Render(
            output,
            MixerRenderTimeline{output_frame, 0});
        output_frame += kPeriodFrames;
        if (rendered.result != MA_SUCCESS ||
            rendered.frames_read != kPeriodFrames ||
            !voice->at_end() ||
            !voice->audible_until_output_frame().has_value()) {
            every_terminal_drain_observable = false;
        }
    }
    for (auto& worker : workers) {
        worker.join();
    }

    failures += Expect(
        !seek_failed.load(std::memory_order_relaxed),
        "all concurrent seeks are accepted");
    failures += Expect(
        every_terminal_drain_observable,
        "concurrent seeks preserve the final audible drain observation");
    return failures;
}

int TestNativeLoopAdvancesAcrossDiscontinuity() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "native-gap mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }

    const auto bytes = Pcm16Bytes(IdentifiedSamples(32));
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "native-gap source normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "native-gap voice creation");
    if (voice == nullptr) {
        return failures + 1;
    }

    std::vector<float> output(kPeriodFrames * 2);
    failures += Expect(
        voice->Play(true, 100) == DS_OK,
        "native-gap looping play");
    failures += ExpectRender(
        *mixer,
        output,
        100,
        "native-gap initial render");
    failures += ExpectNear(
        output[0],
        IdentifiedSample(0),
        "native-gap initial source frame");

    failures += ExpectRender(
        *mixer,
        output,
        MixerRenderTimeline{116, 8},
        "native-gap discontinuous render");
    failures += ExpectNear(
        output[0],
        IdentifiedSample(16),
        "native-gap block starts after skipped source frames");
    failures += Expect(
        ResolvesTo(
            source->timeline->ResolveSourceFrame(108, 100, 32), 8) &&
            ResolvesTo(
                source->timeline->ResolveSourceFrame(115, 100, 32), 15) &&
            ResolvesTo(
                source->timeline->ResolveSourceFrame(116, 100, 32), 16),
        "native-gap timeline covers skipped and rendered intervals");
    return failures;
}

int TestNonLoopingVoiceEndsInsideDiscontinuity() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "gap-end mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }

    const auto bytes = Pcm16Bytes(IdentifiedSamples(12));
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "gap-end source normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "gap-end voice creation");
    if (voice == nullptr) {
        return failures + 1;
    }

    std::vector<float> output(kPeriodFrames * 2);
    failures += Expect(
        voice->Play(false, 101) == DS_OK,
        "gap-end nonlooping play");
    failures += ExpectRender(
        *mixer,
        output,
        100,
        "gap-end initial render");
    failures += ExpectRender(
        *mixer,
        output,
        MixerRenderTimeline{116, 8},
        "gap-end discontinuous render");
    failures += Expect(
        std::all_of(output.begin(), output.end(), [](float sample) {
            return sample == 0.0F;
        }),
        "gap-end current block is silent");
    failures += Expect(
        voice->at_end() && !voice->playing() &&
            voice->audible_until_output_frame() == 112,
        "gap-end drain boundary is inside skipped interval");
    failures += Expect(
        ResolvesTo(
            source->timeline->ResolveSourceFrame(111, 101, 12), 11) &&
            DoesNotResolve(
                source->timeline->ResolveSourceFrame(112, 101, 12)),
        "gap-end timeline stops at source end");
    return failures;
}

int TestLoopWrapsAcrossDiscontinuity() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "gap-wrap mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }

    const auto bytes = Pcm16Bytes(IdentifiedSamples(10));
    auto source = MakeSource(
        Pcm(1, 44100, 16),
        bytes,
        failures,
        "gap-wrap source normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "gap-wrap voice creation");
    if (voice == nullptr) {
        return failures + 1;
    }

    std::vector<float> output(kPeriodFrames * 2);
    failures += Expect(
        voice->Play(true, 102) == DS_OK,
        "gap-wrap looping play");
    failures += ExpectRender(*mixer, output, 100, "gap-wrap initial render");
    failures += ExpectRender(
        *mixer,
        output,
        MixerRenderTimeline{116, 8},
        "gap-wrap discontinuous render");
    failures += ExpectNear(
        output[0],
        IdentifiedSample(6),
        "gap-wrap block starts at wrapped source frame");
    failures += Expect(
        ResolvesTo(
            source->timeline->ResolveSourceFrame(108, 102, 10), 8) &&
            ResolvesTo(
                source->timeline->ResolveSourceFrame(110, 102, 10), 0) &&
            ResolvesTo(
                source->timeline->ResolveSourceFrame(116, 102, 10), 6),
        "gap-wrap timeline advances modulo source length");
    return failures;
}

int TestConvertedRatesUseCumulativeGapMapping() {
    int failures = 0;
    for (const auto rate : {22050U, 48000U}) {
        ma_result result = MA_ERROR;
        auto mixer = MiniaudioMixer::Create(
            kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
        failures += Expect(
            result == MA_SUCCESS && mixer != nullptr,
            "converted-gap mixer creation");
        if (mixer == nullptr) {
            ++failures;
            continue;
        }

        const auto bytes = Pcm16Bytes(IdentifiedSamples(64));
        auto source = MakeSource(
            Pcm(1, rate, 16),
            bytes,
            failures,
            "converted-gap source normalization");
        auto voice = MakeVoice(
            *mixer,
            *source,
            VoiceUsage::General,
            failures,
            "converted-gap voice creation");
        if (voice == nullptr) {
            ++failures;
            continue;
        }

        std::vector<float> output(kPeriodFrames * 2);
        const auto epoch = static_cast<std::uint64_t>(rate);
        failures += Expect(
            voice->Play(true, epoch) == DS_OK,
            "converted-gap looping play");
        failures += ExpectRender(
            *mixer,
            output,
            100,
            "converted-gap initial render");
        failures += ExpectRender(
            *mixer,
            output,
            MixerRenderTimeline{111, 3},
            "converted-gap first discontinuity");
        failures += ExpectRender(
            *mixer,
            output,
            MixerRenderTimeline{122, 3},
            "converted-gap second discontinuity");

        const auto expected = rate == 22050U ? 11U : 23U;
        failures += Expect(
            ResolvesTo(
                source->timeline->ResolveSourceFrame(122, epoch, 64),
                expected),
            "converted gaps retain cumulative fractional phase");
    }
    return failures;
}

int TestDiscontinuityResetsConverterHistory() {
    int failures = 0;
    ma_result result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "gap-reset mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }

    const auto bytes = Pcm16Bytes(RadicalRegions(16, 32));
    auto source = MakeSource(
        Pcm(1, 22050, 16),
        bytes,
        failures,
        "gap-reset source normalization");
    auto voice = MakeVoice(
        *mixer,
        *source,
        VoiceUsage::General,
        failures,
        "gap-reset voice creation");
    if (voice == nullptr) {
        return failures + 1;
    }

    std::vector<float> output(kPeriodFrames * 2);
    failures += Expect(
        voice->Play(false, 103) == DS_OK,
        "gap-reset play");
    failures += ExpectRender(*mixer, output, 100, "gap-reset initial render");
    failures += ExpectRender(
        *mixer,
        output,
        MixerRenderTimeline{132, 24},
        "gap-reset discontinuous render");
    failures += ExpectExceptionalLinear(
        output,
        22050,
        true,
        "gap-reset emits only new positive converter history");
    failures += Expect(
        ResolvesTo(
            source->timeline->ResolveSourceFrame(132, 103, 32), 16),
        "gap-reset block timeline begins at direct mapped position");
    return failures;
}

int TestExplicitGenerationWinsOverDiscontinuity() {
    int failures = 0;
    for (const bool replay : {false, true}) {
        ma_result result = MA_ERROR;
        auto mixer = MiniaudioMixer::Create(
            kPeriodFrames, kGamePrimarySampleRate, nullptr, &result);
        failures += Expect(
            result == MA_SUCCESS && mixer != nullptr,
            "gap-precedence mixer creation");
        if (mixer == nullptr) {
            ++failures;
            continue;
        }

        const auto bytes = Pcm16Bytes(IdentifiedSamples(32));
        auto source = MakeSource(
            Pcm(1, 44100, 16),
            bytes,
            failures,
            "gap-precedence source normalization");
        auto voice = MakeVoice(
            *mixer,
            *source,
            VoiceUsage::General,
            failures,
            "gap-precedence voice creation");
        if (voice == nullptr) {
            ++failures;
            continue;
        }

        std::vector<float> output(kPeriodFrames * 2);
        failures += Expect(
            voice->Play(true, 104) == DS_OK,
            "gap-precedence initial play");
        failures += ExpectRender(
            *mixer,
            output,
            100,
            "gap-precedence initial render");

        constexpr std::uint64_t new_epoch = 105;
        const auto control_result = replay
            ? voice->Play(true, new_epoch)
            : voice->Seek(3, new_epoch);
        failures += Expect(
            control_result == DS_OK,
            "gap-precedence control accepted");
        failures += ExpectRender(
            *mixer,
            output,
            MixerRenderTimeline{116, 8},
            "gap-precedence discontinuous render");

        const auto expected_source = replay ? 8U : 3U;
        failures += ExpectNear(
            output[0],
            IdentifiedSample(expected_source),
            "gap-precedence explicit source anchor wins");
        failures += Expect(
            ResolvesTo(
                source->timeline->ResolveSourceFrame(
                    116,
                    new_epoch,
                    32),
                expected_source),
            "gap-precedence new generation begins without old gap");
    }
    return failures;
}

int TestRuntimeOutputRateContract() {
    int failures = 0;
    ma_result result = MA_SUCCESS;
    auto zero_rate = MiniaudioMixer::Create(
        kPeriodFrames, 0, nullptr, &result);
    failures += Expect(
        result == MA_INVALID_ARGS && zero_rate == nullptr,
        "zero output rate rejection");

    result = MA_SUCCESS;
    auto unsupported_rate = MiniaudioMixer::Create(
        kPeriodFrames, 96'000, nullptr, &result);
    failures += Expect(
        result == MA_INVALID_ARGS && unsupported_rate == nullptr,
        "unsupported output rate rejection");

    constexpr std::uint32_t period_frames = 480;
    result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        period_frames,
        kFallbackEndpointSampleRate,
        nullptr,
        &result);
    failures += Expect(
        result == MA_SUCCESS && mixer != nullptr,
        "48 kHz mixer creation");
    if (mixer == nullptr) {
        return failures + 1;
    }

    const auto game_bytes = Pcm16Bytes(
        std::vector<std::int16_t>(kGamePrimarySampleRate, 16'384));
    auto game_source = MakeSource(
        Pcm(1, kGamePrimarySampleRate, 16),
        game_bytes,
        failures,
        "48 kHz mixer game source normalization");
    const auto endpoint_bytes = Pcm16Bytes(
        std::vector<std::int16_t>(period_frames, 8'192));
    auto endpoint_source = MakeSource(
        Pcm(1, kFallbackEndpointSampleRate, 16),
        endpoint_bytes,
        failures,
        "48 kHz mixer native source normalization");
    auto game_voice = MakeVoice(
        *mixer,
        *game_source,
        VoiceUsage::GameplayNativeCandidate,
        failures,
        "48 kHz mixer game voice creation");
    auto endpoint_voice = MakeVoice(
        *mixer,
        *endpoint_source,
        VoiceUsage::General,
        failures,
        "48 kHz mixer native voice creation");
    if (game_voice == nullptr || endpoint_voice == nullptr) {
        return failures + 1;
    }

    const auto diagnostics = mixer->diagnostics();
    failures += Expect(
        diagnostics.native_rate_buffers == 1 &&
            diagnostics.sample_rate_converted_buffers == 1,
        "48 kHz mixer classifies rates against its output rate");
    failures += Expect(
        diagnostics.native_gameplay_buffers == 1,
        "44.1 kHz PCM16 remains game-native on a 48 kHz mixer");

    constexpr std::uint64_t playback_epoch = 500;
    failures += Expect(
        game_voice->Play(true, playback_epoch) == DS_OK,
        "44.1 kHz game voice starts on 48 kHz mixer");
    std::vector<float> output(period_frames * 2);
    for (std::uint64_t block = 0; block < 100; ++block) {
        const auto rendered = mixer->Render(
            output,
            MixerRenderTimeline{block * period_frames, 0});
        failures += Expect(
            rendered.result == MA_SUCCESS &&
                rendered.frames_read == period_frames,
            "44.1 to 48 kHz sustained render");
    }
    failures += Expect(
        std::any_of(
            output.begin(),
            output.end(),
            [](float sample) { return std::abs(sample) > 0.1F; }),
        "44.1 to 48 kHz sustained render is audible");
    failures += Expect(
        ResolvesTo(
            game_source->timeline->ResolveSourceFrame(
                47'999,
                playback_epoch,
                kGamePrimarySampleRate),
            44'099),
        "one second of 48 kHz output maps to the last 44.1 kHz frame");

    constexpr std::uint64_t seek_epoch = 501;
    failures += Expect(
        game_voice->Seek(22'050, seek_epoch) == DS_OK,
        "48 kHz mixer seek accepts game-rate source position");
    auto rendered = mixer->Render(
        output,
        MixerRenderTimeline{48'000, 0});
    failures += Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == period_frames,
        "48 kHz mixer post-seek render");
    failures += Expect(
        ResolvesTo(
            game_source->timeline->ResolveSourceFrame(
                48'000,
                seek_epoch,
                kGamePrimarySampleRate),
            22'050),
        "48 kHz mixer seek starts a new mapping epoch");

    rendered = mixer->Render(
        output,
        MixerRenderTimeline{48'960, 480});
    failures += Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == period_frames,
        "48 kHz mixer discontinuity render");
    failures += Expect(
        ResolvesTo(
            game_source->timeline->ResolveSourceFrame(
                48'960,
                seek_epoch,
                kGamePrimarySampleRate),
            22'932),
        "48 kHz mixer advances a 44.1 kHz source across a gap");

    constexpr std::uint64_t loop_epoch = 502;
    failures += Expect(
        game_voice->Seek(44'000, loop_epoch) == DS_OK,
        "48 kHz mixer loop-boundary seek");
    rendered = mixer->Render(
        output,
        MixerRenderTimeline{49'440, 0});
    failures += Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == period_frames,
        "48 kHz mixer loop-boundary render");
    failures += Expect(
        ResolvesTo(
            game_source->timeline->ResolveSourceFrame(
                49'640,
                loop_epoch,
                kGamePrimarySampleRate),
            83),
        "48 kHz mixer preserves loop mapping through rate conversion");
    game_voice->Stop();

    const auto short_bytes = Pcm16Bytes(
        std::vector<std::int16_t>(441, 12'288));
    auto short_source = MakeSource(
        Pcm(1, kGamePrimarySampleRate, 16),
        short_bytes,
        failures,
        "48 kHz mixer drain source normalization");
    auto short_voice = MakeVoice(
        *mixer,
        *short_source,
        VoiceUsage::General,
        failures,
        "48 kHz mixer drain voice creation");
    if (short_voice == nullptr) {
        return failures + 1;
    }
    constexpr std::uint64_t drain_epoch = 503;
    failures += Expect(
        short_voice->Play(false, drain_epoch) == DS_OK,
        "48 kHz mixer nonlooping play");
    rendered = mixer->Render(
        output,
        MixerRenderTimeline{49'920, 0});
    failures += Expect(
        rendered.result == MA_SUCCESS &&
            rendered.frames_read == period_frames,
        "48 kHz mixer terminal render");
    failures += Expect(
        short_voice->at_end() && !short_voice->playing() &&
            short_voice->audible_until_output_frame() == 50'400,
        "48 kHz mixer publishes the converted terminal drain boundary");
    failures += Expect(
        ResolvesTo(
            short_source->timeline->ResolveSourceFrame(
                50'399,
                drain_epoch,
                441),
            440),
        "48 kHz mixer maps the final drained output frame");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    AllocationProbe probe;
    const auto callbacks = probe.Callbacks();
    ma_result create_result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames,
        kGamePrimarySampleRate,
        &callbacks,
        &create_result);
    failures += Expect(
        create_result == MA_SUCCESS && mixer != nullptr,
        "no-device mixer creation");
    if (mixer == nullptr) {
        return 1;
    }

    const auto mono_bytes = Pcm16Bytes({16384, -16384, 8192, -8192});
    auto mono = MakeSource(
        Pcm(1, 44100, 16),
        mono_bytes,
        failures,
        "mono PCM16 normalization");

    const std::vector<std::int16_t> stereo_samples(kPeriodFrames * 2, 32767);
    const auto stereo_bytes = Pcm16Bytes(stereo_samples);
    auto stereo_a = MakeSource(
        Pcm(2, 44100, 16),
        stereo_bytes,
        failures,
        "first stereo PCM16 normalization");
    auto stereo_b = MakeSource(
        Pcm(2, 44100, 16),
        stereo_bytes,
        failures,
        "second stereo PCM16 normalization");

    const auto pcm24_bytes = Pcm24Bytes({
        4194304, 4194304, 4194304, 4194304,
        4194304, 4194304, 4194304, 4194304,
    });
    auto pcm24 = MakeSource(
        Pcm(1, 44100, 24),
        pcm24_bytes,
        failures,
        "native PCM24 normalization");

    const auto rate_22050_samples = RadicalRegions(16, 32);
    const auto rate_22050_bytes = Pcm16Bytes(rate_22050_samples);
    auto rate_22050 = MakeSource(
        Pcm(1, 22050, 16),
        rate_22050_bytes,
        failures,
        "22.05 kHz PCM16 normalization");

    const auto rate_48000_samples = RadicalRegions(32, 64);
    const auto rate_48000_bytes = Pcm16Bytes(rate_48000_samples);
    auto rate_48000 = MakeSource(
        Pcm(1, 48000, 16),
        rate_48000_bytes,
        failures,
        "48 kHz PCM16 normalization");

    auto mono_voice = MakeVoice(
        *mixer,
        *mono,
        VoiceUsage::GameplayNativeCandidate,
        failures,
        "mono gameplay voice creation");
    auto stereo_voice_a = MakeVoice(
        *mixer,
        *stereo_a,
        VoiceUsage::GameplayNativeCandidate,
        failures,
        "first stereo gameplay voice creation");
    auto stereo_voice_b = MakeVoice(
        *mixer,
        *stereo_b,
        VoiceUsage::GameplayNativeCandidate,
        failures,
        "second stereo gameplay voice creation");
    auto pcm24_voice = MakeVoice(
        *mixer,
        *pcm24,
        VoiceUsage::General,
        failures,
        "native PCM24 voice creation");
    auto rate_22050_voice = MakeVoice(
        *mixer,
        *rate_22050,
        VoiceUsage::General,
        failures,
        "22.05 kHz voice creation");
    auto rate_48000_voice = MakeVoice(
        *mixer,
        *rate_48000,
        VoiceUsage::General,
        failures,
        "48 kHz voice creation");

    if (!mono_voice || !stereo_voice_a || !stereo_voice_b ||
        !pcm24_voice || !rate_22050_voice || !rate_48000_voice) {
        return 1;
    }

    const auto diagnostics = mixer->diagnostics();
    failures += Expect(diagnostics.native_rate_buffers == 4, "native rate count");
    failures += Expect(
        diagnostics.sample_format_converted_buffers == 6,
        "integer to float count");
    failures += Expect(
        diagnostics.sample_rate_converted_buffers == 2,
        "exceptional rate count");
    failures += Expect(
        diagnostics.native_gameplay_buffers == 3,
        "44.1 kHz PCM16 gameplay count");

    std::vector<float> output(kPeriodFrames * 2);
    failures += Expect(
        mono_voice->Play(true, 1) == DS_OK,
        "looping mono play");
    failures += Expect(mono_voice->playing(), "mono voice reports playing");
    failures += Expect(mono_voice->looping(), "mono voice reports looping");
    failures += ExpectRender(*mixer, output, 100, "native mono render");
    failures += ExpectStereoMono(output, "mono duplication to equal L/R");
    failures += ExpectNear(output[0], 0.5F, "first mono sample near 0.5");
    failures += Expect(
        ResolvesTo(mono->timeline->ResolveSourceFrame(104, 1, 4), 0) &&
            ResolvesTo(
                mono->timeline->ResolveSourceFrame(107, 1, 4), 3),
        "loop span crosses source length in the unwrapped domain");
    failures += Expect(
        !mono_voice->audible_until_output_frame().has_value(),
        "looping voice never publishes a terminal drain boundary");

    mono_voice->SetGain(0.5F);
    failures += ExpectRender(*mixer, output, 108, "next-block gain render");
    failures += ExpectNear(output[0], 0.25F, "gain applies without a ramp");

    failures += ExpectRender(*mixer, output, 116, "allocation warm render");
    probe.Begin();
    failures += ExpectRender(*mixer, output, 124, "allocation-probed render");
    failures += Expect(
        probe.End() == 0,
        "steady-state render invokes zero allocator callbacks");
    mono_voice->Stop();
    failures += Expect(
        !mono_voice->playing() &&
            !mono_voice->audible_until_output_frame().has_value(),
        "stopped mono is not playing and has no drain boundary");

    stereo_voice_a->SetGain(0.75F);
    stereo_voice_b->SetGain(0.75F);
    failures += Expect(
        stereo_voice_a->Play(true, 2) == DS_OK &&
            stereo_voice_b->Play(true, 3) == DS_OK,
        "two stereo voices play together");
    auto active = mixer->diagnostics();
    failures += Expect(
        active.active_voices == 2 &&
            active.maximum_simultaneous_voices == 2,
        "active and maximum simultaneous voice counts");
    failures += ExpectRender(*mixer, output, 200, "two-voice stereo mix");
    failures += Expect(
        output[0] > 1.0F && output[1] > 1.0F,
        "float mix retains amplitude above one");
    std::vector<std::int16_t> saturated(output.size());
    gc::audio::ConvertFloatToPcm16(output, saturated);
    failures += Expect(
        saturated[0] == 32767 && saturated[1] == 32767,
        "positive over-range mix clips to 32767");
    stereo_voice_a->Stop();
    stereo_voice_b->Stop();

    const std::vector<float> negative_one{-1.0F};
    std::vector<std::int16_t> negative_pcm(1);
    gc::audio::ConvertFloatToPcm16(negative_one, negative_pcm);
    failures += Expect(
        negative_pcm[0] == -32768,
        "negative one converts exactly to -32768");

    failures += Expect(
        pcm24_voice->Play(false, 4) == DS_OK,
        "native PCM24 play");
    failures += ExpectRender(*mixer, output, 300, "native PCM24 render");
    failures += ExpectNear(output[0], 0.5F, "native PCM24 converts to float");
    failures += Expect(
        pcm24_voice->at_end() && !pcm24_voice->playing(),
        "nonlooping exact-block source ends after exposing final data");
    failures += Expect(
        pcm24_voice->audible_until_output_frame() == 308,
        "nonlooping voice exposes final queued output end");
    failures += Expect(
        pcm24_voice->Play(false, 5) == DS_OK &&
            !pcm24_voice->audible_until_output_frame().has_value(),
        "ended native PCM24 replay clears drain boundary");
    failures += ExpectRender(*mixer, output, 308, "native PCM24 replay render");
    failures += ExpectNear(output[0], 0.5F, "replay restarts at source frame zero");
    failures += Expect(
        pcm24_voice->audible_until_output_frame() == 316,
        "replayed nonlooping voice publishes its new queued end");
    failures += Expect(
        pcm24_voice->Seek(0, 6) == DS_OK &&
            !pcm24_voice->audible_until_output_frame().has_value(),
        "seek clears prior terminal drain boundary");

    failures += Expect(
        rate_22050_voice->Play(false, 10) == DS_OK,
        "22.05 kHz play");
    failures += ExpectRender(*mixer, output, 400, "22.05 kHz linear render");
    failures += ExpectExceptionalLinear(
        output,
        22050,
        false,
        "22.05 kHz reset transient then negative monotonic output");
    failures += ExpectRender(
        *mixer,
        output,
        408,
        "22.05 kHz steady follow-on render");
    failures += ExpectContinuedExceptionalLinear(
        output,
        false,
        "22.05 kHz steady converter phase remains monotonic");
    failures += Expect(
        ResolvesTo(
            rate_22050->timeline->ResolveSourceFrame(412, 10, 32), 6) &&
            ResolvesTo(
                rate_22050->timeline->ResolveSourceFrame(415, 10, 32), 7),
        "22.05 kHz cumulative phase maps the second block exactly");
    rate_22050_voice->Stop();
    failures += Expect(
        rate_22050_voice->Seek(16, 11) == DS_OK &&
            rate_22050_voice->Play(false, 11) == DS_OK,
        "22.05 kHz stopped seek then play");
    failures += ExpectRender(*mixer, output, 416, "22.05 kHz stopped-seek render");
    failures += ExpectExceptionalLinear(
        output,
        22050,
        true,
        "22.05 kHz reset transient then only new positive epoch");
    failures += Expect(
        ResolvesTo(
            rate_22050->timeline->ResolveSourceFrame(416, 11, 32), 16) &&
            DoesNotResolve(
                rate_22050->timeline->ResolveSourceFrame(416, 10, 32)),
        "22.05 kHz seek changes published epoch and position");
    failures += Expect(
        rate_22050_voice->Seek(0, 12) == DS_OK,
        "22.05 kHz resync while playing");
    failures += ExpectRender(*mixer, output, 424, "22.05 kHz live-resync render");
    failures += ExpectExceptionalLinear(
        output,
        22050,
        false,
        "22.05 kHz live reset emits no prior positive history");
    failures += Expect(
        rate_22050_voice->Seek(0, 13) == DS_OK &&
            rate_22050_voice->Seek(16, 14) == DS_OK,
        "22.05 kHz multiple seeks publish latest request");
    failures += ExpectRender(*mixer, output, 432, "22.05 kHz latest-seek render");
    failures += ExpectExceptionalLinear(
        output,
        22050,
        true,
        "22.05 kHz latest seek wins before one render");
    failures += Expect(
        ResolvesTo(
            rate_22050->timeline->ResolveSourceFrame(432, 14, 32), 16) &&
            DoesNotResolve(
                rate_22050->timeline->ResolveSourceFrame(432, 13, 32)),
        "22.05 kHz latest epoch is the only published seek epoch");
    rate_22050_voice->Stop();

    failures += Expect(
        rate_48000_voice->Play(false, 20) == DS_OK,
        "48 kHz play");
    failures += ExpectRender(*mixer, output, 500, "48 kHz linear render");
    failures += ExpectExceptionalLinear(
        output,
        48000,
        false,
        "48 kHz reset transient then negative monotonic output");
    failures += ExpectRender(
        *mixer,
        output,
        508,
        "48 kHz steady follow-on render");
    failures += ExpectContinuedExceptionalLinear(
        output,
        false,
        "48 kHz steady converter phase remains monotonic");
    failures += Expect(
        ResolvesTo(
            rate_48000->timeline->ResolveSourceFrame(512, 20, 64), 13) &&
            ResolvesTo(
                rate_48000->timeline->ResolveSourceFrame(515, 20, 64), 16),
        "48 kHz cumulative phase maps the second block exactly");
    failures += Expect(
        rate_48000_voice->Seek(32, 21) == DS_OK,
        "48 kHz resync while playing");
    failures += ExpectRender(*mixer, output, 516, "48 kHz live-resync render");
    failures += ExpectExceptionalLinear(
        output,
        48000,
        true,
        "48 kHz reset transient then only new positive epoch");
    failures += Expect(
        ResolvesTo(
            rate_48000->timeline->ResolveSourceFrame(516, 21, 64), 32) &&
            DoesNotResolve(
                rate_48000->timeline->ResolveSourceFrame(516, 20, 64)),
        "48 kHz seek changes published epoch and position");
    rate_48000_voice->Stop();
    failures += Expect(
        rate_48000_voice->Seek(32, 22) == DS_OK &&
            rate_48000_voice->Seek(0, 23) == DS_OK &&
            rate_48000_voice->Play(false, 23) == DS_OK,
        "48 kHz stopped multiple seek then play");
    failures += ExpectRender(*mixer, output, 524, "48 kHz latest stopped-seek render");
    failures += ExpectExceptionalLinear(
        output,
        48000,
        false,
        "48 kHz latest stopped seek emits no prior positive history");
    failures += Expect(
        ResolvesTo(
            rate_48000->timeline->ResolveSourceFrame(524, 23, 64), 0) &&
            DoesNotResolve(
                rate_48000->timeline->ResolveSourceFrame(524, 22, 64)),
        "48 kHz latest stopped-seek epoch is published");
    failures += Expect(
        rate_48000_voice->Seek(64, 24) == DSERR_INVALIDPARAM,
        "seek at source length is rejected");
    rate_48000_voice->Stop();

    active = mixer->diagnostics();
    failures += Expect(
        active.active_voices == 0 &&
            active.maximum_simultaneous_voices == 2,
        "voice counters settle without underflow");

    std::vector<float> wrong_size(kPeriodFrames * 2 - 1, 1.0F);
    const auto rejected = mixer->Render(
        wrong_size,
        MixerRenderTimeline{600, 0});
    failures += Expect(
        rejected.result == MA_INVALID_ARGS && rejected.frames_read == 0,
        "non-period render span is rejected");
    const auto invalid_timeline = mixer->Render(
        output,
        MixerRenderTimeline{7, 8});
    failures += Expect(
        invalid_timeline.result == MA_INVALID_ARGS &&
            invalid_timeline.frames_read == 0,
        "discontinuity before output frame zero is rejected");

    failures += TestVoiceRetainsSourceOwners();
    failures += TestVoiceRetainsMixerState();
    failures += TestConcurrentVoiceStateAccounting();
    failures += TestStaleRenderCannotEndNewPlaybackRun();
    failures += TestAudibleDrainPublicationRejectsStaleRunAndEpoch();
    failures += TestConcurrentSeeksKeepTerminalDrainObservable();
    failures += TestNativeLoopAdvancesAcrossDiscontinuity();
    failures += TestNonLoopingVoiceEndsInsideDiscontinuity();
    failures += TestLoopWrapsAcrossDiscontinuity();
    failures += TestConvertedRatesUseCumulativeGapMapping();
    failures += TestDiscontinuityResetsConverterHistory();
    failures += TestExplicitGenerationWinsOverDiscontinuity();
    failures += TestRuntimeOutputRateContract();

    return failures == 0 ? 0 : 1;
}
