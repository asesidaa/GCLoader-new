# Audio Types and Pinned Miniaudio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pin the reviewed miniaudio release and establish the exact source-format, output-format, conversion-classification, volume, and frame-time contracts used by every later audio plan.

**Architecture:** Compile miniaudio as a no-device/no-decoder library and expose a small project-owned `NormalizedSourceFormat`. Validate the observed PCM envelope once at buffer creation so render code never branches over malformed formats.

**Tech Stack:** C++23, Windows multimedia/DirectSound structs, miniaudio 0.11.25 commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, CMake FetchContent, CTest.

## Global Constraints

- Pin miniaudio by the full reviewed commit, not a branch.
- Use miniaudio's MIT-0 option.
- Disable miniaudio device I/O, decoding, encoding, resource manager, generators, examples, tests, tools, extra nodes, libvorbis, and libopus.
- All consumers must compile with the same `MA_NO_*` definitions as the miniaudio translation unit.
- Output is exactly 44,100 Hz, stereo, PCM16, block alignment `4`, average bytes/second `176400`.
- Accept only PCM/extensible PCM; 1 or 2 channels; 22,050/44,100/48,000 Hz; packed 16 or 24 bits.
- Reject malformed block alignment, average byte rate, extensible subtype, valid-bit count, and channel mask.
- This plan opens no endpoint and creates no voice.

---

## Prerequisites

- Plan 01 is committed.

## File Structure

- Create `WasapiAudioTypes.h` / `WasapiAudioTypes.cpp`: constants and validation helpers.
- Create `tests/AudioFormatTests.cpp`: accepted matrix, rejection matrix, conversion classes, dB gain, and duration arithmetic.
- Modify `CMakeLists.txt`: pinned dependency and focused test.

### Task 1: Pin Miniaudio and Define the Format Contract

**Files:**
- Create: `WasapiAudioTypes.h`
- Create: `WasapiAudioTypes.cpp`
- Create: `tests/AudioFormatTests.cpp`
- Modify: `CMakeLists.txt:27-90,105-147,163-187`

**Interfaces:**
- Produces `NormalizedSourceFormat`, `NormalizeSourceFormat`, `IsExactOutputFormat`, `DirectSoundVolumeToLinearGain`, `FramesToReferenceTime`, and `ReferenceTimeToFramesCeil`.

- [ ] **Step 1: Add the pinned dependency**

Insert after MinHook:

```cmake
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DEVICEIO ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DECODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_ENCODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_RESOURCE_MANAGER ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_GENERATION ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio)
target_compile_definitions(miniaudio PUBLIC
        MA_NO_DEVICE_IO
        MA_NO_DECODING
        MA_NO_ENCODING
        MA_NO_RESOURCE_MANAGER
        MA_NO_GENERATION
)
```

- [ ] **Step 2: Write the failing format matrix**

Create `tests/AudioFormatTests.cpp` with a helper that builds PCM `WAVEFORMATEX` values. Assert:

```cpp
for (const DWORD rate : {22050UL, 44100UL, 48000UL}) {
    for (const WORD channels : {WORD{1}, WORD{2}}) {
        for (const WORD bits : {WORD{16}, WORD{24}}) {
            const auto source = pcm(channels, rate, bits);
            gc::audio::NormalizedSourceFormat normalized{};
            failures += expect(
                gc::audio::NormalizeSourceFormat(&source, &normalized) == DS_OK,
                "observed PCM accepted");
            failures += expect(
                normalized.sample_rate_converted == (rate != 44100),
                "rate conversion classification");
            failures += expect(
                normalized.native_rate_pcm16 ==
                    (rate == 44100 && bits == 16),
                "native-rate PCM16 classification");
        }
    }
}
```

Add one valid `WAVEFORMATEXTENSIBLE` PCM24 case and explicit rejections for 3 channels, 32 kHz, 32-bit integer, bad block alignment, bad average byte rate, IEEE-float subtype, mismatched valid bits, and noncanonical nonzero channel mask.

Add exact arithmetic assertions:

```cpp
failures += expect(
    gc::audio::ReferenceTimeToFramesCeil(30'000, 44100) == 133,
    "3 ms is 133 whole frames");
failures += expect(
    gc::audio::FramesToReferenceTime(133, 44100) == 30'159,
    "133 frames uses documented nearest hns value");
failures += expect(
    std::abs(gc::audio::DirectSoundVolumeToLinearGain(-600) -
             std::pow(10.0F, -6.0F / 20.0F)) < 0.000001F,
    "DirectSound hundredths of dB conversion");
```

Register `AudioFormatTests`, link it to `miniaudio`, and append `WasapiAudioTypes.cpp` to `SOURCES`.

- [ ] **Step 3: Verify the test is red**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl && cmake --build build-msvc32-latest --target AudioFormatTests'
```

Expected: compilation fails because `WasapiAudioTypes.h` is absent.

- [ ] **Step 4: Define the exact public types**

Create `WasapiAudioTypes.h`:

```cpp
#pragma once
#include <Windows.h>
#include <dsound.h>
#include <mmreg.h>
#include <cstdint>
#include "miniaudio.h"

namespace gc::audio {

inline constexpr std::uint32_t kOutputSampleRate = 44100;
inline constexpr std::uint16_t kOutputChannels = 2;
inline constexpr std::uint16_t kOutputBitsPerSample = 16;
inline constexpr std::uint16_t kOutputBlockAlign = 4;
inline constexpr std::uint32_t kOutputAverageBytesPerSecond = 176400;
inline constexpr REFERENCE_TIME kReferenceTimesPerSecond = 10'000'000;

enum class SourceSampleFormat : std::uint8_t { Pcm16, Pcm24 };
enum class ConversionPath : std::uint8_t {
    NativeRatePcm16,
    NativeRatePcm24,
    LinearResampledPcm16,
    LinearResampledPcm24,
};

struct NormalizedSourceFormat {
    WAVEFORMATEXTENSIBLE wave{};
    std::uint32_t wave_format_size{};
    SourceSampleFormat sample_format{};
    ConversionPath path{};
    ma_format miniaudio_format{ma_format_unknown};
    std::uint16_t channels{};
    std::uint16_t bits_per_sample{};
    std::uint16_t block_align{};
    std::uint32_t sample_rate{};
    std::uint32_t average_bytes_per_second{};
    bool sample_format_converted{};
    bool sample_rate_converted{};
    bool native_rate_pcm16{};
};

HRESULT NormalizeSourceFormat(
    const WAVEFORMATEX*, NormalizedSourceFormat*) noexcept;
bool IsExactOutputFormat(const WAVEFORMATEX&) noexcept;
float DirectSoundVolumeToLinearGain(LONG) noexcept;
REFERENCE_TIME FramesToReferenceTime(
    std::uint64_t, std::uint32_t) noexcept;
std::uint64_t ReferenceTimeToFramesCeil(
    REFERENCE_TIME, std::uint32_t) noexcept;

} // namespace gc::audio
```

- [ ] **Step 5: Implement validation and conversion**

In `NormalizeSourceFormat`:

1. Return `DSERR_INVALIDPARAM` for null input/output.
2. Accept `WAVE_FORMAT_PCM`, or `WAVE_FORMAT_EXTENSIBLE` only when `cbSize == 22`, subtype is `KSDATAFORMAT_SUBTYPE_PCM`, valid bits equal container bits, and mask is zero/canonical mono/canonical stereo.
3. Validate the observed channel/rate/bit matrix and exact derived block/average values.
4. Zero-initialize `NormalizedSourceFormat`, copy exactly `sizeof(WAVEFORMATEX)` or `sizeof(WAVEFORMATEXTENSIBLE)`, and set miniaudio format to `ma_format_s16` or `ma_format_s24`.
5. Set `sample_format_converted = true`, `sample_rate_converted = sample_rate != 44100`, and `native_rate_pcm16 = sample_rate == 44100 && bits == 16`. Do not call a format alone “gameplay”; Plan 06 adds the observed creation/use pattern.

Use these exact helpers:

```cpp
float DirectSoundVolumeToLinearGain(LONG volume) noexcept {
    const auto clamped = std::clamp<LONG>(
        volume, DSBVOLUME_MIN, DSBVOLUME_MAX);
    return ma_volume_db_to_linear(
        static_cast<float>(clamped) / 100.0F);
}

REFERENCE_TIME FramesToReferenceTime(
    std::uint64_t frames,
    std::uint32_t rate) noexcept {
    return rate == 0 ? 0 : static_cast<REFERENCE_TIME>(
        (frames * kReferenceTimesPerSecond + rate / 2) / rate);
}

std::uint64_t ReferenceTimeToFramesCeil(
    REFERENCE_TIME duration,
    std::uint32_t rate) noexcept {
    return duration <= 0 || rate == 0 ? 0 :
        (static_cast<std::uint64_t>(duration) * rate +
         kReferenceTimesPerSecond - 1) /
        kReferenceTimesPerSecond;
}
```

- [ ] **Step 6: Verify focused build and tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^AudioFormatTests$" --output-on-failure'
```

Expected: both targets build and `AudioFormatTests` passes.

- [ ] **Step 7: Commit**

```powershell
git add -- CMakeLists.txt WasapiAudioTypes.h WasapiAudioTypes.cpp tests/AudioFormatTests.cpp
git commit -m "feat: define WASAPI audio format contract"
```

## Completion Gate

The plan is complete when every observed format is accepted, every explicitly unsupported/malformed format is rejected, and CMake proves all consumers share the pinned no-device miniaudio ABI.
