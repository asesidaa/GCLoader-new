# WASAPI Fixed-Period Clock Pacing Plan Set

This directory implements
`docs/superpowers/specs/2026-07-14-wasapi-fixed-period-clock-pacing-design.md`
as six independently testable plans. Execute them in order and commit each plan
before starting the next.

## Plan sequence

| Order | Plan | Deliverable | Focused verification |
|---:|---|---|---|
| 1 | [Strict fixed-period endpoint](01-strict-fixed-period-endpoint.md) | Positive, non-clamping period contract and exact buffer validation | `WasapiEndpointTests`, `WasapiAudioPatchTests` |
| 2 | [Output pacing tracker](02-output-pacing-tracker.md) | Pure fixed-slot clock/tail decisions and sustained-gap policy | `OutputPacingTrackerTests` |
| 3 | [Mixer discontinuity advancement](03-mixer-discontinuity-advancement.md) | Timing-preserving source jumps without discard rendering | `MiniaudioMixerTests` |
| 4 | [Cursor generation semantics](04-cursor-generation-semantics.md) | Pending-generation versus real-unmapped cursor results | `AudioCursorTimelineTests`, `SecondarySoundBufferTests` |
| 5 | [Engine pacing integration](05-engine-pacing-integration.md) | Pre-start clock origin, gap recovery, stream latency, counters, fatal handoff | `WasapiEndpointTests`, `ExclusiveAudioEngineTests`, `WasapiAudioPatchTests` |
| 6 | [Static verification and handoff](06-static-verification-and-handoff.md) | Complete x86 build/CTest evidence and operator gameplay checklist | complete CTest |

## Dependency flow

```text
strict endpoint contract ─────────────────────────────────────┐
output pacing tracker ────────────────────────────────────────┤
mixer discontinuity advancement ─────────────────────────────┤
cursor generation semantics ─────────────────────────────────┤
                                                              ↓
                                                   engine integration
                                                              ↓
                                                 static verification
                                                              ↓
                                            operator in-game acceptance
```

## Cross-plan interfaces

These names are fixed for this plan set:

```cpp
namespace gc::audio {

enum class OutputPacingDecisionKind : std::uint8_t {
    Sequential,
    RecoverableGap,
    ChronicGap,
    InvalidClock,
};

struct OutputPacingDecision {
    OutputPacingDecisionKind kind{};
    std::uint64_t block_begin{};
    std::uint64_t block_end{};
    std::uint64_t discontinuity_begin{};
    std::uint64_t discontinuity_frames{};
    std::int64_t submitted_lead_frames{};
};

class OutputPacingTracker;

struct MixerRenderTimeline {
    std::uint64_t output_frame_begin{};
    std::uint64_t discontinuity_frames{};
};

enum class AudioCursorResolutionKind : std::uint8_t {
    Resolved,
    PendingGeneration,
    Unmapped,
};

struct AudioCursorResolution {
    AudioCursorResolutionKind kind{};
    std::uint64_t source_frame{};
};

} // namespace gc::audio
```

## Global constraints

- Source, tests, plans, and commits belong in
  `H:\gc\artifacts\GCLoader\.worktrees\wasapi-exclusive-low-latency-audio`.
  `H:\gc` is runtime/deploy state.
- Do not overwrite or discard the existing uncommitted duration-handoff
  diagnostics. Integrate them in Plan 5.
- Target Windows 10+ and Win32/x86.
- The exact endpoint format remains 44,100 Hz, stereo, PCM16.
- `wasapi_exclusive_buffer_ms` is a positive fixed user value; default 10 ms.
- Zero and values below the endpoint minimum fail. No clamping, probing,
  negotiation, runtime adaptation, or backend fallback is allowed.
- The documented `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED` retry is the only driver
  adjustment.
- Exclusive event rendering always obtains and releases exactly one full actual
  endpoint buffer.
- `IAudioClock` is the presentation authority. Callback count and QPC lateness
  are diagnostics only.
- Explicit game play/seek generations override an older endpoint gap for that
  voice.
- Do not modify `FrameratePatch.cpp`, `SkipMargin`, `SkipInterval`, or gameplay
  timing policy.
- The render thread performs no allocation/free, file I/O, logging, UI,
  configuration access, COM setup, or game-thread mutex wait.
- Every CMake configure and build runs inside
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- CMake discovers or fetches all dependencies. Do not add manual dependency
  paths.
- Automated verification is non-game evidence only. The operator's deployed
  in-game test is the final behavior evidence.

## Standard build command

Use this after any `CMakeLists.txt` change while the cache is healthy:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl'
```

If the cache is contaminated, replace `cmake -S` with `cmake --fresh -S`.
