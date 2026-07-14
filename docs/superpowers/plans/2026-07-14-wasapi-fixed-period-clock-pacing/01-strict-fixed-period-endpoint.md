# Strict Fixed-Period Endpoint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the configured exclusive period an honest positive user value, reject requests below the endpoint minimum, and accept only documented frame alignment or one-frame duration rounding.

**Architecture:** `WasapiEndpoint` validates the configured `REFERENCE_TIME` before and after `GetDevicePeriod`, passes the value unchanged to exclusive initialization, and retains the existing single alignment retry. Endpoint metadata preserves every attempted value for startup reporting.

**Tech Stack:** C++23, WASAPI `IAudioClient`, WRL, CTest.

## Global Constraints

- Apply every constraint in `README.md`.
- Do not use `max(configured_duration, minimum_period)`.
- Do not change the 10 ms distributed default.
- A failed validation must perform no later endpoint initialization call.

---

### Task 1: Positive non-clamping endpoint contract

**Files:**
- Modify: `WasapiAudioTypes.h`
- Modify: `WasapiAudioTypes.cpp`
- Modify: `WasapiEndpoint.h`
- Modify: `WasapiEndpoint.cpp`
- Modify: `tests/AudioFormatTests.cpp`
- Modify: `tests/WasapiEndpointTests.cpp`
- Modify: `tests/WasapiAudioPatchTests.cpp`

**Interfaces:**
- Consumes: `REFERENCE_TIME configured_duration` already passed to
  `WasapiEndpoint::Create`.
- Produces: `AudioFailureStage::InvalidConfiguredDuration`,
  `AudioFailureStage::ConfiguredDurationBelowMinimum`, and strict
  `EndpointInitialization::requested_duration` semantics.

- [x] **Step 1: Write failing endpoint tests**

Add cases that call `WasapiEndpoint::Create` with controlled fake APIs:

```cpp
auto zero = WasapiEndpoint::Create(
    std::move(zero_api), 0, &attempted, &failure);
expect(!zero &&
       failure.stage == AudioFailureStage::InvalidConfiguredDuration &&
       fake.initialize_exclusive_calls == 0,
       "zero fixed duration is rejected before exclusive initialization");

auto below = WasapiEndpoint::Create(
    std::move(below_api), 20'000, &attempted, &failure);
expect(!below &&
       attempted.minimum_period == 30'000 &&
       attempted.requested_duration == 20'000 &&
       failure.stage == AudioFailureStage::ConfiguredDurationBelowMinimum &&
       fake.initialize_exclusive_calls == 0,
       "below-minimum duration fails without clamping");
```

Retain a 100,000-reference-time success case and assert both arguments passed
to `InitializeExclusiveEvent` are exactly 100,000. Add no-alignment cases where
the actual frames equal the floor or ceiling of the requested duration, plus a
multi-frame mismatch that fails at `GetActualBufferSize`.

- [x] **Step 2: Run the focused tests and verify red**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests WasapiAudioPatchTests && ctest --test-dir build-msvc32-latest -R "^(WasapiEndpointTests|WasapiAudioPatchTests)$" --output-on-failure'
```

Expected: the zero case still selects the minimum, the below-minimum case still
clamps, or the new failure-stage symbols are absent.

- [x] **Step 3: Add strict validation stages**

Extend `AudioFailureStage`:

```cpp
InvalidConfiguredDuration,
ConfiguredDurationBelowMinimum,
```

At the start of `WasapiEndpoint::Initialize`, reject non-positive duration:

```cpp
if (initialization_.configured_duration <= 0) {
    return Fail(
        AudioFailureStage::InvalidConfiguredDuration,
        E_INVALIDARG,
        attempted,
        failure);
}
```

After `GetDevicePeriod`, preserve the configured value as the attempted request
and fail instead of clamping:

```cpp
initialization_.requested_duration = initialization_.configured_duration;
if (initialization_.configured_duration < initialization_.minimum_period) {
    return Fail(
        AudioFailureStage::ConfiguredDurationBelowMinimum,
        AUDCLNT_E_INVALID_DEVICE_PERIOD,
        attempted,
        failure);
}
const auto requested = initialization_.configured_duration;
```

- [x] **Step 4: Enforce exact ordinary rounding**

For a success without alignment retry, compute the allowed frame bounds with
overflow-safe integer helpers:

```cpp
const auto floor_frames = ReferenceTimeToFramesFloor(
    initialization_.requested_duration, kOutputSampleRate);
const auto ceil_frames = ReferenceTimeToFramesCeil(
    initialization_.requested_duration, kOutputSampleRate);
const bool ordinary_size =
    initialization_.actual_buffer_frames == floor_frames ||
    initialization_.actual_buffer_frames == ceil_frames;
```

Add `ReferenceTimeToFramesFloor` beside the existing ceiling helper in
`WasapiAudioTypes.h/.cpp`, with zero/negative and overflow tests in
`tests/AudioFormatTests.cpp`. Keep the alignment-retry equality check unchanged.

- [x] **Step 5: Verify green**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioFormatTests WasapiEndpointTests WasapiAudioPatchTests && ctest --test-dir build-msvc32-latest -R "^(AudioFormatTests|WasapiEndpointTests|WasapiAudioPatchTests)$" --output-on-failure'
```

Expected: all three tests pass.

- [x] **Step 6: Commit**

```powershell
git add -- WasapiAudioTypes.h WasapiAudioTypes.cpp WasapiEndpoint.h WasapiEndpoint.cpp tests/AudioFormatTests.cpp tests/WasapiEndpointTests.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "fix: enforce strict WASAPI buffer duration"
```
