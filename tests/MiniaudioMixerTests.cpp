#include "MiniaudioMixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioLockRegions;
using gc::audio::AudioSnapshot;
using gc::audio::MiniaudioMixer;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;

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
          snapshot(
              static_cast<std::uint32_t>(bytes.size()),
              source_format.block_align) {
        AudioLockRegions regions{};
        const auto lock_result = snapshot.Lock(
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
        initialized = snapshot.Unlock(
            regions.first,
            regions.first_bytes,
            regions.second,
            regions.second_bytes) == DS_OK;
    }

    NormalizedSourceFormat format{};
    AudioSnapshot snapshot;
    AudioCursorTimeline timeline;
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

int ExpectRender(
    MiniaudioMixer& mixer,
    std::span<float> output,
    std::uint64_t output_frame_begin,
    std::string_view name) {
    const auto rendered = mixer.Render(output, output_frame_begin);
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

} // namespace

int main() {
    int failures = 0;
    AllocationProbe probe;
    const auto callbacks = probe.Callbacks();
    ma_result create_result = MA_ERROR;
    auto mixer = MiniaudioMixer::Create(
        kPeriodFrames,
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
        mono->timeline.ResolveSourceFrame(104, 1, 4) == 0 &&
            mono->timeline.ResolveSourceFrame(107, 1, 4) == 3,
        "loop span crosses source length in the unwrapped domain");

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
    failures += Expect(!mono_voice->playing(), "stopped mono is not playing");

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
        pcm24_voice->Play(false, 5) == DS_OK,
        "ended native PCM24 voice replays");
    failures += ExpectRender(*mixer, output, 308, "native PCM24 replay render");
    failures += ExpectNear(output[0], 0.5F, "replay restarts at source frame zero");

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
        rate_22050->timeline.ResolveSourceFrame(416, 11, 32) == 16 &&
            !rate_22050->timeline.ResolveSourceFrame(416, 10, 32).has_value(),
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
        rate_22050->timeline.ResolveSourceFrame(432, 14, 32) == 16 &&
            !rate_22050->timeline.ResolveSourceFrame(432, 13, 32).has_value(),
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
        rate_48000_voice->Seek(32, 21) == DS_OK,
        "48 kHz resync while playing");
    failures += ExpectRender(*mixer, output, 516, "48 kHz live-resync render");
    failures += ExpectExceptionalLinear(
        output,
        48000,
        true,
        "48 kHz reset transient then only new positive epoch");
    failures += Expect(
        rate_48000->timeline.ResolveSourceFrame(516, 21, 64) == 32 &&
            !rate_48000->timeline.ResolveSourceFrame(516, 20, 64).has_value(),
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
        rate_48000->timeline.ResolveSourceFrame(524, 23, 64) == 0 &&
            !rate_48000->timeline.ResolveSourceFrame(524, 22, 64).has_value(),
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
    const auto rejected = mixer->Render(wrong_size, 600);
    failures += Expect(
        rejected.result == MA_INVALID_ARGS && rejected.frames_read == 0,
        "non-period render span is rejected");

    return failures == 0 ? 0 : 1;
}
