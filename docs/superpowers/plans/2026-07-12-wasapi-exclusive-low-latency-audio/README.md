# WASAPI Exclusive Low-Latency Audio Plan Set

This directory implements the design in `docs/superpowers/specs/2026-07-12-wasapi-exclusive-low-latency-audio-design.md` as a sequence of small, independently reviewable plans.

Do not combine these plans during execution. Complete, verify, review, and commit one plan before starting the next. At execution time, create or select an isolated worktree with `superpowers:using-git-worktrees`, then use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` for the selected plan.

## Plan Sequence

| Order | Plan | Deliverable | Focused verification |
|---:|---|---|---|
| 1 | [Opt-in configuration](01-opt-in-configuration.md) | Required default-off TOML field, getter, and ConfigGUI checkbox | `ConfigFeatureTests` |
| 2 | [Audio types and miniaudio](02-audio-types-and-miniaudio.md) | Pinned miniaudio plus exact source/output format contract | `AudioFormatTests` |
| 3 | [Immutable PCM publication](03-immutable-pcm-publication.md) | Copy-on-write DirectSound lock storage with RT-safe reads | `AudioSnapshotTests` |
| 4 | [Endpoint-clock cursor timeline](04-endpoint-clock-cursor-timeline.md) | Fixed render-span ring and source-byte cursor projection | `AudioCursorTimelineTests` |
| 5 | [No-device miniaudio mixer](05-miniaudio-mixer.md) | Snapshot-backed voices, native-rate mixing, exceptional conversion | `MiniaudioMixerTests` |
| 6 | [Secondary DirectSound buffer](06-secondary-directsound-buffer.md) | Functional game-observed `IDirectSoundBuffer8` methods | `SecondarySoundBufferTests` |
| 7 | [DirectSound device and primary buffer](07-directsound-device-primary.md) | Complete `IDirectSound8` and primary-buffer ABI | `DirectSoundDeviceTests` |
| 8 | [Strict WASAPI endpoint](08-wasapi-endpoint.md) | Mockable exclusive endpoint policy and production Core Audio session | `WasapiEndpointTests` |
| 9 | [Exclusive audio engine](09-exclusive-audio-engine.md) | Render thread, endpoint clock, counters, fatal monitor, summaries | `ExclusiveAudioEngineTests` |
| 10 | [Hook and lifecycle integration](10-hook-integration.md) | Exact-target `DirectSoundCreate8` hook, lazy engine, game-only gate | `WasapiAudioPatchTests` |
| 11 | [Verification and gameplay acceptance](11-verification-and-gameplay-acceptance.md) | Full build/CTest evidence and operator play-test gate | complete CTest plus manual sequence |
| 12 | [Configurable fixed buffer duration](12-configurable-fixed-buffer-duration.md) | Default 10 ms fixed request, strict buffer knob, and distortion retest | focused audio tests plus complete CTest |

## Dependency Flow

```text
configuration ────────────────────────────────────────────────┐
audio types → immutable PCM → mixer → secondary buffer ──────┤
audio types → cursor timeline ────────────────┘               │
audio types → strict WASAPI endpoint → exclusive engine ─────┤
device/primary + secondary + exclusive engine → hook/lifecycle
hook/lifecycle → fixed-buffer repair → full verification → operator gameplay acceptance
```

## Cross-Plan Contracts

These names are fixed. A later plan must not rename them without updating every consuming plan first.

```cpp
namespace gc::audio {

inline constexpr std::uint32_t kOutputSampleRate = 44100;
inline constexpr std::uint16_t kOutputChannels = 2;
inline constexpr std::uint16_t kOutputBitsPerSample = 16;
inline constexpr std::uint16_t kOutputBlockAlign = 4;
inline constexpr std::uint32_t kOutputAverageBytesPerSecond = 176400;

struct NormalizedSourceFormat;
class AudioSnapshot;
class AudioCursorTimeline;
class MiniaudioMixer;
class MixerVoice;
class IAudioEngineServices;
class SecondarySoundBuffer;
class PrimarySoundBuffer;
class DirectSoundDevice;
class IWasapiApi;
class WasapiEndpoint;
class ExclusiveAudioEngine;

bool WasapiAudioPatchInit() noexcept;

} // namespace gc::audio
```

## Shared Non-Negotiable Constraints

- Source, tests, plans, and commits belong in `H:\gc\artifacts\GCLoader`. `H:\gc` is runtime/deploy state.
- Preserve the ignored `.superpowers/` directory and unrelated untracked files.
- Target Windows 10+ and Win32/x86 only.
- `experimental.enable_wasapi_exclusive_audio` and `experimental.wasapi_exclusive_buffer_ms` are required; their defaults are `false` and `10`.
- Enabled mode uses one event-driven WASAPI exclusive stream at exact 44,100 Hz stereo PCM16 and requests the larger of the configured duration or endpoint minimum; `0` explicitly selects the minimum.
- There is no enabled-mode fallback to 48 kHz, shared WASAPI, another audio API, or original DirectSound.
- Hook only `dsound!DirectSoundCreate8`; never enable or disable all MinHook targets from this feature.
- COM activation, endpoint enumeration, thread creation, and engine initialization occur after the first intercepted call, never under `DllMain` loader lock.
- The render thread performs no allocation/free, file I/O, plog call, UI, COM initialization, endpoint reconfiguration, or game-thread mutex wait.
- Gameplay BGM, SHOT, tap, and arrangement audio uses no sample-rate conversion.
- `GetCurrentPosition` reports original source bytes derived from `IAudioClock`, not queued mixer position.
- Runtime endpoint failures are handed to a non-real-time monitor; no mid-song fallback or endpoint rebuild is attempted.
- Automated checks do not establish perceived latency. Plan 11 remains open until the operator completes gameplay acceptance.

## Spec Coverage Ownership

| Design requirement family | Owning plan |
|---|---|
| Required flag, disabled behavior, ConfigGUI round trip | 01 |
| Pinned dependency, exact output, observed source envelope, conversion classes | 02 |
| Lock/unlock validation and immutable publication | 03 |
| Render spans, epochs, hardware-clock interpolation, source bytes/write cursor | 04 |
| Voice playback, gain, loop/seek, mono expansion, mixing, exceptional conversion | 05 |
| Secondary caps/format/status/storage/playback/cursor COM behavior | 06 |
| Device and primary COM behavior | 07 |
| Default endpoint, exclusive initialization, alignment retry, event/services/MMCSS | 08 |
| Continuous rendering, preallocation, late/silence counters, runtime fatal handoff | 09 |
| Exact MinHook ownership, lazy first-call startup, fail-closed lifecycle, logging/UI | 10 |
| Complete automated checks, deployment, disabled baseline, gameplay and latency judgment | 11 |
| Fixed 10 ms default, strict buffer knob, endpoint-duration propagation, and crackling retest | 12 |

## Source of Truth

The committed design contains the binary, asset, and endpoint evidence. Re-run daemon-backed `ida-cli` only if the deployed `game471.exe`, its DirectSound import contract, or a game-observed method call differs from that evidence. Do not expand the emulation surface based on hypothetical DirectSound clients.
