# WASAPI Exclusive 48 kHz Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the game-facing 44,100 Hz PCM contract while automatically opening WASAPI exclusive output as exact stereo PCM16 at 44,100 Hz or, when necessary, 48,000 Hz.

**Architecture:** `WasapiEndpoint` probes four exact descriptors in a deterministic order and stores the selected format. That selected rate becomes the single output-frame domain passed through `ExclusiveAudioEngine` to the no-device miniaudio engine, per-voice converters, clock mapper, pacing tracker, render spans, and DirectSound write-cursor projection. Source snapshots and the primary DirectSound buffer remain in their original game-visible domains.

**Tech Stack:** C++23, Win32 DirectSound, WASAPI `IAudioClient`, WRL, miniaudio data converters, CMake/Ninja, CTest, 32-bit MSVC.

## Global Constraints

- Implement the approved design in `docs/superpowers/specs/2026-07-17-wasapi-exclusive-48khz-output-design.md`.
- Preserve the game primary format as legacy stereo PCM16 at 44,100 Hz. Never expose 48,000 Hz through `PrimarySoundBuffer` and never rewrite source assets.
- Probe exactly: 44,100 legacy PCM, 44,100 extensible PCM, 48,000 legacy PCM, then 48,000 extensible PCM.
- Select only exact `S_OK`. Continue after `AUDCLNT_E_UNSUPPORTED_FORMAT` or any other successful-but-nonexact result. Abort immediately on every other failed HRESULT. If no candidate succeeds, return `AUDCLNT_E_UNSUPPORTED_FORMAT`.
- Support no endpoint rates, channel layouts, or sample formats beyond 44,100/48,000 Hz, stereo, PCM16 in this change.
- Keep the configured exclusive period strict and fixed. Preserve the single documented alignment retry, event-driven full-packet submission, clock pacing, gap recovery, and fatal-error policies.
- Preserve miniaudio's current linear resampler and `lpfOrder = 0`; do not add a resampler dependency or a master post-mix converter.
- Keep source normalization independent of the selected endpoint. Classify game-native 44,100 Hz PCM16 separately from runtime sample-rate conversion.
- Do not allocate, format strings, or log on the render thread.
- Treat automated build/CTest results as static evidence only. Only the operator's in-game run on a 48,000 Hz-only endpoint accepts the feature.
- Build and test with the existing 32-bit MSVC/Ninja tree `build-msvc32-latest`.

---

## Task 1: Separate the game-primary contract from endpoint format descriptors

**Files:**

- Modify: `WasapiAudioTypes.h`
- Modify: `WasapiAudioTypes.cpp`
- Modify: `DirectSoundFacade.cpp`
- Modify: `MiniaudioMixer.cpp`
- Modify: `tests/AudioFormatTests.cpp`
- Modify: `tests/DirectSoundDeviceTests.cpp`

- [ ] **Step 1: Write failing tests for the explicit game-primary and endpoint-format contracts**

Replace `ConversionPath` assertions in `tests/AudioFormatTests.cpp` with assertions that source normalization remains endpoint-independent. Add descriptor cases for both supported rates and both descriptor forms:

```cpp
const auto legacy_44100 = MakeEndpointPcm16Format(
    kGamePrimarySampleRate, EndpointFormatKind::LegacyPcm);
const auto extensible_48000 = MakeEndpointPcm16Format(
    kFallbackEndpointSampleRate, EndpointFormatKind::ExtensiblePcm);

failures += expect(
    legacy_44100.valid() &&
        legacy_44100.wave_format().wFormatTag == WAVE_FORMAT_PCM &&
        legacy_44100.wave_format().nSamplesPerSec == 44'100 &&
        legacy_44100.wave_format().nAvgBytesPerSec == 176'400 &&
        legacy_44100.wave_format().cbSize == 0,
    "44.1 kHz legacy endpoint descriptor");
failures += expect(
    extensible_48000.valid() &&
        extensible_48000.wave_format().wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        extensible_48000.wave_format().nSamplesPerSec == 48'000 &&
        extensible_48000.wave_format().nAvgBytesPerSec == 192'000 &&
        extensible_48000.storage.Samples.wValidBitsPerSample == 16 &&
        extensible_48000.storage.dwChannelMask ==
            (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) &&
        IsEqualGUID(
            extensible_48000.storage.SubFormat,
            KSDATAFORMAT_SUBTYPE_PCM),
    "48 kHz extensible endpoint descriptor");
failures += expect(
    !MakeEndpointPcm16Format(32'000, EndpointFormatKind::LegacyPcm).valid(),
    "unsupported endpoint rate does not create a descriptor");
```

Keep the source-rate matrix at 22,050, 44,100, and 48,000 Hz, but assert only 44,100 Hz PCM16 is `game_native_pcm16`. Rename the primary-format assertions to `IsExactGamePrimaryFormat` and retain the existing rejection of 48,000 Hz by the game primary buffer.

- [ ] **Step 2: Run the format tests to prove they fail for the missing vocabulary**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests DirectSoundDeviceTests'
```

Expected: compilation fails because `kGamePrimarySampleRate`, `EndpointFormatKind`, `EndpointPcmFormat`, `MakeEndpointPcm16Format`, `game_native_pcm16`, and `IsExactGamePrimaryFormat` do not exist yet.

- [ ] **Step 3: Add explicit format types and endpoint descriptor construction**

In `WasapiAudioTypes.h`, introduce the game-primary/fallback concepts while temporarily retaining `kOutputSampleRate` and `kOutputAverageBytesPerSecond` as aliases for unchanged callers. Task 5 removes both aliases after all runtime consumers receive a rate.

```cpp
inline constexpr std::uint32_t kGamePrimarySampleRate = 44'100;
inline constexpr std::uint32_t kFallbackEndpointSampleRate = 48'000;
inline constexpr std::uint32_t kOutputSampleRate = kGamePrimarySampleRate;
inline constexpr std::uint16_t kOutputChannels = 2;
inline constexpr std::uint16_t kOutputBitsPerSample = 16;
inline constexpr std::uint16_t kOutputBlockAlign = 4;
inline constexpr std::uint32_t kGamePrimaryAverageBytesPerSecond =
    kGamePrimarySampleRate * kOutputBlockAlign;
inline constexpr std::uint32_t kOutputAverageBytesPerSecond =
    kGamePrimaryAverageBytesPerSecond;

enum class EndpointFormatKind : std::uint8_t {
    LegacyPcm,
    ExtensiblePcm,
};

struct EndpointPcmFormat {
    WAVEFORMATEXTENSIBLE storage{};
    std::uint32_t size{};
    EndpointFormatKind kind{EndpointFormatKind::LegacyPcm};

    bool valid() const noexcept { return size != 0; }
    const WAVEFORMATEX& wave_format() const noexcept {
        return storage.Format;
    }
};

bool IsSupportedEndpointSampleRate(std::uint32_t) noexcept;
EndpointPcmFormat MakeEndpointPcm16Format(
    std::uint32_t, EndpointFormatKind) noexcept;
```

Implement `MakeEndpointPcm16Format` in `WasapiAudioTypes.cpp`. Both forms use two channels, 16 valid/container bits, block alignment 4, and `sample_rate * 4` bytes per second. The extensible form uses `cbSize = 22`, the canonical front-left/front-right mask, and `KSDATAFORMAT_SUBTYPE_PCM`; an unsupported rate returns a zero/invalid descriptor.

Remove the unused `ConversionPath` enum and `NormalizedSourceFormat::path`. Rename `native_rate_pcm16` to `game_native_pcm16`, compute it against `kGamePrimarySampleRate`, and rename `IsExactOutputFormat` to `IsExactGamePrimaryFormat` without otherwise changing its accepted legacy primary-format behavior. Leave `sample_rate_converted` in place only until Task 3 moves that classification to the mixer.

- [ ] **Step 4: Keep DirectSound and gameplay classification explicitly game-facing**

Rename `OutputWaveFormat()` to `GamePrimaryWaveFormat()` in `DirectSoundFacade.cpp` and construct it with `kGamePrimarySampleRate` and `kGamePrimaryAverageBytesPerSecond`:

```cpp
WAVEFORMATEX GamePrimaryWaveFormat() noexcept {
    return {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = kOutputChannels,
        .nSamplesPerSec = kGamePrimarySampleRate,
        .nAvgBytesPerSec = kGamePrimaryAverageBytesPerSecond,
        .nBlockAlign = kOutputBlockAlign,
        .wBitsPerSample = kOutputBitsPerSample,
        .cbSize = 0,
    };
}
```

Use `IsExactGamePrimaryFormat` in `PrimarySoundBuffer::SetFormat`. Use `format.game_native_pcm16` for `GameplayNativeCandidate` in `DirectSoundDevice::CreateSoundBuffer` and in the mixer's native-gameplay diagnostic. Update `tests/DirectSoundDeviceTests.cpp` to use the game-primary constant names while keeping its exact 44,100 Hz assertions unchanged.

- [ ] **Step 5: Build and run the focused contract tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests DirectSoundDeviceTests MiniaudioMixerTests && ctest --test-dir build-msvc32-latest -R "^(AudioFormatTests|DirectSoundDeviceTests|MiniaudioMixerTests)$" --output-on-failure'
```

Expected: all three tests pass. The game primary remains 44,100 Hz, both endpoint descriptor forms are canonical, and endpoint descriptor construction rejects other rates.

- [ ] **Step 6: Commit the explicit format vocabulary**

```powershell
git add -- WasapiAudioTypes.h WasapiAudioTypes.cpp DirectSoundFacade.cpp MiniaudioMixer.cpp tests/AudioFormatTests.cpp tests/DirectSoundDeviceTests.cpp
git commit -m "refactor: separate game and endpoint audio formats"
```

---

## Task 2: Parameterize clock, cursor, and pacing primitives by output rate

**Files:**

- Modify: `AudioCursorTimeline.h`
- Modify: `AudioCursorTimeline.cpp`
- Modify: `OutputPacingTracker.h`
- Modify: `OutputPacingTracker.cpp`
- Modify: `DirectSoundFacade.cpp`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `tests/AudioCursorTimelineTests.cpp`
- Modify: `tests/OutputPacingTrackerTests.cpp`

- [ ] **Step 1: Write failing dual-rate clock and cursor tests**

Update `tests/AudioCursorTimelineTests.cpp` to pass an explicit output rate and prove one hardware second maps to the selected number of output frames:

```cpp
EndpointClockMapper mapper;
mapper.Reset(10'000, 10'000'000, 500, 44'100);
failures += Expect(
    mapper.ToOutputFrame(10'010'000) == 44'600,
    "one second maps to 44,100 output frames");

mapper.Reset(10'000, 10'000'000, 500, 48'000);
failures += Expect(
    mapper.ToOutputFrame(10'010'000) == 48'500,
    "one second maps to 48,000 output frames");
```

Change write-cursor calls to the signature `(play_frame, endpoint_buffer_frames, output_sample_rate, source_rate, source_length_frames)`. Add the equivalent 10 ms projection:

```cpp
failures += Expect(
    ProjectWriteCursorFrame(90, 441, 44'100, 44'100, 1'000) ==
        ProjectWriteCursorFrame(90, 480, 48'000, 44'100, 1'000),
    "44.1 and 48 kHz endpoint packets project the same source-time lead");
failures += Expect(
    ProjectWriteCursorFrame(90, 480, 0, 44'100, 1'000) == 0,
    "zero output rate is rejected safely");
```

- [ ] **Step 2: Write failing runtime-rate rolling-window tests**

Pass a sample rate to every `OutputPacingTracker` construction in `tests/OutputPacingTrackerTests.cpp`. Keep the existing 44,100 Hz assertions and add a 48,000 Hz expiry boundary:

```cpp
OutputPacingTracker expiry_48000(10, 48'000);
auto first = expiry_48000.Plan(11);
expiry_48000.Commit(first);
auto second = expiry_48000.Plan(31);
expiry_48000.Commit(second);
auto after_one_second = expiry_48000.Plan(48'011);
failures += Expect(
    after_one_second.kind == OutputPacingDecisionKind::RecoverableGap,
    "48 kHz gap expires only after 48,000 output frames");

OutputPacingTracker zero_rate(441, 0);
failures += Expect(
    zero_rate.Plan(0).kind == OutputPacingDecisionKind::InvalidClock,
    "zero pacing rate is invalid");
```

- [ ] **Step 3: Run the timing tests to prove the APIs are not rate-aware yet**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioCursorTimelineTests OutputPacingTrackerTests'
```

Expected: compilation fails because the mapper reset, cursor projection, and pacing constructor do not accept an output sample rate.

- [ ] **Step 4: Store and use the runtime rate in the timing primitives**

Change the public contracts to:

```cpp
void EndpointClockMapper::Reset(
    std::uint64_t position,
    std::uint64_t frequency,
    std::uint64_t output_frame,
    std::uint32_t output_sample_rate) noexcept;

std::uint64_t ProjectWriteCursorFrame(
    std::uint64_t play_frame,
    std::uint32_t endpoint_buffer_frames,
    std::uint32_t output_sample_rate,
    std::uint32_t source_rate,
    std::uint64_t source_length_frames) noexcept;

explicit OutputPacingTracker(
    std::uint32_t packet_frames,
    std::uint32_t output_sample_rate) noexcept;
```

Add `output_sample_rate_` to `EndpointClockMapper` and use it as the numerator in `ToOutputFrame`. Return `std::nullopt` when either frequency or output rate is zero. Use the explicit output rate as the denominator in `ProjectWriteCursorFrame`.

Replace `kGapWindowFrames` with a per-instance `gap_window_frames_` initialized from `output_sample_rate`. Treat zero packet frames or zero window frames as invalid in both `Plan` and `Commit`; retain the fixed three-gap array and no-allocation behavior.

- [ ] **Step 5: Update transitional callers without changing runtime behavior yet**

Until endpoint selection is connected in Task 5, pass `kOutputSampleRate` from the existing callers:

```cpp
clock_mapper_.Reset(
    initial_clock.position,
    initialization_.clock_frequency,
    0,
    kOutputSampleRate);
pacing_tracker_.emplace(frames, kOutputSampleRate);

const auto write_frame = ProjectWriteCursorFrame(
    source_frame,
    engine_.endpoint_buffer_frames(),
    kOutputSampleRate,
    format_.sample_rate,
    buffer_bytes_ / format_.block_align);
```

This is an intentionally buildable bridge; Task 5 replaces all three fixed arguments with the selected endpoint rate.

- [ ] **Step 6: Run focused timing and integration tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioCursorTimelineTests OutputPacingTrackerTests SecondarySoundBufferTests ExclusiveAudioEngineTests && ctest --test-dir build-msvc32-latest -R "^(AudioCursorTimelineTests|OutputPacingTrackerTests|SecondarySoundBufferTests|ExclusiveAudioEngineTests)$" --output-on-failure'
```

Expected: all four tests pass; the same primitives now produce correct 44,100 and 48,000 Hz results, and existing callers remain behaviorally unchanged.

- [ ] **Step 7: Commit rate-aware timing primitives**

```powershell
git add -- AudioCursorTimeline.h AudioCursorTimeline.cpp OutputPacingTracker.h OutputPacingTracker.cpp DirectSoundFacade.cpp ExclusiveAudioEngine.cpp tests/AudioCursorTimelineTests.cpp tests/OutputPacingTrackerTests.cpp
git commit -m "refactor: parameterize audio output timing"
```

---

## Task 3: Make the miniaudio mixer own the selected output rate

**Files:**

- Modify: `WasapiAudioTypes.h`
- Modify: `WasapiAudioTypes.cpp`
- Modify: `MiniaudioMixer.h`
- Modify: `MiniaudioMixer.cpp`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `tests/AudioFormatTests.cpp`
- Modify: `tests/MiniaudioMixerTests.cpp`
- Modify: `tests/DirectSoundDeviceTests.cpp`
- Modify: `tests/SecondarySoundBufferTests.cpp`

- [ ] **Step 1: Write failing mixer-construction and conversion-classification tests**

Change every `MiniaudioMixer::Create` call to include an output sample rate. Add explicit construction rejections and keep the 44,100 Hz suite as the regression path:

```cpp
ma_result result = MA_ERROR;
auto mixer_44100 = MiniaudioMixer::Create(
    kPeriodFrames, 44'100, nullptr, &result);
failures += Expect(
    result == MA_SUCCESS && mixer_44100 != nullptr,
    "44.1 kHz mixer creation");

auto zero_rate = MiniaudioMixer::Create(
    kPeriodFrames, 0, nullptr, &result);
failures += Expect(
    result == MA_INVALID_ARGS && zero_rate == nullptr,
    "zero output rate rejected");

auto unsupported_rate = MiniaudioMixer::Create(
    kPeriodFrames, 96'000, nullptr, &result);
failures += Expect(
    result == MA_INVALID_ARGS && unsupported_rate == nullptr,
    "unsupported output rate rejected");
```

Create a 48,000 Hz mixer with one 44,100 Hz PCM16 gameplay source and one 48,000 Hz PCM16 general source. Assert the independent diagnostic facts:

```cpp
const auto diagnostics = mixer_48000->diagnostics();
failures += Expect(
    diagnostics.native_rate_buffers == 1 &&
        diagnostics.sample_rate_converted_buffers == 1 &&
        diagnostics.native_gameplay_buffers == 1,
    "runtime-rate and game-native diagnostics are independent");
```

- [ ] **Step 2: Write failing 44.1-to-48 kHz render and cumulative-mapping tests**

Add `Test44100SourceOn48000Mixer` in `tests/MiniaudioMixerTests.cpp`. Use a looping 44,100-frame identified/ramp source and render 100 blocks of 480 output frames. Assert:

```cpp
constexpr std::uint64_t epoch = 48'000;
ma_result result = MA_ERROR;
auto mixer = MiniaudioMixer::Create(480, 48'000, nullptr, &result);
std::vector<float> output(480 * kOutputChannels);
failures += Expect(
    result == MA_SUCCESS && mixer != nullptr,
    "48 kHz cumulative-test mixer creation");
if (mixer == nullptr) {
    return failures + 1;
}

const auto source_samples = IdentifiedSamples(44'100);
const auto source_bytes = Pcm16Bytes(source_samples);
auto source = MakeSource(
    Pcm(1, 44'100, 16),
    source_bytes,
    failures,
    "44.1 kHz cumulative-test source");
auto voice = MakeVoice(
    *mixer,
    *source,
    VoiceUsage::GameplayNativeCandidate,
    failures,
    "44.1 kHz cumulative-test voice");
if (voice == nullptr || voice->Play(true, epoch) != DS_OK) {
    return failures + 1;
}

for (std::uint64_t block = 0; block < 100; ++block) {
    failures += ExpectRender(
        *mixer,
        output,
        block * 480,
        "44.1-to-48 cumulative render");
}

failures += Expect(
    ResolvesTo(
        source->timeline->ResolveSourceFrame(47'999, epoch, 44'100),
        44'099),
    "48,000 output frames map to one 44,100-frame source second");
```

Also run play, seek, looping wrap, stop/drain, new-play epoch, and a recoverable discontinuity on a 48,000 Hz mixer. Reuse the existing identified-sample and `ExpectNear` helpers to verify the first post-reset samples come from the requested source epoch and that converter history is reset only on the same events as today. Keep a tolerance-based sample-progression assertion so the test verifies unchanged pitch/tempo without depending on bit-exact floating-point output.

- [ ] **Step 3: Run the mixer test to prove the fixed-rate API fails**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target MiniaudioMixerTests'
```

Expected: compilation fails because `MiniaudioMixer::Create` does not accept the new rate and the existing converter/mapping code still assumes `kOutputSampleRate`.

- [ ] **Step 4: Add the runtime rate to the mixer state and construction API**

Change both public overloads and `CreateWithOwner` in `MiniaudioMixer.h`:

```cpp
static std::unique_ptr<MiniaudioMixer> Create(
    std::uint32_t period_frames,
    std::uint32_t output_sample_rate,
    const ma_allocation_callbacks* callbacks,
    ma_result* result) noexcept;
static std::unique_ptr<MiniaudioMixer> Create(
    std::uint32_t period_frames,
    std::uint32_t output_sample_rate,
    std::shared_ptr<const ma_allocation_callbacks> callbacks,
    ma_result* result) noexcept;
```

Add `std::uint32_t output_sample_rate` to `MiniaudioMixerState`. Reject a zero period or any rate for which `IsSupportedEndpointSampleRate` is false. Configure the no-device engine and every per-voice converter from the stored rate:

```cpp
config.sampleRate = state->output_sample_rate;

auto converter_config = ma_data_converter_config_init(
    format.miniaudio_format,
    kOutputChannels,
    format.sample_rate,
    ma_format_f32,
    kOutputChannels,
    state_->output_sample_rate);
converter_config.resampling.algorithm = ma_resample_algorithm_linear;
converter_config.resampling.linear.lpfOrder = 0;
```

Replace every fixed-rate numerator/denominator in source advancement, output-to-source mapping, converter input sizing, cumulative discontinuity advancement, and drain-boundary calculation with `voice.mixer->output_sample_rate` or `state_->output_sample_rate` as appropriate. Do not change the cumulative integer/remainder algorithms.

- [ ] **Step 5: Move sample-rate conversion classification to voice creation**

Delete `NormalizedSourceFormat::sample_rate_converted` and its assignment in `NormalizeSourceFormat`; the selected endpoint is not known there. Compute both mixer counters at `CreateVoice`:

```cpp
const bool native_to_mixer =
    format.sample_rate == state_->output_sample_rate;
state_->native_rate_buffers.fetch_add(
    native_to_mixer ? 1 : 0,
    std::memory_order_relaxed);
state_->sample_rate_converted_buffers.fetch_add(
    native_to_mixer ? 0 : 1,
    std::memory_order_relaxed);
state_->native_gameplay_buffers.fetch_add(
    usage == VoiceUsage::GameplayNativeCandidate &&
            format.game_native_pcm16
        ? 1
        : 0,
    std::memory_order_relaxed);
```

Remove the corresponding endpoint-relative assertion from `tests/AudioFormatTests.cpp`; that test should retain only source-format facts and `game_native_pcm16`.

- [ ] **Step 6: Update all current mixer callers with the transitional game-primary rate**

Pass `kGamePrimarySampleRate` from `ExclusiveAudioEngine.cpp` until Task 5 wires the selected endpoint. Update mixer test fixtures in `tests/DirectSoundDeviceTests.cpp` and `tests/SecondarySoundBufferTests.cpp` the same way:

```cpp
mixer_ = MiniaudioMixer::Create(
    period_frames,
    kGamePrimarySampleRate,
    callbacks,
    &result);
```

In `tests/MiniaudioMixerTests.cpp`, pass 44,100 to all existing cases and 48,000 only to the new endpoint-rate cases. No production caller may rely on a default rate.

- [ ] **Step 7: Run focused source, mixer, and facade tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests MiniaudioMixerTests DirectSoundDeviceTests SecondarySoundBufferTests ExclusiveAudioEngineTests && ctest --test-dir build-msvc32-latest -R "^(AudioFormatTests|MiniaudioMixerTests|DirectSoundDeviceTests|SecondarySoundBufferTests|ExclusiveAudioEngineTests)$" --output-on-failure'
```

Expected: all five tests pass. Existing 44,100 Hz output remains equivalent, 44,100 Hz content renders for the same duration on a 48,000 Hz mixer, cumulative source mapping does not drift, and diagnostics distinguish game-native from mixer-native.

- [ ] **Step 8: Commit the endpoint-rate mixer**

```powershell
git add -- WasapiAudioTypes.h WasapiAudioTypes.cpp MiniaudioMixer.h MiniaudioMixer.cpp ExclusiveAudioEngine.cpp tests/AudioFormatTests.cpp tests/MiniaudioMixerTests.cpp tests/DirectSoundDeviceTests.cpp tests/SecondarySoundBufferTests.cpp
git commit -m "feat: make the audio mixer output-rate aware"
```

---

## Task 4: Negotiate and retain the exact WASAPI endpoint format

**Files:**

- Modify: `WasapiEndpoint.h`
- Modify: `WasapiEndpoint.cpp`
- Modify: `tests/WasapiEndpointTests.cpp`
- Modify: `tests/ExclusiveAudioEngineTests.cpp`

- [ ] **Step 1: Expand the endpoint fake to capture full descriptors and per-candidate results**

Change `IWasapiApi::IsExactFormatSupported` and `InitializeExclusiveEvent` to accept `const EndpointPcmFormat&`. In `tests/WasapiEndpointTests.cpp`, replace the single `format_result` and truncated `WAVEFORMATEX` captures with full descriptors:

```cpp
std::array<HRESULT, 4> format_results{
    S_OK,
    AUDCLNT_E_UNSUPPORTED_FORMAT,
    AUDCLNT_E_UNSUPPORTED_FORMAT,
    AUDCLNT_E_UNSUPPORTED_FORMAT,
};
std::size_t format_result_index{};
std::vector<EndpointPcmFormat> probed_formats;
std::vector<EndpointPcmFormat> initialize_formats;

HRESULT IsExactFormatSupported(
    const EndpointPcmFormat& format) noexcept override {
    probed_formats.push_back(format);
    const auto index = std::min(
        format_result_index++, format_results.size() - 1);
    return Record(Call::IsExactFormatSupported, format_results[index]);
}
```

Update the production `Win32WasapiApi` implementation to pass `format.wave_format()` to `IAudioClient::IsFormatSupported` and `IAudioClient::Initialize`. Update the engine-test fake to compile with the same interface while preserving first-candidate success for its existing tests. Replace remaining endpoint-test uses of the transitional output-rate/byte-rate aliases with explicit game-primary constants or the captured selected rate, as appropriate.

- [ ] **Step 2: Write failing tests for every negotiation outcome**

Add endpoint tests that configure the fake result array and assert exact probe order, selected descriptor, and stopping point:

```cpp
// 44.1 legacy succeeds: one probe.
{S_OK, E_UNEXPECTED, E_UNEXPECTED, E_UNEXPECTED}

// 44.1 legacy rejected, 44.1 extensible succeeds: two probes.
{AUDCLNT_E_UNSUPPORTED_FORMAT, S_OK, E_UNEXPECTED, E_UNEXPECTED}

// Both 44.1 forms rejected, 48 legacy succeeds: three probes.
{AUDCLNT_E_UNSUPPORTED_FORMAT, S_FALSE, S_OK, E_UNEXPECTED}

// 48 legacy rejected, 48 extensible succeeds: four probes.
{S_FALSE, AUDCLNT_E_UNSUPPORTED_FORMAT,
 AUDCLNT_E_UNSUPPORTED_FORMAT, S_OK}
```

For each case, assert rate, legacy/extensible kind, full extensible fields, `format_attempt_count`, each recorded HRESULT, and that `InitializeExclusiveEvent` receives the exact selected wrapper.

Add two failures:

```cpp
// All unsupported/nonexact successes collapse to the exact final result.
{S_FALSE, AUDCLNT_E_UNSUPPORTED_FORMAT,
 S_FALSE, AUDCLNT_E_UNSUPPORTED_FORMAT}
// Operational failure aborts at its position and never probes a later format.
{AUDCLNT_E_UNSUPPORTED_FORMAT, AUDCLNT_E_DEVICE_INVALIDATED,
 S_OK, S_OK}
```

The first must fail at `AudioFailureStage::IsFormatSupported` with `AUDCLNT_E_UNSUPPORTED_FORMAT` after four attempts. The second must fail with `AUDCLNT_E_DEVICE_INVALIDATED` after two attempts and make no `GetDevicePeriod` or initialize call.

- [ ] **Step 3: Write failing selected-rate period and alignment tests**

Retain the existing strict-period cases and add ordinary 10 ms cases where `actual_frames` is 441 for 44,100 Hz and 480 for 48,000 Hz. Add a 48,000 Hz extensible alignment retry and assert both initialize calls receive byte-for-byte equivalent selected descriptors:

```cpp
failures += Expect(
    observed->initialize_formats.size() == 2 &&
        SameEndpointFormat(
            observed->initialize_formats[0],
            observed->initialize_formats[1]) &&
        observed->initialize_formats[0].kind ==
            EndpointFormatKind::ExtensiblePcm &&
        observed->initialize_formats[0].wave_format().nSamplesPerSec == 48'000,
    "alignment retry retains selected 48 kHz extensible descriptor");
```

- [ ] **Step 4: Run the endpoint tests to prove negotiation is absent**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests'
```

Expected: compilation and/or assertions fail because the endpoint still probes one legacy 44,100 Hz `WAVEFORMATEX` and stores no selected descriptor or attempt history.

- [ ] **Step 5: Add allocation-free attempt history and selected-format metadata**

Add `<array>` and `<cstddef>` to `WasapiEndpoint.h`, then append fixed-size format metadata to the existing `EndpointInitialization` fields:

```cpp
inline constexpr std::size_t kEndpointFormatCandidateCount = 4;

struct EndpointFormatAttempt {
    EndpointPcmFormat format{};
    HRESULT result{E_NOTIMPL};
};

struct EndpointInitialization {
    std::array<EndpointFormatAttempt, kEndpointFormatCandidateCount>
        format_attempts{};
    std::uint8_t format_attempt_count{};
    EndpointPcmFormat selected_format{};
    bool has_selected_format{};
};
```

Use an array, never a vector, because the initialization metadata is copied into startup/failure handoff and must not introduce render-path allocation or throwing behavior.

- [ ] **Step 6: Implement exact ordered negotiation and selected-rate initialization**

Construct the fixed candidate array in `WasapiEndpoint::Initialize` and record each result before deciding:

```cpp
const std::array candidates{
    MakeEndpointPcm16Format(44'100, EndpointFormatKind::LegacyPcm),
    MakeEndpointPcm16Format(44'100, EndpointFormatKind::ExtensiblePcm),
    MakeEndpointPcm16Format(48'000, EndpointFormatKind::LegacyPcm),
    MakeEndpointPcm16Format(48'000, EndpointFormatKind::ExtensiblePcm),
};

for (const auto& candidate : candidates) {
    const auto result = api_->IsExactFormatSupported(candidate);
    initialization_.format_attempts[
        initialization_.format_attempt_count++] = {candidate, result};
    if (result == S_OK) {
        initialization_.selected_format = candidate;
        initialization_.has_selected_format = true;
        break;
    }
    if (result == AUDCLNT_E_UNSUPPORTED_FORMAT || SUCCEEDED(result)) {
        continue;
    }
    return Fail(
        AudioFailureStage::IsFormatSupported,
        result,
        attempted,
        failure);
}
if (!initialization_.has_selected_format) {
    return Fail(
        AudioFailureStage::IsFormatSupported,
        AUDCLNT_E_UNSUPPORTED_FORMAT,
        attempted,
        failure);
}
```

Pass `initialization_.selected_format` to the first initialize and the one alignment retry. Derive `selected_rate` from its `WAVEFORMATEX` and use it for:

```cpp
requested = FramesToReferenceTime(aligned_frames, selected_rate);
const auto ordinary_floor = ReferenceTimeToFramesFloor(
    initialization_.requested_duration, selected_rate);
const auto ordinary_ceil = ReferenceTimeToFramesCeil(
    initialization_.requested_duration, selected_rate);
```

Do not re-probe after reactivating the audio client. The selected wrapper remains stored in `EndpointInitialization` and is passed unchanged to retry initialization.

- [ ] **Step 7: Run the endpoint negotiation suite**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests && ctest --test-dir build-msvc32-latest -R "^(WasapiEndpointTests|ExclusiveAudioEngineTests)$" --output-on-failure'
```

Expected: both tests pass. All four success positions, all-unsupported collapse, operational abort, selected-rate ordinary frame validation, and descriptor-preserving alignment retry are covered.

- [ ] **Step 8: Commit deterministic endpoint negotiation**

```powershell
git add -- WasapiEndpoint.h WasapiEndpoint.cpp tests/WasapiEndpointTests.cpp tests/ExclusiveAudioEngineTests.cpp
git commit -m "feat: negotiate WASAPI exclusive output format"
```

---

## Task 5: Drive the engine and DirectSound facade from the selected rate

**Files:**

- Modify: `WasapiAudioTypes.h`
- Modify: `DirectSoundFacade.h`
- Modify: `DirectSoundFacade.cpp`
- Modify: `ExclusiveAudioEngine.h`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `WasapiAudioPatch.cpp`
- Modify: `tests/ExclusiveAudioEngineTests.cpp`
- Modify: `tests/DirectSoundDeviceTests.cpp`
- Modify: `tests/SecondarySoundBufferTests.cpp`
- Modify: `tests/WasapiAudioPatchTests.cpp`

- [ ] **Step 1: Write a failing end-to-end 48,000 Hz engine test**

Extend `FakeWasapiState` in `tests/ExclusiveAudioEngineTests.cpp` with a supported rate/kind and full probe captures. Its format probe returns `S_OK` only for that exact pair and `AUDCLNT_E_UNSUPPORTED_FORMAT` otherwise. Keep the fake endpoint packet at eight frames, which is within the allowed floor/ceiling for the fixture period at both supported rates.

Add a 48,000 Hz fixture case that asserts the negotiated metadata reaches every engine-facing seam:

```cpp
EngineFixture fixture;
fixture.api->supported_output_rate = 48'000;
fixture.api->supported_format_kind = EndpointFormatKind::LegacyPcm;
auto engine = fixture.Start();

failures += Expect(
    engine != nullptr &&
        engine->output_sample_rate() == 48'000 &&
        fixture.observer_state->startup.has_selected_format &&
        fixture.observer_state->startup.selected_format.wave_format()
                .nSamplesPerSec == 48'000 &&
        fixture.api->format_probe_count == 3,
    "engine publishes selected 48 kHz fallback");

fixture.api->PushClock(
    kInitialClock + kClockFrequency,
    kInitialClock + kClockFrequency);
const auto mapped = engine->CurrentOutputFrame();
failures += Expect(
    mapped.has_value() && *mapped == 48'000,
    "engine clock mapper uses selected 48 kHz domain");
```

Create a 44,100 Hz gameplay voice through that engine, render at least two packets, and assert mixer diagnostics count it as both game-native and sample-rate-converted. This catches the endpoint-selected/mixer-fixed mismatch that exists before this task.

- [ ] **Step 2: Write a failing DirectSound write-cursor integration test**

Add `output_sample_rate()` to the `IAudioEngineServices` fakes in `tests/DirectSoundDeviceTests.cpp`, `tests/SecondarySoundBufferTests.cpp`, and `tests/WasapiAudioPatchTests.cpp`. Make `MixerEngineServices` in `tests/SecondarySoundBufferTests.cpp` construct its mixer with configurable endpoint rate and packet frames.

Add a 44,100 Hz secondary buffer backed by a 48,000 Hz engine with a 480-frame packet. After the play span resolves, assert the write cursor is exactly 441 source frames ahead, modulo the source length:

```cpp
MixerEngineServices engine(/* output_rate */ 48'000,
                           /* endpoint_frames */ 480);
failures += Expect(
    buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
        write_cursor ==
            ((play_cursor / source_block_align + 441) % source_frames) *
                source_block_align,
    "48 kHz endpoint write cursor projects a 10 ms 44.1 kHz source lead");
```

- [ ] **Step 3: Run the engine/facade tests to expose the fixed transitional calls**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ExclusiveAudioEngineTests SecondarySoundBufferTests DirectSoundDeviceTests WasapiAudioPatchTests'
```

Expected: compilation fails for the new service method and/or the 48,000 Hz assertions fail because engine mixer, clock, pacing, period, and facade projection still pass the transitional 44,100 Hz rate.

- [ ] **Step 4: Expose the selected rate through the engine service contract**

Add the method to `IAudioEngineServices` and its production implementation:

```cpp
virtual std::uint32_t output_sample_rate() const noexcept = 0;

std::uint32_t ExclusiveAudioEngine::output_sample_rate() const noexcept {
    return output_sample_rate_.load(std::memory_order_acquire);
}
```

Add `std::atomic_uint32_t output_sample_rate_{}` beside `endpoint_buffer_frames_`. After endpoint initialization succeeds, validate `has_selected_format`, read its rate, and store both the frame count and selected rate before publishing successful startup. An absent/unsupported selected rate fails at `AudioFailureStage::InitializeMixer` with `E_INVALIDARG`.

- [ ] **Step 5: Use one selected-rate domain throughout engine initialization**

Replace every transitional fixed-rate call in `ExclusiveAudioEngine.cpp`:

```cpp
const auto output_sample_rate =
    initialization_.selected_format.wave_format().nSamplesPerSec;

mixer_ = MiniaudioMixer::Create(
    frames,
    output_sample_rate,
    mixer_allocations_,
    &mixer_result);
clock_mapper_.Reset(
    initial_clock.position,
    initialization_.clock_frequency,
    0,
    output_sample_rate);
pacing_tracker_.emplace(frames, output_sample_rate);
actual_period_100ns_ = FramesToReferenceTime(
    frames, output_sample_rate);
```

Keep render buffer sizing based on fixed stereo channels and the selected endpoint packet frame count. Do not add another render buffer, staging queue, device, or thread.

- [ ] **Step 6: Use the engine rate only for hardware-time cursor projection**

Pass `engine_.output_sample_rate()` from `SecondarySoundBuffer::GetCurrentPosition`:

```cpp
const auto write_frame = ProjectWriteCursorFrame(
    source_frame,
    engine_.endpoint_buffer_frames(),
    engine_.output_sample_rate(),
    format_.sample_rate,
    buffer_bytes_ / format_.block_align);
```

Do not use the engine rate in source normalization, buffer allocation, locking, byte cursors, primary `GetFormat`, or primary `SetFormat`. Update every test fake with an explicit rate, defaulting to `kGamePrimarySampleRate` for existing cases.

- [ ] **Step 7: Remove the ambiguous fixed-output aliases and audit every use**

Before deleting the aliases, change `startup_text` in `WasapiAudioPatch.cpp` to compute `actual_buffer_ms` and `mixer_rate_hz` from `initialization.selected_format.wave_format().nSamplesPerSec`. Populate a selected 44,100 Hz legacy descriptor in every pre-existing successful-startup test fixture so the old expected text remains valid. Task 6 will replace the remaining hard-coded format label and add attempt-history text.

Then delete `kOutputSampleRate` and the old `kOutputAverageBytesPerSecond` alias from `WasapiAudioTypes.h`. Use `kGamePrimarySampleRate`/`kGamePrimaryAverageBytesPerSecond` only for game-facing formats and source fixtures. All endpoint, mixer, clock, pacing, period, diagnostics, and cursor calculations must receive or read a runtime rate.

```cpp
const auto output_sample_rate =
    initialization.selected_format.wave_format().nSamplesPerSec;
const double actual_ms =
    static_cast<double>(initialization.actual_buffer_frames) * 1000.0 /
    static_cast<double>(output_sample_rate);
stream << " mixer_rate_hz=" << output_sample_rate;
```

Run the audit:

```powershell
rg -n "kOutputSampleRate|kOutputAverageBytesPerSecond" --glob '!docs/**' --glob '!build*/**'
```

Expected: no matches.

- [ ] **Step 8: Run the full rate-domain integration slice**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests AudioCursorTimelineTests OutputPacingTrackerTests MiniaudioMixerTests WasapiEndpointTests ExclusiveAudioEngineTests DirectSoundDeviceTests SecondarySoundBufferTests WasapiAudioPatchTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^(AudioFormatTests|AudioCursorTimelineTests|OutputPacingTrackerTests|MiniaudioMixerTests|WasapiEndpointTests|ExclusiveAudioEngineTests|DirectSoundDeviceTests|SecondarySoundBufferTests|WasapiAudioPatchTests)$" --output-on-failure'
```

Expected: all nine focused tests pass and `iDmacDrv32.dll` links. The 44,100 Hz path remains green; the 48,000 Hz endpoint path drives mixer, clock, pacing, timeline, and cursor projection consistently.

- [ ] **Step 9: Commit selected-rate engine integration**

```powershell
git add -- WasapiAudioTypes.h DirectSoundFacade.h DirectSoundFacade.cpp ExclusiveAudioEngine.h ExclusiveAudioEngine.cpp WasapiAudioPatch.cpp tests/ExclusiveAudioEngineTests.cpp tests/DirectSoundDeviceTests.cpp tests/SecondarySoundBufferTests.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "feat: drive audio engine at negotiated rate"
```

---

## Task 6: Report selected and attempted formats without render-thread logging

**Files:**

- Modify: `WasapiAudioPatch.cpp`
- Modify: `GUI_main.cpp`
- Modify: `tests/WasapiAudioPatchTests.cpp`

- [ ] **Step 1: Write failing success-diagnostic assertions for both rates and descriptor forms**

Populate full selected/attempted metadata in `test_production_diagnostics_use_injected_platform_actions`. Add a 48,000 Hz fallback case and assert startup text includes:

```cpp
for (const auto required : {
         "format=pcm16/48000Hz/2ch/16bit",
         "descriptor=legacy_pcm",
         "fallback_rate=true",
         "format_attempt_count=3",
         "format_attempts=\"44100/legacy_pcm:",
         "44100/extensible_pcm:",
         "48000/legacy_pcm:0x00000000\"",
         "actual_buffer_frames=480",
         "actual_buffer_ms=10.000",
         "mixer_rate_hz=48000",
     }) {
    failures += expect(contains(startup, required), required);
}
```

Add a 44,100 Hz extensible success and assert `descriptor=extensible_pcm`, `fallback_rate=false`, and two attempts. Retain period, MMCSS, endpoint identity, stream-latency, and event-driven fields.

- [ ] **Step 2: Write failing startup/runtime failure assertions with attempt history**

Build an all-unsupported `AudioStartupFailure` containing four recorded results and no selected format. Assert the failure record says `format=<none>`, `descriptor=<none>`, `format_attempt_count=4`, and lists all four ordered candidates with HRESULTs.

For a runtime failure after a selected 48,000 Hz extensible startup, assert the saved selected format and fallback state remain present alongside the existing endpoint id, stage, HRESULT, period metadata, and counters.

- [ ] **Step 3: Run the diagnostics tests to prove logs still hard-code 44,100 Hz**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiAudioPatchTests && ctest --test-dir build-msvc32-latest -R "^WasapiAudioPatchTests$" --output-on-failure'
```

Expected: `WasapiAudioPatchTests` fails because `kExactFormat`, actual-buffer milliseconds, and mixer-rate text still assume 44,100 Hz and no attempt history is formatted.

- [ ] **Step 4: Format selected descriptors and ordered attempts on observer threads**

Replace `kExactFormat` with helpers that read `EndpointInitialization`. Keep all string construction inside the existing startup/failure reporting functions:

```cpp
const char* descriptor_name(EndpointFormatKind kind) noexcept {
    return kind == EndpointFormatKind::LegacyPcm
        ? "legacy_pcm"
        : "extensible_pcm";
}

std::string selected_format_text(
    const EndpointInitialization& initialization) {
    if (!initialization.has_selected_format) {
        return "format=<none> descriptor=<none> fallback_rate=false";
    }
    const auto& wave = initialization.selected_format.wave_format();
    std::ostringstream stream;
    stream << "format=pcm16/" << wave.nSamplesPerSec
           << "Hz/" << wave.nChannels
           << "ch/" << wave.wBitsPerSample
           << "bit descriptor="
           << descriptor_name(initialization.selected_format.kind)
           << " fallback_rate="
           << (wave.nSamplesPerSec != kGamePrimarySampleRate
                   ? "true"
                   : "false");
    return stream.str();
}
```

Add `format_attempts_text` that iterates only `format_attempt_count`, emits candidate rate/form/result in stored order, and uses the existing uppercase eight-digit HRESULT formatter. Include it in startup success, startup failure, and runtime failure records.

Compute `actual_buffer_ms` and `mixer_rate_hz` from the selected sample rate. Guard the no-selection case so failure formatting never divides by zero. Do not change `counters_text`; the existing native-rate, converted-rate, and native-gameplay counters already expose runtime conversion behavior.

- [ ] **Step 5: Update the GUI description to match automatic negotiation**

Change only the WASAPI checkbox tooltip in `GUI_main.cpp`:

```cpp
ImGui::SetTooltip(
    "Uses the default console endpoint in exclusive stereo PCM16 mode.\n"
    "Prefers exact 44.1 kHz and automatically falls back to exact 48 kHz.\n"
    "Disable this option if exclusive endpoint initialization fails.");
```

Do not add a rate selector or change configuration serialization.

- [ ] **Step 6: Build the diagnostics, DLL, and configuration GUI**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiAudioPatchTests iDmacDrv32 ConfigGUI && ctest --test-dir build-msvc32-latest -R "^WasapiAudioPatchTests$" --output-on-failure'
```

Expected: diagnostics tests pass, `iDmacDrv32.dll` links, and `ConfigGUI.exe` builds with the new explanatory text. No rate option is introduced.

- [ ] **Step 7: Commit diagnostics and operator-facing text**

```powershell
git add -- WasapiAudioPatch.cpp GUI_main.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "feat: report negotiated WASAPI output format"
```

---

## Task 7: Verify the complete 32-bit build and prepare runtime acceptance

**Files:**

- Verify only: `build-msvc32-latest/`
- Verify only: `docs/superpowers/specs/2026-07-17-wasapi-exclusive-48khz-output-design.md`

- [ ] **Step 1: Run static hygiene and fixed-rate-assumption audits**

```powershell
git diff --check
rg -n "kOutputSampleRate|kOutputAverageBytesPerSecond" --glob '!docs/**' --glob '!build*/**'
rg -n "ma_resample_algorithm_linear|lpfOrder = 0" MiniaudioMixer.cpp
```

Expected: `git diff --check` is silent; the removed ambiguous constants have no production/test matches; the existing linear/zero-order resampler configuration is still present.

- [ ] **Step 2: Reconfigure and rebuild the complete x86 tree from current sources**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl && cmake --build build-msvc32-latest'
```

Expected: configure/generate succeeds and every target, including `iDmacDrv32`, `ConfigGUI`, and all test executables, builds successfully for Win32.

- [ ] **Step 3: Run the complete CTest suite**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && ctest --test-dir build-msvc32-latest --output-on-failure'
```

Expected: 28 of 28 tests pass with zero failures. If the project test count has intentionally changed, report the actual discovered/pass count and explain the CMake change instead of copying this expectation.

- [ ] **Step 4: Capture the candidate DLL identity without deploying it**

```powershell
Get-Item -LiteralPath 'build-msvc32-latest/iDmacDrv32.dll' | Select-Object FullName,Length,LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-latest/iDmacDrv32.dll'
git status --short --branch
```

Expected: the DLL exists, its SHA-256 is printed for the handoff, and the branch contains only intentional implementation/plan changes. Do not copy into `H:\gc` as part of static verification; deployment is an explicit operator acceptance action.

- [ ] **Step 5: Hand off the two enabled-backend runtime checks**

Report static verification separately, then ask the operator to deploy the identified DLL and run:

1. A 44,100 Hz-capable endpoint regression: startup selects 44,100 Hz; menu, attract, stage BGM/SHOT, taps, effects, fades, seeks, loops, and transitions have normal pitch/tempo and no new gaps or cursor failures.
2. A 48,000 Hz-only endpoint acceptance: startup logs both rejected/skipped 44,100 Hz descriptors and a selected 48,000 Hz descriptor; a normal 10 ms endpoint reports 480 frames unless alignment-adjusted; the same representative audio remains correctly pitched, timed, synchronized, and stable.

Healthy runtime summaries for both runs must show zero confirmed gaps, skipped frames, chronic pacing failures, genuine unmapped cursor failures, and endpoint failures. Do not mark runtime acceptance complete if the operator reports silence, distortion, pitch/tempo change, loop/seek errors, desynchronization, instability, or startup failure.

- [ ] **Step 6: State the verification boundary accurately**

The implementation handoff must use one of these outcomes:

- `Static verification passed; 48,000 Hz in-game acceptance is pending operator testing.`
- `Static verification passed; operator accepted both 44,100 Hz regression and 48,000 Hz-only gameplay.`
- `Not accepted:` followed by the exact failing build, test, startup log, or gameplay symptom.

Never infer gameplay acceptance from CTest or a successful DLL build.
