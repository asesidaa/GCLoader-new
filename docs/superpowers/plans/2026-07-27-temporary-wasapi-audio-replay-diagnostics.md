# Temporary WASAPI Audio Replay Diagnostics Stage A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, verify, deploy, and exercise an always-on temporary audio flight recorder that determines whether the intermittent replay occurs at the game seek, mixer/source progression, submitted PCM, or downstream endpoint boundary.

**Architecture:** A process-lifetime `AudioFlightRecorder` owned by the existing WASAPI detour state accepts fixed-size, allocation-free records from real-time producers and drains them on a dedicated writer thread into checkpointed PCM16 WAV and JSONL files. A loss-detecting bounded MPSC event queue drops new events instead of racing an overwrite against the consumer. The mixer, DirectSound facade, resync hook, and exclusive engine publish causal metadata without changing audio decisions; a Python standard-library analyzer performs all waveform correlation and clip extraction after the game exits.

**Tech Stack:** C++23, Win32 x86, DirectSound, WASAPI exclusive event-driven rendering, miniaudio 0.11.25, SafetyHook, CMake/Ninja presets, CTest, MSVC x86, Python 3 standard library, RIFF/WAVE PCM16, JSON Lines.

## Global Constraints

- The approved design is
  `docs/superpowers/specs/2026-07-27-temporary-wasapi-audio-replay-diagnostics-design.md`.
- This is the **Stage A diagnostics plan only**. It ends at a root-cause
  checkpoint. The evidence must select the Stage B correction before a
  correction plan can be written; no speculative audio fix belongs here.
- Stage B must retain this same recorder while verifying the evidence-backed
  correction. Stage C must remove every source, test, tool, thread, event,
  counter, output, and log field introduced by this plan.
- The diagnostic DLL records automatically whenever the WASAPI engine starts.
  Do not add a public config key, internal enable flag, Config GUI control, or
  disabled-by-default path.
- Work and commits belong in `H:\gc\artifacts\GCLoader`. `H:\gc` is only the
  runtime/deployment tree.
- Do not modify or commit `H:\gc\data`, `H:\gc\game471.exe`,
  `H:\gc\game471.exe.i64`, `H:\gc\config.toml`, generated captures, runtime
  DLLs, or deployment backups.
- Preserve the current endpoint contract: PCM16, stereo, 48,000 Hz fallback,
  480 frames/10 ms for the supplied device and configuration.
- Preserve the current linear 44.1-to-48 kHz miniaudio converter,
  `lpfOrder = 0`, cursor projection, output pacing, 10 ms buffer, and accepted
  in-margin resync suppression.
- Do not suppress a new seek, add a crossfade, reuse PCM, change gain, alter
  endpoint negotiation, or change any chart/judgement/timing behavior.
- `ExclusiveAudioEngine::RenderLoop` and `VoiceNodeProcess` may only copy into
  preallocated recorder slots and publish atomics/events. They must not
  allocate, lock, wait, format text, log, open/seek/flush/write files, or call
  the analyzer.
- Queue allocation and file creation may occur before `endpoint_->Start`.
  Every operation after the render loop begins must obey the real-time rule.
- `ProductionDetourState` is intentionally process-lifetime. Do not add
  destructor work, waits, or thread joins to `DLL_PROCESS_DETACH`. The writer
  must update the WAV header and flush a causal checkpoint once per second.
  The analyzer must ignore any uncheckpointed tail.
- The capture safety limit is 1,800 seconds. Reaching it stops diagnostics
  only; audio continues unchanged.
- A PCM queue loss makes the affected interval inconclusive. Never label
  writer-inserted silence as submitted audio.
- Existing production startup, failure, and 30-second runtime-health lines
  remain unchanged. Add only one temporary diagnostic startup line. Capture
  limit, queue loss, and writer/checkpoint validity belong to the session
  artifacts and analyzer report, not the ordinary runtime summary.
- Runtime acceptance belongs to the user. Automated tests and captured PCM do
  not prove that a candidate clip sounds like the live artifact.
- Capture artifacts stay outside Git and remain only until the root cause and
  corrected diagnostic run are recorded. Their later deletion occurs in
  Stage C, not this plan.
- Use the `msvc32-release` preset through
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- The implementation diff baseline is design commit `d799e77`. Owned source
  changes are limited to the file map below plus this plan and the validation
  record.

---

## File and Responsibility Map

| File | Responsibility in Stage A |
|---|---|
| `src/Audio/Diagnostics/AudioFlightRecorder.h` | Temporary POD event schema, diagnostic sink interface, active-sink publication, bounded queue APIs, recorder/session/status interface. |
| `src/Audio/Diagnostics/AudioFlightRecorder.cpp` | SPSC PCM queue, loss-detecting bounded MPSC event queue, checkpointed WAV/JSONL writer, process-lifetime-safe session implementation. |
| `src/Audio/CMakeLists.txt` | Compile the temporary recorder into `gc_audio`. |
| `tests/Audio/AudioFlightRecorderTests.cpp` | Deterministic queue, active-sink, WAV, JSONL, checkpoint, overflow, and limit tests. |
| `tests/Audio/CMakeLists.txt` | Register `AudioFlightRecorderTests`. |
| `src/Audio/Wasapi/ExclusiveAudioEngine.h/.cpp` | Start the recorder from the negotiated endpoint, retain its sink for Task 4 mixer construction, and publish exact successful PCM blocks plus endpoint timeline metadata. |
| `src/Audio/Wasapi/WasapiAudioPatch.cpp` | Own the process-lifetime recorder, forward it to engine startup, and emit the one temporary startup/error line. |
| `src/Audio/Wasapi/WasapiAudioPatchInternal.h` | Extend the production startup function type/signature for the sink. |
| `tests/Audio/ExclusiveAudioEngineTests.cpp` | Prove start-before-render, exact submitted samples, metadata, successful-submit ordering, and no capture after submission failure. |
| `tests/Audio/WasapiAudioPatchTests.cpp` | Prove process-lifetime ownership wiring, injected startup forwarding, and temporary startup formatting. |
| `src/Audio/Mixer/MiniaudioMixer.h/.cpp` | Assign voice IDs and publish voice lifecycle, seek request/application, reset, and render-span records. |
| `src/Audio/DirectSound/DirectSoundFacade.cpp` | Attach requested byte position and last reported frame to the existing `MixerVoice::Seek` call. |
| `tests/Audio/MiniaudioMixerTests.cpp` | Prove 44.1-to-48 kHz source spans, seek/reset generations, loop wraps, and discontinuity reset records. |
| `tests/Audio/SecondarySoundBufferTests.cpp` | Prove `SetCurrentPosition` publishes its requested byte/source context without changing DirectSound results. |
| `src/Patches/Framerate/FrameratePatch.h/.cpp` | Publish readable/unreadable and suppress/allow decisions from the already-owned resync hook without changing its branch. |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Prove resync diagnostic event formatting/publication and retain the runtime binding contract. |
| `tools/analysis/audio_replay_analyzer.py` | Parse checkpointed PCM/timeline data, detect 33–100 ms replay candidates, correlate causal events, extract clips, and write `report.md`. |
| `tools/analysis/tests/test_audio_replay_analyzer.py` | Synthetic clean, 40 ms rewind, 50 ms crossfaded replay, malformed input, checkpoint, and report tests. |
| `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md` | Append-only Stage A build, deployment, capture, analyzer, and user-listening evidence. |

## Design-to-Task Traceability

| Approved requirement | Owning task and acceptance evidence |
|---|---|
| Always-on, temporary, non-configurable recorder | Tasks 2–3; startup/ownership tests and the config-diff audit in Task 7 |
| Exact PCM after successful WASAPI submission | Task 3; sample-identity and failed-submit ordering tests |
| Seek, resampler, source-span, and converter-reset evidence | Task 4; 44.1-to-48 kHz, backward-seek, loop, and reset tests |
| Existing resync decision remains behaviorally identical | Task 5; hook publication tests plus branch/RVA diff review |
| Endpoint clock, presentation, discontinuity, and pacing context | Task 3; endpoint metadata assertions and unchanged runtime counters |
| No render-thread allocation, locking, formatting, logging, or file I/O | Tasks 1–3 and 7; bounded queues plus focused source audit |
| Checkpointed 30-minute capture with explicit incomplete ranges | Task 2; WAV/header, queue-loss, limit, and writer-failure tests |
| 33–100 ms offline replay detection and listening clips | Task 6; synthetic `00`/`07`/`13` verification |
| Hashed deployment and user-owned auditory verdict | Task 8; archive/runtime hash equality, full-song capture, and evidence record |
| Evidence-gated fix and complete later removal | Task 8; hard stop at Stage A and explicit Stage B/Stage C handoff |

### Task 1: Define the Temporary Record Schema and Non-Blocking Queues

**Files:**

- Create: `src/Audio/Diagnostics/AudioFlightRecorder.h`
- Create: `src/Audio/Diagnostics/AudioFlightRecorder.cpp`
- Create: `tests/Audio/AudioFlightRecorderTests.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**

- Consumes: C++23 atomics, `std::span`, PCM16 stereo blocks, and
  `OutputPacingDecisionKind`.
- Produces:

```cpp
namespace gc::audio::diagnostics {

enum class AudioDiagnosticEventKind : std::uint8_t {
    VoiceCreated,
    VoicePlay,
    VoiceStop,
    SeekRequested,
    SeekApplied,
    ConverterReset,
    RenderSpan,
    AudioResync,
};

enum class ConverterResetReason : std::uint8_t {
    Seek,
    OutputDiscontinuity,
};

enum class AudioResyncDecision : std::uint8_t {
    Unreadable,
    SuppressedInMargin,
    AllowedOutOfMargin,
};

struct AudioDiagnosticEvent {
    AudioDiagnosticEventKind kind{};
    std::uint8_t decision{};
    std::uint16_t flags{};
    std::uint32_t signed_value0{};
    std::uint32_t signed_value1{};
    std::uint64_t sequence{};
    std::uint64_t qpc_ticks{};
    std::uint64_t voice_id{};
    std::uint64_t epoch{};
    std::uint64_t generation{};
    std::uint64_t output_frame_begin{};
    std::uint64_t output_frame_end{};
    std::uint64_t source_frame_begin{};
    std::uint64_t source_frame_end{};
    std::uint64_t value0{};
    std::uint64_t value1{};
    std::uint64_t value2{};
    std::uint64_t value3{};
};

struct SubmittedPcmMetadata {
    std::uint64_t endpoint_clock_position{};
    std::uint64_t endpoint_qpc_100ns{};
    std::uint64_t presented_output_frame{};
    std::uint64_t output_frame_begin{};
    std::uint64_t submitted_tail{};
    std::uint64_t discontinuity_frames{};
    std::uint64_t mixer_frames_read{};
    std::int32_t mixer_result{};
    std::uint8_t pacing_kind{};
};

struct AudioFlightRecorderSession {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::uint16_t bits_per_sample{};
    std::uint32_t frames_per_block{};
    std::uint64_t qpc_frequency{};
};

struct PcmPublishResult {
    std::uint64_t sequence{};
    bool queued{};
};

class IAudioDiagnosticSink {
public:
    virtual ~IAudioDiagnosticSink() = default;
    virtual bool StartSession(
        const AudioFlightRecorderSession&) noexcept = 0;
    virtual void PublishEvent(AudioDiagnosticEvent) noexcept = 0;
    virtual PcmPublishResult PublishSubmittedPcm(
        const SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept = 0;
};

void ActivateAudioDiagnosticSink(IAudioDiagnosticSink*) noexcept;
void DeactivateAudioDiagnosticSink(IAudioDiagnosticSink*) noexcept;
std::uint64_t CaptureAudioDiagnosticQpcTicks() noexcept;
void PublishActiveAudioDiagnosticEvent(
    AudioDiagnosticEvent) noexcept;

} // namespace gc::audio::diagnostics
```

`AudioDiagnosticEvent` must remain trivially copyable and at most 128 bytes.
Its factory functions give the generic fields stable event-specific meaning:

| Kind | Stable fields |
|---|---|
| `VoiceCreated` | voice ID, source length, rate, channels, block alignment, usage |
| `VoicePlay` / `VoiceStop` | voice ID, epoch, playback run, looping flag |
| `SeekRequested` | voice ID, epoch, old cursor, target cursor, requested byte, previous reported source frame |
| `SeekApplied` | voice ID, epoch, old cursor, target cursor, mailbox sequence, output frame |
| `ConverterReset` | voice ID, epoch, reason, old cursor, new cursor, output frame |
| `RenderSpan` | voice ID, epoch, output/source begin/end, required/copied/consumed/produced counts, snapshot generation, loop/end flags |
| `AudioResync` | raw QPC ticks, signed drift, signed margin, unreadable/suppressed/allowed decision |

- [x] **Step 1: Add failing queue and active-sink tests**

Create `AudioFlightRecorderTests.cpp` with the repository's existing
`Expect(bool, std::string_view)` harness. The first tests must exercise these
contracts:

```cpp
int TestPcmQueuePreservesSamplesAndSequence();
int TestPcmQueueReportsSequenceGapWithoutBlocking();
int TestEventRingPreservesMultipleProducerSequences();
int TestEventQueueReportsDroppedRecords();
int TestActiveSinkPublishesAndClears();
int TestEventRecordStaysFixedAndTriviallyCopyable();
```

Use deterministic sample blocks:

```cpp
constexpr std::array<std::int16_t, 8> first{
    1, -1, 2, -2, 3, -3, 4, -4};
constexpr std::array<std::int16_t, 8> second{
    5, -5, 6, -6, 7, -7, 8, -8};
```

For the MPSC test, start four producer threads, publish 1,024 events each,
join them, then assert that every retained record has a unique sequence and
valid producer payload. Use a queue large enough to prevent loss in the
preservation test and capacity 8 in the drop test.

- [x] **Step 2: Register the test and verify the missing API fails**

Add only `AudioFlightRecorderTests` to `GC_AUDIO_TESTS`; do not add the
production source to `gc_audio` yet.

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target AudioFlightRecorderTests'
```

Expected: compilation fails because `AudioFlightRecorder.h` does not yet
provide the tested event and queue APIs. A pass means the new target did not
compile the failing test.

- [x] **Step 3: Implement the event factories and active sink**

Create `AudioFlightRecorder.h` and `AudioFlightRecorder.cpp`, then add
`Diagnostics/AudioFlightRecorder.cpp` to `gc_audio`.

Use one inline lock-free active pointer:

```cpp
inline std::atomic<IAudioDiagnosticSink*> active_sink{};

inline void PublishActiveAudioDiagnosticEvent(
    AudioDiagnosticEvent event) noexcept {
    if (auto* const sink =
            active_sink.load(std::memory_order_acquire);
        sink != nullptr) {
        sink->PublishEvent(event);
    }
}
```

Activation uses compare-exchange from `nullptr`; deactivation clears only the
same pointer. Tests must prove a stale owner cannot clear a replacement sink.
Event factories must bit-preserve negative drift/margin values by converting
through `std::int32_t`, not by taking an absolute value.
`VoiceCreated`, `VoicePlay`, `VoiceStop`, `SeekRequested`, and `AudioResync`
stamp `qpc_ticks` with `CaptureAudioDiagnosticQpcTicks`; the helper returns
zero only when `QueryPerformanceCounter` fails. Audio-thread events already
carry an exact output frame and do not make an additional QPC call per voice.

- [x] **Step 4: Implement the SPSC PCM queue**

Allocate header slots and one contiguous PCM sample array in `Initialize`.
`TryPush` must:

1. reserve the next sequence even when full;
2. compare producer and consumer positions without a mutex;
3. return `{sequence, false}` immediately when full;
4. copy the fixed sample count into the selected slot;
5. publish the write position with release ordering; and
6. release-publish a separate `completed_sequence` after either the queued or
   dropped outcome, then signal the recorder event without a lock.

The consumer API is:

```cpp
struct PcmBlockView {
    std::uint64_t sequence{};
    SubmittedPcmMetadata metadata{};
    std::span<const std::int16_t> samples;
};

std::optional<PcmBlockView> TryPeek() noexcept;
void Pop() noexcept;
```

The view remains valid until `Pop`. The producer never overwrites a slot that
the consumer has not popped. At a checkpoint, the writer drains all visible
records through the acquired `completed_sequence`; any absent tail sequence
through that bound is a proven drop, not an in-progress producer.

- [x] **Step 5: Implement the loss-detecting bounded MPSC event queue**

Use a bounded per-slot sequence queue. Initialize slot `i` with
`slot.sequence == i`. A producer compares the slot sequence at
`enqueue_position % capacity`, claims the position with
`compare_exchange_weak`, writes the ordinary payload only after it owns the
slot, assigns that claimed position to `event.sequence`, then release-stores
`enqueue_position + 1` into the slot sequence. The single consumer
acquire-loads that value, copies the payload while producers cannot own the
slot, then release-stores
`dequeue_position + capacity` to return the slot.

Do not overwrite an unconsumed slot: that would race the consumer's ordinary
payload copy. Retry a stale reservation CAS without waiting on any slot,
consumer, or writer; return `Dropped` only when the sequence comparison proves
the queue full. Increment one atomic lost-event counter for every dropped
publication. The writer snapshots the counter at each checkpoint and emits an
`event_gap` covering that checkpoint's QPC/output-frame interval whenever the
count advanced.

The read contract is:

```cpp
enum class EventReadKind : std::uint8_t {
    Empty,
    Ready,
};

struct EventReadResult {
    EventReadKind kind{};
    AudioDiagnosticEvent event{};
};

enum class EventPublishResult : std::uint8_t {
    Queued,
    Dropped,
};

EventPublishResult TryPush(AudioDiagnosticEvent) noexcept;
EventReadResult TryRead() noexcept;
```

Return `Empty` when the next owned position is not yet published. A producer
never spins on a claimed slot, waits for another producer, waits for the
writer, or writes payload bytes unless it owns the slot.

- [x] **Step 6: Run the focused queue tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioFlightRecorderTests && ctest --preset msvc32-release -R "^AudioFlightRecorderTests$" --output-on-failure'
```

Expected: `AudioFlightRecorderTests` passes repeatedly, including the
four-producer case.

- [x] **Step 7: Commit the record and queue foundation**

Run:

```powershell
git add -- src/Audio/Diagnostics/AudioFlightRecorder.h src/Audio/Diagnostics/AudioFlightRecorder.cpp src/Audio/CMakeLists.txt tests/Audio/AudioFlightRecorderTests.cpp tests/Audio/CMakeLists.txt
git commit -m "test: define temporary audio diagnostic queues"
```

### Task 2: Add the Checkpointed WAV and Timeline Writer

**Files:**

- Modify: `src/Audio/Diagnostics/AudioFlightRecorder.h`
- Modify: `src/Audio/Diagnostics/AudioFlightRecorder.cpp`
- Modify: `tests/Audio/AudioFlightRecorderTests.cpp`

**Interfaces:**

- Consumes: Task 1 PCM/event queues and `AudioFlightRecorderSession`.
- Produces:

```cpp
struct AudioFlightRecorderOptions {
    std::filesystem::path root_directory{"audio-diagnostics"};
    std::size_t pcm_queue_blocks{512};
    std::size_t event_queue_records{65'536};
    std::chrono::milliseconds checkpoint_interval{1'000};
    std::uint64_t maximum_seconds{1'800};
};

enum class AudioFlightRecorderState : std::uint8_t {
    Idle,
    Active,
    LimitReached,
    Failed,
    Stopped,
};

struct AudioFlightRecorderStatus {
    AudioFlightRecorderState state{};
    std::filesystem::path session_directory;
    std::uint64_t submitted_blocks{};
    std::uint64_t dropped_pcm_blocks{};
    std::uint64_t lost_events{};
    std::uint64_t checkpointed_blocks{};
    std::string error;
};

class AudioFlightRecorder final : public IAudioDiagnosticSink {
public:
    static std::unique_ptr<AudioFlightRecorder> Create(
        AudioFlightRecorderOptions = {}) noexcept;
    ~AudioFlightRecorder() override;

    bool StartSession(
        const AudioFlightRecorderSession&) noexcept override;
    void PublishEvent(AudioDiagnosticEvent) noexcept override;
    PcmPublishResult PublishSubmittedPcm(
        const SubmittedPcmMetadata&,
        std::span<const std::int16_t>) noexcept override;
    AudioFlightRecorderStatus status() const;
    void StopAndJoin() noexcept;
};
```

Tests poll the production `status().checkpointed_blocks` contract with a
bounded test-side timeout; no test-only wait method is added to the production
class.

- [x] **Step 1: Add failing persistence and failure-path tests**

Extend `AudioFlightRecorderTests.cpp` with:

```cpp
int TestRecorderWritesExactPcm16Wave();
int TestRecorderWritesStableSessionAndTimelineSchema();
int TestCheckpointHeaderExcludesUncheckpointedTail();
int TestPcmGapInsertsMarkedSilenceAndInvalidatesRange();
int TestCaptureLimitStopsRecorderWithoutBlockingPublisher();
int TestWriterFailureLeavesPublishersNonBlocking();
int TestStopDrainsAndFinalizesFiniteTestOwner();
```

Use a unique directory beneath `std::filesystem::temp_directory_path()` and
remove only that exact test directory after each successful assertion. Verify
the 44-byte RIFF header fields directly:

```cpp
Expect(ReadLe32(bytes, 0) == FourCc("RIFF"), "RIFF id");
Expect(ReadLe32(bytes, 24) == 48'000, "sample rate");
Expect(ReadLe16(bytes, 22) == 2, "stereo");
Expect(ReadLe16(bytes, 34) == 16, "PCM16");
Expect(ReadLe32(bytes, 40) == expected_data_bytes, "data size");
```

The gap test uses queue capacity 1, forces a rejected middle block, permits
the next sequence, and asserts that the WAV contains one block of zero samples
at the missing sequence while `timeline.jsonl` contains
`"kind":"pcm_gap"` and `"conclusive":false`.

- [x] **Step 2: Run the tests to prove persistence is absent**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioFlightRecorderTests'
```

Expected: build or test failure because `AudioFlightRecorder` does not yet
create a session or writer.

- [x] **Step 3: Implement deterministic session creation**

`StartSession` validates:

```cpp
session.sample_rate != 0
session.channels == 2
session.bits_per_sample == 16
session.frames_per_block != 0
session.qpc_frequency != 0
```

It computes `maximum_blocks` with checked integer arithmetic:

```cpp
maximum_blocks =
    maximum_seconds * sample_rate / frames_per_block;
```

It creates
`current_path()/audio-diagnostics/YYYYMMDD-HHMMSS`, adding `-01`, `-02`, and
so on only when the exact directory already exists. Before
`endpoint_->Start`, it allocates queues, creates `session.json`,
`submitted.wav`, and `timeline.jsonl`, writes a valid zero-length WAV header,
starts the writer, activates the sink, and returns.

`session.json` schema version 1 contains exactly:

```json
{
  "schema_version": 1,
  "sample_rate": 48000,
  "channels": 2,
  "bits_per_sample": 16,
  "frames_per_block": 480,
  "qpc_frequency": 10000000,
  "maximum_seconds": 1800
}
```

The numeric values come from `AudioFlightRecorderSession`; the example shows
the expected runtime endpoint except that `qpc_frequency` remains the actual
machine value.

- [x] **Step 4: Implement the writer loop and durable checkpoints**

The writer waits on a recorder-owned Win32 event with a timeout equal to the
checkpoint interval. Producers call `SetEvent` only after publishing a slot.
The writer:

1. drains PCM in sequence order;
2. inserts zero PCM only for missing sequences proven by a later retained
   sequence or the acquired SPSC `completed_sequence`;
3. emits one `endpoint_block` JSON object for every retained PCM block;
4. drains causal events and emits event-specific JSON field names;
5. emits `event_gap` for checkpoint intervals whose lost-event counter
   advanced;
6. once per second seeks only its own WAV handle to refresh RIFF/data sizes,
   then restores the file pointer to the exact end of PCM data;
7. flushes the WAV handle;
8. appends a checkpoint object with the last durable PCM/event sequence and
   flushed WAV byte count;
9. flushes the JSONL handle after the checkpoint; and
10. returns to its wait.

This ordering is mandatory: a durable checkpoint may refer only to WAV bytes
already covered by the refreshed header and successful WAV flush. A crash
between the WAV flush and JSONL flush leaves the previous checkpoint
authoritative.

The checkpoint object is:

```json
{
  "kind": "checkpoint",
  "pcm_sequence": 99,
  "event_sequence": 412,
  "wav_data_bytes": 192000,
  "dropped_pcm_blocks": 0,
  "lost_events": 0,
  "conclusive": true
}
```

Do not use `PLOG_*`, `std::ostringstream`, `std::filesystem`, or stream I/O
from any producer method. JSON formatting and file operations stay in the
writer loop.

- [x] **Step 5: Implement limit and failure behavior**

The `maximum_blocks` check occurs before queue reservation. Once reached,
transition `Active -> LimitReached` once, reject later PCM immediately, leave
event publication non-blocking, and ask the writer to emit one
`capture_limit` timeline object.

On directory, open, write, seek, or flush failure, transition to `Failed`,
retain the first error string, deactivate the active sink, and make later
producer calls return immediately. Audio behavior must not change.

`StopAndJoin` is for finite test/startup owners. It deactivates the sink,
signals the writer, drains published slots, finalizes the header, joins, and
is idempotent. Production must not invoke it from `DllMain`.

- [x] **Step 6: Run recorder tests and inspect one artifact**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioFlightRecorderTests && ctest --preset msvc32-release -R "^AudioFlightRecorderTests$" --output-on-failure'
```

Expected: all recorder tests pass. The test itself verifies exact PCM bytes,
JSON schema, gap marking, and final/checkpoint header sizes before removing its
own temporary directory.

- [x] **Step 7: Commit the recorder writer**

Run:

```powershell
git add -- src/Audio/Diagnostics/AudioFlightRecorder.h src/Audio/Diagnostics/AudioFlightRecorder.cpp tests/Audio/AudioFlightRecorderTests.cpp
git commit -m "feat: record checkpointed WASAPI diagnostic audio"
```

### Task 3: Capture Successful Endpoint Blocks and Own the Recorder

**Files:**

- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/Wasapi/WasapiAudioPatch.cpp`
- Modify: `src/Audio/Wasapi/WasapiAudioPatchInternal.h`
- Modify: `tests/Audio/ExclusiveAudioEngineTests.cpp`
- Modify: `tests/Audio/WasapiAudioPatchTests.cpp`

**Interfaces:**

- Consumes: `IAudioDiagnosticSink`, `AudioFlightRecorder`, negotiated endpoint
  format, `OutputPacingDecision`, and final `pcm16_mix_`.
- Produces these temporary signature additions:

```cpp
ExclusiveAudioEngine::StartAndWait(
    std::unique_ptr<IWasapiApi>,
    std::shared_ptr<IAudioEngineObserver>,
    DWORD timeout_ms,
    REFERENCE_TIME configured_duration,
    std::shared_ptr<const ma_allocation_callbacks>,
    diagnostics::IAudioDiagnosticSink*,
    AudioStartupFailure*) noexcept;
```

The internal `StartExclusiveAudioEngineAndWait`,
`StartProductionExclusiveAudioEngine`, fake startup function type, and
constructor carry the same raw sink pointer. Ownership remains in
`ProductionDetourState`.

- [x] **Step 1: Add failing engine capture tests**

Add a fixed-capacity fake sink to `ExclusiveAudioEngineTests.cpp`:

```cpp
class FakeAudioDiagnosticSink final
    : public diagnostics::IAudioDiagnosticSink {
public:
    bool StartSession(
        const diagnostics::AudioFlightRecorderSession& session)
        noexcept override;
    void PublishEvent(
        diagnostics::AudioDiagnosticEvent event) noexcept override;
    diagnostics::PcmPublishResult PublishSubmittedPcm(
        const diagnostics::SubmittedPcmMetadata& metadata,
        std::span<const std::int16_t> samples) noexcept override;

    std::array<std::int16_t, 960> last_samples{};
    diagnostics::AudioFlightRecorderSession last_session{};
    diagnostics::SubmittedPcmMetadata last_metadata{};
    std::atomic_uint32_t pcm_calls{};
    std::atomic_bool start_seen{};
};
```

Add tests proving:

```cpp
int TestRecorderStartsBeforeEndpointRendering();
int TestRecorderStartFailureDoesNotFailAudioEngine();
int TestSuccessfulSubmissionPublishesExactPcmAndTimeline();
int TestReleaseBufferFailurePublishesNoSubmittedPcm();
int TestNullDiagnosticSinkPreservesEngineBehavior();
```

For the successful case, use the existing constant source and one fake render
event. Assert:

- one 960-sample block;
- metadata clock/QPC/presented/block begin/discontinuity values equal the fake
  endpoint and pacing state;
- `submitted_tail == output_frame_begin + 480`;
- the fake endpoint recorded `ReleaseRenderBuffer` before the sink call; and
- the capture call occurs while the existing allocation/render probe reports
  no forbidden allocation.

- [x] **Step 2: Add failing production ownership/formatting tests**

Extend `WasapiAudioPatchTests.cpp` so `fake_start_engine` accepts and records
the forwarded sink. Add an injected recorder status formatter test requiring:

```text
^WASAPI audio diagnostic session status=active directory="audio-diagnostics\\\d{8}-\d{6}(?:-\d{2})?"$
```

Treat the text above as the exact regular expression for the whole log line.
The failure text is:

```text
WASAPI audio diagnostic session status=unavailable
```

Do not append diagnostic fields to the ordinary `WASAPI audio startup` or
30-second runtime summary lines.

- [x] **Step 3: Run the focused tests to verify the signatures fail**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target ExclusiveAudioEngineTests WasapiAudioPatchTests'
```

Expected: compile failures at the new sink parameters and recorder startup
expectations.

- [x] **Step 4: Thread the non-owning sink through engine startup**

Store:

```cpp
diagnostics::IAudioDiagnosticSink* diagnostic_sink_{};
```

in `ExclusiveAudioEngine`. Preserve all existing null checks and startup
failure results. In `AudioThreadMain`, after endpoint format validation and
mixer/buffer allocation but before `endpoint_->Start`, call
`IAudioDiagnosticSink::StartSession`; the concrete recorder remains owned by
production and engine code never downcasts. The session uses:

```cpp
{
    output_sample_rate,
    kOutputChannels,
    kOutputBitsPerSample,
    frames,
    qpc_frequency_,
}
```

A false result does not fail WASAPI startup.

- [x] **Step 5: Publish only successfully committed PCM**

Keep the render order:

```text
ReadClock -> Plan -> Render -> Convert -> SubmitPcm16 -> Commit
-> Publish clock -> Publish diagnostic PCM -> Record counters
```

After successful `SubmitPcm16` and successful pacing commit, call:

```cpp
diagnostic_sink_->PublishSubmittedPcm(
    diagnostics::SubmittedPcmMetadata{
        clock.position,
        clock.qpc_100ns,
        *presented,
        decision.block_begin,
        pacing_tracker_->submitted_tail(),
        decision.discontinuity_frames,
        rendered.frames_read,
        rendered.result,
        static_cast<std::uint8_t>(decision.kind),
    },
    pcm16_mix_);
```

Ignore the returned `queued` flag in audio behavior. The recorder sequence gap
and status own the diagnostic consequence. Do not call the sink before
`SubmitPcm16`, on any failed submit, or for `TrySubmitSilence`.

- [x] **Step 6: Add process-lifetime production ownership**

Add `std::unique_ptr<diagnostics::AudioFlightRecorder> recorder` before
`ProductionExclusiveEngineStartup startup` inside `ProductionDetourState`.
Construct the recorder unconditionally with default options and pass
`recorder.get()` into startup.

The existing allocation order must remain:

```text
recorder -> startup (owns engine) -> cached factory
```

so finite test destruction would destroy the engine before the recorder. Keep
the production `ProductionDetourState` allocation deliberately leaked; do not
add `DLL_PROCESS_DETACH` cleanup.

After `StartAndWait` returns, emit exactly one temporary diagnostic status
line from the game/control thread using the injected platform actions. A null
recorder or failed `StartSession` reports unavailable but does not change the
DirectSound detour result. Later capture state is intentionally read from the
checkpoint/timeline during offline analysis; do not add periodic diagnostic
log lines.

- [x] **Step 7: Run endpoint and patch tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioFlightRecorderTests ExclusiveAudioEngineTests WasapiAudioPatchTests && ctest --preset msvc32-release -R "^(AudioFlightRecorderTests|ExclusiveAudioEngineTests|WasapiAudioPatchTests)$" --output-on-failure'
```

Expected: all three targets pass. Existing startup, failure, pacing, and
runtime-summary assertions remain unchanged except for explicit injected sink
parameters.

- [x] **Step 8: Commit endpoint integration**

Run:

```powershell
git add -- src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Wasapi/WasapiAudioPatch.cpp src/Audio/Wasapi/WasapiAudioPatchInternal.h tests/Audio/ExclusiveAudioEngineTests.cpp tests/Audio/WasapiAudioPatchTests.cpp
git commit -m "feat: capture submitted WASAPI diagnostic blocks"
```

### Task 4: Trace Mixer Voices, Seeks, Converter Resets, and Source Spans

**Files:**

- Modify: `src/Audio/Mixer/MiniaudioMixer.h`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `tests/Audio/MiniaudioMixerTests.cpp`
- Modify: `tests/Audio/SecondarySoundBufferTests.cpp`

**Interfaces:**

- Consumes: Task 3 sink pointer and existing `MixerRenderTimeline`.
- Produces:

```cpp
struct MixerSeekDiagnosticContext {
    std::uint64_t requested_byte_position{};
    std::uint64_t previous_reported_source_frame{};
};

HRESULT MixerVoice::Seek(
    std::uint64_t source_frame,
    std::uint64_t epoch,
    MixerSeekDiagnosticContext = {}) noexcept;
```

Both `MiniaudioMixer::Create` overloads gain a final defaulted parameter:

```cpp
diagnostics::IAudioDiagnosticSink* diagnostic_sink = nullptr
```

The private `CreateWithOwner` and `MiniaudioMixerState` carry the same pointer.
Update the existing `ExclusiveAudioEngine::AudioThreadMain` mixer construction
to pass its stored `diagnostic_sink_` as that final argument.

- [x] **Step 1: Add failing 44.1-to-48 kHz span tests**

In `MiniaudioMixerTests.cpp`, add a fixed-array fake sink and:

```cpp
int TestDiagnosticsRecordConvertedSourceSpans();
int TestDiagnosticsRecordBackwardSeekRequestAndApplication();
int TestDiagnosticsDistinguishSeekAndDiscontinuityResets();
int TestDiagnosticsMarkLegalLoopWrap();
int TestNullDiagnosticsDoNotChangeRenderedPcm();
```

The converted-span test creates a 44,100 Hz PCM16 stereo voice, renders two
480-frame output blocks at 48,000 Hz, and requires:

```text
block 1: source [0, 441), consumed=441, produced=480
block 2: source [441, 882), consumed=441, produced=480
```

It also asserts `required_input`, `copied`, snapshot generation, output frame
range, epoch, voice ID, and no reset event.

The seek test renders forward, calls `Seek` to a lower source frame, renders
again, and requires one `SeekRequested`, one `SeekApplied`, and one
`ConverterReset(Seek)` record with the same voice ID/epoch/target.

- [x] **Step 2: Add a failing DirectSound seek-context test**

In `SecondarySoundBufferTests.cpp`, create a real mixer-backed fake engine
with the sink, call:

```cpp
buffer->GetCurrentPosition(&before, nullptr);
buffer->SetCurrentPosition(8);
```

Assert the `SeekRequested` event records:

- requested byte position `8`;
- source frame `8 / block_align`;
- the pre-seek `last_reported_source_frame_`;
- the next playback generation; and
- unchanged `DS_OK` behavior.

- [x] **Step 3: Run tests to verify diagnostics are absent**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target MiniaudioMixerTests SecondarySoundBufferTests'
```

Expected: compilation or assertion failure because mixer voices have no sink,
ID, or diagnostic events.

- [x] **Step 4: Assign IDs and publish lifecycle/control events**

Add to `MiniaudioMixerState`:

```cpp
diagnostics::IAudioDiagnosticSink* diagnostic_sink{};
std::atomic_uint64_t next_diagnostic_voice_id{1};
```

Add to `MixerVoiceState`:

```cpp
std::uint64_t diagnostic_voice_id{};
```

Assign the ID before converter initialization and publish `VoiceCreated` only
after the node attaches successfully. Publish:

- `VoicePlay` after the new run/epoch is accepted;
- `VoiceStop` only when a nonzero run actually transitions to stopped; and
- `SeekRequested` immediately before the existing mailbox publication.

No diagnostic publication participates in the control decision or changes
the current sequentially consistent mailbox ordering.

- [x] **Step 5: Pass exact DirectSound seek context**

Change only the existing facade call:

```cpp
const auto result = voice_->Seek(
    source_frame,
    generation,
    MixerSeekDiagnosticContext{
        position,
        last_reported_source_frame_,
    });
```

Retain validation, locking, generation advancement, and
`last_reported_source_frame_` assignment exactly as they are.

- [x] **Step 6: Publish seek application and reset reasons**

When `seek_sequence != applied`, capture the old cursor before reset/store and
publish:

1. `ConverterReset(Seek)`;
2. `SeekApplied` with mailbox sequence, old cursor, target, epoch, and the
   current output block begin.

In `AdvanceVoiceAcrossDiscontinuity`, publish
`ConverterReset(OutputDiscontinuity)` with old/new cursor and gap/output
frames only after the cursor mapping succeeds and the converter reset returns
`MA_SUCCESS`.

Diagnostic publication failure must not enter an existing mixer error path.

- [x] **Step 7: Publish every successfully converted render span**

Acquire and retain `view.generation()` beside the snapshot copy. After
`ma_data_converter_process_pcm_frames` succeeds and `new_position` is known,
publish one `RenderSpan` containing:

```text
voice ID, epoch
output begin/end
source begin/new position
required input, copied, consumed
requested output, produced output
snapshot generation
loop-wrapped and source-ended flags
```

Publish no normal render-span event on converter failure. The existing
silence/error behavior remains unchanged.

- [x] **Step 8: Run mixer and facade tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioFlightRecorderTests MiniaudioMixerTests SecondarySoundBufferTests AudioCursorTimelineTests && ctest --preset msvc32-release -R "^(AudioFlightRecorderTests|MiniaudioMixerTests|SecondarySoundBufferTests|AudioCursorTimelineTests)$" --output-on-failure'
```

Expected: all four targets pass, including existing concurrency, generation,
drain, looping, and conversion tests.

- [x] **Step 9: Commit mixer/source tracing**

Run:

```powershell
git add -- src/Audio/Mixer/MiniaudioMixer.h src/Audio/Mixer/MiniaudioMixer.cpp src/Audio/DirectSound/DirectSoundFacade.cpp src/Audio/Wasapi/ExclusiveAudioEngine.cpp tests/Audio/MiniaudioMixerTests.cpp tests/Audio/SecondarySoundBufferTests.cpp
git commit -m "feat: trace audio source progression and seeks"
```

### Task 5: Observe the Existing Game Resync Policy Decision

**Files:**

- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`

**Interfaces:**

- Consumes: the Task 1 active sink and current
  `HookAudioResyncPolicy(safetyhook::Context&)`.
- Produces:

```cpp
namespace gc::framerate::detail {

void PublishAudioResyncDiagnostic(
    std::int32_t drift_ms,
    std::int32_t margin_ms,
    bool readable,
    bool suppressed) noexcept;

} // namespace gc::framerate::detail
```

- [x] **Step 1: Add a failing resync publication test**

In `FramerateRuntimeTests.cpp`, activate a fixed-array fake sink and call:

```cpp
gc::framerate::detail::PublishAudioResyncDiagnostic(
    -17, 48, true, true);
gc::framerate::detail::PublishAudioResyncDiagnostic(
    63, 48, true, false);
gc::framerate::detail::PublishAudioResyncDiagnostic(
    0, 0, false, false);
```

Assert the three events preserve signed drift/margin and classify as
`SuppressedInMargin`, `AllowedOutOfMargin`, and `Unreadable`. Retain the
existing assertion that `AudioResyncPolicy` has a runtime binding.

- [x] **Step 2: Run the test to verify the helper is absent**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests'
```

Expected: compile failure because the temporary helper is undefined.

- [x] **Step 3: Implement game-thread publication**

The helper captures raw `QueryPerformanceCounter` ticks and publishes one
`AudioResync` event through the active sink. QPC failure records zero ticks;
it never changes readability or policy classification.

In `HookAudioResyncPolicy`:

1. initialize drift/margin to zero;
2. on failed stack read or invalid negative margin, publish unreadable and
   return exactly as before;
3. compute widened absolute drift exactly as current code does;
4. compute `suppressed = abs_drift_ms <= margin_ms`;
5. publish readable with that classification; and
6. set `context.eip` only under the same `suppressed` condition.

Do not recompute the interval, change the comparison, log directly, or alter
registers/flags beyond the existing `EIP` bypass.

- [x] **Step 4: Run framerate policy tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests FrameratePatchPlanTests FrameratePatchTransactionTests && ctest --preset msvc32-release -R "^(FramerateRuntimeTests|FrameratePatchPlanTests|FrameratePatchTransactionTests)$" --output-on-failure'
```

Expected: all three targets pass; hook count, RVA, expected bytes, optional
WASAPI selection, and rollback behavior are unchanged.

- [x] **Step 5: Review the hook diff for behavior identity**

Run:

```powershell
git diff -- src/Patches/Framerate/FrameratePatch.cpp
```

Expected: the only policy-body changes are diagnostic publication plus a local
boolean naming the existing `abs_drift <= margin` condition. RVA
`0x002401C4`, epilogue RVA `0x002401D4`, and the branch condition are
unchanged.

- [x] **Step 6: Commit resync observation**

Run:

```powershell
git add -- src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "feat: trace game audio resync decisions"
```

### Task 6: Build the Offline Replay Analyzer and Candidate Extractor

**Files:**

- Create: `tools/analysis/audio_replay_analyzer.py`
- Create: `tools/analysis/tests/test_audio_replay_analyzer.py`

**Interfaces:**

- Consumes: `session.json`, checkpointed `submitted.wav`,
  `timeline.jsonl`, and optional WAV-only controls.
- Produces:

```python
from collections.abc import Sequence


@dataclass(frozen=True)
class Pcm16Wave:
    sample_rate: int
    channels: int
    frames: Sequence[tuple[int, int]]


@dataclass(frozen=True)
class ReplayCandidate:
    start_frame: int
    lag_frames: int
    window_frames: int
    correlation: float
    normalized_error: float
    causal_events: Sequence[dict[str, object]]


@dataclass(frozen=True)
class SessionAnalysis:
    conclusive_frames: int
    incomplete_ranges: Sequence[tuple[int, int]]
    candidates: Sequence[ReplayCandidate]
    causal_findings: Sequence[str]
```

Implement these exact public call shapes:

- `read_checkpointed_pcm16(path: Path, checkpoint_bytes: int) -> Pcm16Wave`
- `read_timeline(path: Path) -> Sequence[dict[str, object]]`
- `scan_replay_candidates(wave: Pcm16Wave, timeline: Sequence[dict[str, object]] = ()) -> Sequence[ReplayCandidate]`
- `analyze_session(session_directory: Path) -> SessionAnalysis`
- `write_analysis_outputs(session_directory: Path, analysis: SessionAnalysis) -> None`

Their complete behavior is defined in Steps 3–6.

- [x] **Step 1: Add failing synthetic analyzer tests**

Use `unittest`, `tempfile`, `wave`, `array`, and deterministic pseudo-random
PCM. Add these exact test classes and methods:

- `WaveParsingTests.test_reads_only_complete_checkpointed_pcm16_frames`
- `WaveParsingTests.test_rejects_non_pcm16_or_non_stereo_input`
- `WaveParsingTests.test_rejects_malformed_jsonl_and_schema_mismatch`
- `ReplayDetectionTests.test_clean_noise_has_no_replay_candidate`
- `ReplayDetectionTests.test_detects_40ms_source_rewind`
- `ReplayDetectionTests.test_detects_50ms_edge_crossfaded_replay`
- `ReplayDetectionTests.test_ignores_candidates_overlapping_pcm_gaps`
- `ReplayDetectionTests.test_correlates_seek_and_resync_events_within_250ms`
- `ReportTests.test_writes_ranked_report_and_bounded_candidate_wavs`

Generate 16 seconds at 48 kHz. Inject artifacts at 3, 7, and 11 seconds.
Rewind copies 40 ms from immediately before the event. Crossfaded replay uses
50 ms from 50 ms earlier and linearly blends the first/last 10 ms. Candidate
start must be within 20 ms of each injected event.

- [x] **Step 2: Run the tests to prove the analyzer is absent**

Run:

```powershell
python -m unittest tools.analysis.tests.test_audio_replay_analyzer -v
```

Expected: import failure because `audio_replay_analyzer.py` does not exist.

- [x] **Step 3: Implement strict RIFF and timeline parsing**

Use the Python standard library only. Parse RIFF chunks explicitly rather than
trusting a stale header after process termination. Accept only:

```text
format tag = 1
channels = 2
bits per sample = 16
sample rate = session.json sample_rate
block align = 4
```

Bound readable data to:

```python
min(
    actual_data_bytes,
    last_checkpoint["wav_data_bytes"],
)
```

Round down to a complete four-byte stereo frame. Preserve every `pcm_gap` or
`event_gap` as an incomplete range. Reject malformed JSON, unknown schema
versions, non-monotonic checkpoints, or a checkpoint larger than the actual
file.

- [x] **Step 4: Implement a two-stage bounded replay scan**

Avoid an all-samples/all-lags quadratic loop.

1. Partition conclusive PCM into 10 ms blocks.
2. Build a normalized signature from 32 evenly spaced stereo frames per block.
3. Compare each signature with lags 1 through 25 blocks.
4. Retain runs of at least three consecutive blocks with cosine correlation
   at least `0.985`.
5. Around each run, calculate full-rate stereo correlation and normalized RMS
   error for 33, 40, 50, 67, and 100 ms windows.
6. Retain candidates with full correlation at least `0.97` and normalized
   error at most `0.25`.
7. Reject any window or delayed source window intersecting an incomplete
   range.
8. Cluster starts within 100 ms, keep the highest score per cluster, and cap
   output at 20 candidates.

The score is:

```python
score = correlation - 0.25 * normalized_error
```

Sort descending by score, then ascending by start frame for deterministic
output.

- [x] **Step 5: Correlate causal evidence**

Normalize `AudioDiagnosticEvent.qpc_ticks` to 100 ns with checked integer
arithmetic using `session.json.qpc_frequency`. For each candidate, attach an
event when either its output frame or normalized QPC lies within 250 ms of the
candidate's endpoint block. Ignore a zero QPC only when that event also lacks
an output frame.

Derive source progression per `(voice_id, epoch)`. Adjacent non-loop
`RenderSpan` records must satisfy
`next.source_frame_begin == previous.source_frame_end`; classify overlap,
backward movement, or a gap unless a seek, loop wrap, or explicit output
discontinuity explains the transition.

Also validate the endpoint sequence: clock position, endpoint QPC,
presentation frame, and submitted tail never regress; sequential block begin
equals the prior submitted tail; and a recoverable discontinuity accounts for
any forward jump. Report these deterministic causal findings before
waveform-only candidates:

- backward `SeekApplied`;
- overlapping non-loop `RenderSpan`;
- `ConverterReset(Seek)`;
- `ConverterReset(OutputDiscontinuity)`;
- allowed out-of-margin `AudioResync`;
- endpoint discontinuity or clock regression;
- PCM/event gap.

A musical waveform match without causal evidence remains a listening
candidate, not a defect verdict.

- [x] **Step 6: Write reports and clips**

`report.md` must contain:

- endpoint/session format;
- last conclusive checkpoint and duration;
- incomplete ranges;
- causal findings;
- a ranked table with start, lag, window, correlation, error, score, and
  nearby events;
- the diagnostic verdict matrix from the design; and
- the explicit warning that clean submitted PCM moves the investigation
  downstream only after the user confirms the live artifact occurred.

Write each candidate as a PCM16 stereo WAV containing two seconds before and
two seconds after its start, clamped to conclusive audio. Name clips
`candidate-001.wav` through `candidate-020.wav`.
Always create the analyzer-owned `candidates` directory, even when no
candidate is retained. On a repeat analysis, replace only
`candidate-001.wav` through `candidate-020.wav` and `report.md`; do not remove
any other session file.

The CLI is:

```powershell
python tools/analysis/audio_replay_analyzer.py 'H:\gc\audio-diagnostics\session-directory'
python tools/analysis/audio_replay_analyzer.py --wav-only 'control.wav' --output-directory 'analysis-output'
```

Exit 0 for a valid conclusive analysis, 2 for incomplete capture, and 1 for
invalid input or writer/schema failure.

- [x] **Step 7: Run unit tests**

Run:

```powershell
python -m unittest tools.analysis.tests.test_audio_replay_analyzer -v
```

Expected: clean/no-artifact, rewind, crossfaded replay, malformed input, gap,
causal correlation, report, and clip tests all pass.

- [x] **Step 8: Verify against the existing listening sweep**

Run:

```powershell
$sweepRoot = 'H:\gc\tmp\audio-issue-identification\resampler-mechanism-sweep'
$analysisRoot = 'H:\gc\tmp\audio-issue-identification\analyzer-verification'
New-Item -ItemType Directory -Force -Path $analysisRoot | Out-Null

python tools/analysis/audio_replay_analyzer.py --wav-only (Join-Path $sweepRoot '00-clean-actual-miniaudio.wav') --output-directory (Join-Path $analysisRoot '00-clean')
if ($LASTEXITCODE -ne 0) { throw '00 control analysis failed.' }

python tools/analysis/audio_replay_analyzer.py --wav-only (Join-Path $sweepRoot '07-rewind-source-40ms-fresh-resampler.wav') --output-directory (Join-Path $analysisRoot '07-rewind')
if ($LASTEXITCODE -ne 0) { throw '07 rewind analysis failed.' }

python tools/analysis/audio_replay_analyzer.py --wav-only (Join-Path $sweepRoot '13-crossfaded-50ms-replay-concealment.wav') --output-directory (Join-Path $analysisRoot '13-crossfade')
if ($LASTEXITCODE -ne 0) { throw '13 crossfade analysis failed.' }
```

Expected:

- `07` ranks candidates within 20 ms of 3, 7, and 11 seconds;
- `13` ranks candidates within 20 ms of 3, 7, and 11 seconds; and
- `00` does not produce equally scored candidates at those same timestamps.

Do not commit the WAVs or generated analyzer outputs.

- [x] **Step 9: Commit the analyzer**

Run:

```powershell
git add -- tools/analysis/audio_replay_analyzer.py tools/analysis/tests/test_audio_replay_analyzer.py
git commit -m "feat: analyze temporary audio replay captures"
```

### Task 7: Verify and Record the Stage A Diagnostic Build

**Files:**

- Create: `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`
- Verify: `build-msvc32-release/dist/iDmacDrv32.dll`

**Interfaces:**

- Consumes: committed Tasks 1–6.
- Produces: one statically verified, hashed Stage A DLL and an append-only
  validation record with runtime status explicitly unaccepted.

- [x] **Step 1: Create the validation record**

Create the document with these concrete sections:

```markdown
# WASAPI Audio Replay Runtime Validation

## Scope and evidence rule

This record separates static/build evidence, captured PCM/timeline evidence,
and the user's live auditory verdict. Stage A observes only and changes no
audio decision.

## Baseline

- Endpoint: PCM16 stereo 48,000 Hz
- Period: 480 frames / 10 ms
- Source under test:
  `H:\gc\data\stage\sound\bgm_b-516_happysyn2_BGM.wav`
- Previous runtime counters: zero late wakes, confirmed gaps, skipped output
  frames, chronic pacing failures, and endpoint HRESULT failures.
- Listening controls: sample `07` source rewind and sample `13` crossfaded
  replay were somewhat similar but not accepted as exact matches.

## Stage A build

Stage A build and deployment identity are recorded by the verification steps
below. Runtime status is not yet exercised.

## Stage A capture

No Stage A capture has been analyzed yet.

## User verdict

No Stage A live auditory verdict has been supplied yet.

## Root-cause classification

Unclassified. No correction is authorized by Stage A static evidence.

## Mandatory future cleanup

The recorder, event publications, analyzer, temporary tests, capture
directories, and diagnostic log lines must remain through the corrected
diagnostic run and be removed from the final production DLL.
```

- [x] **Step 2: Run Python verification**

Run:

```powershell
python -m unittest tools.analysis.tests.test_audio_replay_analyzer -v
```

Expected: all analyzer tests pass.

- [x] **Step 3: Build the exact focused x86 slice**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32 AudioFlightRecorderTests AudioCursorTimelineTests MiniaudioMixerTests SecondarySoundBufferTests OutputPacingTrackerTests WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests FramerateRuntimeTests FrameratePatchPlanTests FrameratePatchTransactionTests && ctest --preset msvc32-release -R "^(AudioFlightRecorderTests|AudioCursorTimelineTests|MiniaudioMixerTests|SecondarySoundBufferTests|OutputPacingTrackerTests|WasapiEndpointTests|ExclusiveAudioEngineTests|WasapiAudioPatchTests|FramerateRuntimeTests|FrameratePatchPlanTests|FrameratePatchTransactionTests)$" --output-on-failure'
```

Expected: all eleven focused targets pass and
`build-msvc32-release/dist/iDmacDrv32.dll` links.

- [x] **Step 4: Audit the real-time source boundary**

Run:

```powershell
rg -n "ofstream|fstream|WriteFile|FlushFileBuffers|filesystem|ostringstream|PLOG_|sleep_for|wait_for|mutex" src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Mixer/MiniaudioMixer.cpp
git diff d799e77..HEAD -- src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Mixer/MiniaudioMixer.cpp
```

Expected: existing control-path mutex/thread uses may remain, but the owned
diff inside `RenderLoop`, `VoiceNodeProcess`, seek application, and
discontinuity advancement contains only fixed-record construction, PCM copy,
atomic publication, and non-blocking wake signaling. No new formatting,
logging, file operation, allocation, lock, wait, or sleep appears there.

- [x] **Step 5: Audit behavior and configuration non-changes**

Run:

```powershell
rg -n "audio.*diagnostic.*=" config.toml src/Config tools/ConfigGUI tests/Config
rg -n "ma_resample_algorithm_linear|lpfOrder = 0" src/Audio/Mixer/MiniaudioMixer.cpp
rg -n "kAudioResyncEpilogueRva|abs_drift_ms <=|HookAudioResyncPolicy" src/Patches/Framerate/FrameratePatch.cpp
git diff d799e77..HEAD -- config.toml src/Config tools/ConfigGUI
```

Expected:

- no diagnostic configuration key;
- linear resampling and `lpfOrder = 0` remain;
- the same in-margin comparison and epilogue target remain;
- no config or Config GUI diff.

- [x] **Step 6: Verify source scope and cleanup markers**

Run:

```powershell
git diff --name-status d799e77..HEAD
rg -n "AudioFlightRecorder|AudioDiagnostic|audio-diagnostics|audio replay diagnostic" src tests tools
git diff --check
git status --short
```

Expected: only the file map in this plan, the approved spec/plan commits, and
the validation record differ from `d799e77`. Preserve the `rg` result as the
mechanical Stage C removal manifest. Diff check exits zero.

- [x] **Step 7: Verify machine type and hash the candidate**

Run:

```powershell
$candidate = (Resolve-Path -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll').Path
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
Get-Item -LiteralPath $candidate | Select-Object FullName,Length,LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
```

Expected: PE machine is x86 and one candidate SHA-256 is printed.

- [x] **Step 8: Append build identity and commit validation**

Append the current implementation commits, test results, candidate path,
length, timestamp, and SHA-256 beneath `## Stage A build`. Keep capture,
user-verdict, and root-cause sections explicitly unexercised/unclassified.

Run:

```powershell
git add -- docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: record audio replay diagnostic build"
```

### Task 8: Archive, Deploy, Capture, Analyze, and Stop at Root Cause

**Files and runtime artifacts:**

- Candidate:
  `H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll`
- Runtime destination: `H:\gc\iDmacDrv32.dll`
- Archive root:
  `H:\gc\artifacts\runtime-builds\wasapi-audio-replay\stage-a-diagnostic`
- Rollback root:
  `H:\gc\deploy-backups`
- Generated capture root:
  `H:\gc\audio-diagnostics`
- Modify after evidence:
  `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`

**Interfaces:**

- Consumes: the verified Stage A DLL and supplied BGM.
- Produces: one complete capture, ranked listening clips, a user auditory
  verdict, and one evidence-backed root-cause classification. It produces no
  fix.

- [x] **Step 1: Refuse deployment while the game is running**

Run:

```powershell
if (Get-Process -Name game471 -ErrorAction SilentlyContinue) {
    throw 'game471.exe is running; close it before DLL backup/deployment.'
}
```

Expected: no output. If it throws, ask the user to close the game. Do not
terminate it automatically.

- [x] **Step 2: Archive the immutable Stage A candidate**

Run:

```powershell
$candidate = (Resolve-Path -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll').Path
$candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash
$archiveRoot = 'H:\gc\artifacts\runtime-builds\wasapi-audio-replay\stage-a-diagnostic'
$archiveDirectory = Join-Path $archiveRoot $candidateHash
$archivedDll = Join-Path $archiveDirectory 'iDmacDrv32.dll'

if (-not (Test-Path -LiteralPath $archiveDirectory)) {
    New-Item -ItemType Directory -Path $archiveDirectory | Out-Null
    Copy-Item -LiteralPath $candidate -Destination $archivedDll
} elseif (-not (Test-Path -LiteralPath $archivedDll)) {
    throw "Stage A archive directory lacks its DLL: $archiveDirectory"
}

$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivedDll).Hash
if ($archiveHash -ne $candidateHash) {
    throw 'Archived diagnostic DLL hash mismatch.'
}
[pscustomobject]@{
    CandidateHash = $candidateHash
    Archive = $archivedDll
} | Format-List
```

Expected: archive path contains the candidate hash and the two hashes match.

- [x] **Step 3: Back up the live DLL and deploy**

Run:

```powershell
$candidate = (Resolve-Path -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll').Path
$runtime = (Resolve-Path -LiteralPath 'H:\gc\iDmacDrv32.dll').Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDirectory = Join-Path 'H:\gc\deploy-backups' "wasapi-audio-replay-stage-a-$stamp"
$rollback = Join-Path $backupDirectory 'iDmacDrv32.pre-diagnostic.dll'

if (Test-Path -LiteralPath $backupDirectory) {
    throw "Rollback directory already exists: $backupDirectory"
}
New-Item -ItemType Directory -Path $backupDirectory | Out-Null
Copy-Item -LiteralPath $runtime -Destination $rollback
Copy-Item -LiteralPath $candidate -Destination $runtime -Force

$candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash
$runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime).Hash
if ($candidateHash -ne $runtimeHash) {
    throw 'Deployed DLL hash does not match the Stage A candidate.'
}
[pscustomobject]@{
    CandidateHash = $candidateHash
    RuntimeHash = $runtimeHash
    Rollback = $rollback
} | Format-List
```

Expected: hashes match and the exact rollback path is printed. Record it in
the validation document. Do not alter `config.toml`.

- [ ] **Step 4: Run the automatic capture**

User-owned runtime procedure:

1. Start the game normally.
2. Confirm `loader-log.txt` contains
   `WASAPI audio diagnostic session status=active` and the exact session
   directory.
3. Confirm the ordinary startup line still reports PCM16/48,000 Hz/stereo,
   480 frames, and 10 ms.
4. Play
   `data\stage\sound\bgm_b-516_happysyn2_BGM.wav` for the complete song.
5. Note only whether the live artifact occurred at least once; no hotkey or
   timestamp is needed.
6. Remain in the game for at least five seconds after the song so the final
   song region is inside a durable checkpoint.
7. Exit the game normally.

If startup reports unavailable, do not continue the run. If offline analysis
later reports writer failure, queue loss over the suspect region, or capture
limit before the song ends, do not interpret that run.

- [ ] **Step 5: Verify runtime health and capture completeness**

Run:

```powershell
$sessionLine = Select-String -LiteralPath 'H:\gc\loader-log.txt' -Pattern 'WASAPI audio diagnostic session status=active' | Select-Object -Last 1
if ($null -eq $sessionLine) {
    throw 'No active audio diagnostic session line was recorded.'
}
$sessionMatch = [regex]::Match(
    $sessionLine.Line,
    'directory="([^"]+)"')
if (-not $sessionMatch.Success) {
    throw 'The active diagnostic line did not contain a session directory.'
}
$sessionDirectory = $sessionMatch.Groups[1].Value
if (-not [System.IO.Path]::IsPathRooted($sessionDirectory)) {
    $sessionDirectory = Join-Path 'H:\gc' $sessionDirectory
}
$sessionDirectory = (Resolve-Path -LiteralPath $sessionDirectory).Path
$sessionLine.Line
$sessionDirectory

rg -n "WASAPI audio runtime summary|audio diagnostic" 'H:\gc\loader-log.txt'
rg -n '"kind":"(pcm_gap|event_gap)"' (Join-Path $sessionDirectory 'timeline.jsonl')
```

Then inspect the exact directory printed in the startup line:

```powershell
Get-Item -LiteralPath $sessionDirectory | Select-Object FullName,CreationTime,LastWriteTime
Get-ChildItem -LiteralPath $sessionDirectory | Select-Object Name,Length,LastWriteTime
```

Expected:

- `session.json`, `submitted.wav`, and `timeline.jsonl` exist;
- analyzer/session state contains no writer failure or premature limit;
- runtime summaries retain zero new late wakes, confirmed gaps, skipped
  frames, chronic pacing failures, and endpoint HRESULT failures;
- startup-only silence fallback count does not increase during gameplay.

If any pacing counter regresses from baseline, mark the diagnostic build as
timing-invalid before interpreting audio content.

- [ ] **Step 6: Run automatic analysis**

From the source repository, run:

```powershell
python tools/analysis/audio_replay_analyzer.py $sessionDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Audio replay analyzer reported incomplete or invalid capture: $LASTEXITCODE"
}
Get-Content -LiteralPath (Join-Path $sessionDirectory 'report.md')
Get-ChildItem -LiteralPath (Join-Path $sessionDirectory 'candidates') -Filter '*.wav' | Sort-Object Name | Select-Object Name,Length
```

Expected: analyzer exits zero, identifies the conclusive duration covering the
full song, and writes a ranked report plus zero to twenty candidate WAVs.

- [ ] **Step 7: Obtain the user listening verdict**

Ask the user to:

1. listen to ranked candidates in order;
2. identify whether any candidate reproduces the live artifact;
3. if none do, listen to `submitted.wav` through the song once; and
4. report whether the live run contained the artifact while the submitted
   recording remained clean.

Classify only from combined evidence:

| Timeline/submitted evidence | Classification |
|---|---|
| Backward `SeekApplied` aligned with repeated PCM | Application source rewind, sample `07` class |
| No seek, but overlapping/reused source span aligned with repeated PCM | Mixer cursor/input reuse |
| Monotonic source spans with repeated submitted PCM | Final application mix/replay, sample `13` class |
| Submitted artifact aligned with endpoint clock/discontinuity | Application/endpoint scheduling boundary |
| User heard live artifact but full submitted recording is clean | Downstream driver/wireless/DAC/headset |
| No user-recognized artifact or capture gap covers candidate | Inconclusive; repeat Stage A unchanged |

- [ ] **Step 8: Append the evidence and commit**

Update the validation document with:

- Stage A implementation commit range;
- candidate/runtime/archive/rollback paths and SHA-256;
- session directory and file sizes;
- checkpoint/conclusive duration and any incomplete ranges;
- exact runtime counter excerpts;
- top analyzer candidates and correlated events;
- the user's words about live and captured audio;
- one classification from the table or `Inconclusive`; and
- explicit statement that no correction has yet been implemented.

Run:

```powershell
git add -- docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: record audio replay diagnostic evidence"
```

- [ ] **Step 9: Stop before changing audio behavior**

Do not implement a correction in this plan.

If classified, write a new Stage B design amendment/implementation plan for
that one causal boundary while retaining the recorder. If inconclusive, repeat
Task 8 with the same DLL and settings. After a corrected diagnostic run is
accepted by the user, write and execute the mechanical Stage C removal plan
against the cleanup manifest captured in Task 7.
