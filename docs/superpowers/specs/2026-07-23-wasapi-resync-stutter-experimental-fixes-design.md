# WASAPI Resync Stutter Experimental Fixes Design

Date: 2026-07-23

## Status and relationship to earlier designs

This is an approved experimental design. Implementation and operator
acceptance are pending.

It follows the runtime and binary finding recorded in
[`E-040`](../../../.planning/debug/high-fps-timing-domains/evidence/E-040-wasapi-periodic-resync-stutter.md)
and narrowly amends these designs:

- [`2026-07-12-wasapi-exclusive-low-latency-audio-design.md`](2026-07-12-wasapi-exclusive-low-latency-audio-design.md);
- [`2026-07-14-wasapi-fixed-period-clock-pacing-design.md`](2026-07-14-wasapi-fixed-period-clock-pacing-design.md);
- [`2026-07-17-wasapi-exclusive-48khz-output-design.md`](2026-07-17-wasapi-exclusive-48khz-output-design.md);
- [`2026-07-18-configurable-fixed-framerate-design.md`](2026-07-18-configurable-fixed-framerate-design.md).

The selected DirectSound facade, miniaudio mixer, fixed-period exclusive
endpoint, 48 kHz fallback, and configurable framerate architecture remain in
place. This design changes only:

1. how the game watchdog's interval-only seek is handled while the committed
   audio backend is WASAPI exclusive; and
2. how game-thread DirectSound cursor reads obtain endpoint presentation time.

The previous statement that `SkipMargin` and `SkipInterval` behavior is wholly
unchanged is superseded only for an interval-only seek on the WASAPI backend.
The previous resync observation hook becomes a silent decision hook.

## Evidence baseline

The 240 FPS capture and live `game471.exe` analysis established:

- `GC120FPS_GameplayAudioSync_CheckAndSeek` at EA `0x00640070`
  (RVA `0x00240070`) enters its seek block when absolute BGM drift exceeds
  `SkipMargin` or when a positive `SkipInterval` divides the gameplay frame
  sum exactly.
- The shipped values are `GameTimeOffset = 0`, `SkipMargin = 48` ms, and
  `SkipInterval = 180` authored frames.
- The framerate patch correctly scales the interval to 720 target frames at
  240 FPS, retaining a three-second wall-time cadence.
- The captured song produced 32 seek entries 2.999 to 3.010 seconds apart.
  Every entry was interval-driven and only 2 to 19 ms from the expected
  position; none exceeded the 48 ms margin.
- Every accepted facade `SetCurrentPosition` creates a new playback generation.
  The next mixer block hard-jumps the source cursor and resets its converter.
  There is no no-op coalescing, slew, or crossfade.
- The stage BGM sources were 44.1 kHz while the endpoint ran at 48 kHz. The
  periodic hard jump therefore also restarted active sample-rate conversion.
- The render path recorded no endpoint error, missed output interval, skipped
  frame, chronic pacing failure, or render-work deadline overrun. Buffer size
  and render starvation do not explain the repeating stutter.
- During gameplay, cursor resolution synchronously entered the endpoint clock
  approximately 480 times per second. This path takes
  `endpoint_service_mutex_` and calls `IAudioClock` from the game thread.
- Buffer snapshot copying stopped before gameplay. It cannot explain continuing
  in-song micro-freezes.
- The detailed render, cursor, and buffer diagnostics were added only for this
  investigation. They cannot explain the pre-existing report and are no longer
  needed. The resync observation log will also be removed so it cannot confound
  the correction experiment.

The periodic hard seek is the high-confidence cause of the audible repeating
or skipping. The synchronous clock path is a credible game-thread hitch
boundary, but its responsibility for the reported visual micro-freezes remains
an experimental question.

## Decisions

- Apply the seek-policy change only when the WASAPI DirectSound detour is
  successfully committed.
- Leave passthrough DirectSound behavior unchanged.
- Preserve `GameTimeOffset`, the 48 ms margin policy, and framerate-aware
  `SkipInterval` scaling.
- Treat arrival at the final seek block with drift inside or equal to the
  margin as an interval-only request.
- Suppress that interval-only request before it reaches the stage-BGM group
  setter.
- Continue to execute every genuine out-of-margin correction unchanged.
- Do not coalesce or soften other explicit game seeks.
- Make the audio/render thread the sole caller of the endpoint's
  `IAudioClock`.
- Publish a coherent presentation-frame and QPC snapshot from that thread.
- Resolve game-thread cursors by reading and extrapolating the published
  snapshot without a project mutex, COM call, driver call, allocation, or
  logging.
- Preserve `DSBCAPS_GETCURRENTPOSITION2` play-cursor semantics. Do not return
  the projected write cursor and do not add a fixed latency bias.
- Remove the temporary render, cursor, buffer-copy, and resync diagnostic
  instrumentation. Add no replacement counters, samples, or periodic summary
  fields.
- Add no new configuration key. These corrections are part of the already
  experimental WASAPI-exclusive backend.
- Require a normal production build, binary preflight, static owned-diff
  review, and focused operator A/B. Do not create a new automated test suite
  for this debug experiment.

## Goals

- Eliminate the approximately three-second repeating or skipping caused by
  harmless periodic BGM re-anchors.
- Retain the game's ability to correct a real BGM divergence larger than
  `SkipMargin`.
- Remove synchronous endpoint-clock serialization from the gameplay thread.
- Keep the game-visible play cursor smooth, monotonic, and bounded by audio
  already submitted to the endpoint.
- Determine through a focused runtime experiment whether removing the periodic
  seek and synchronous clock boundary also removes the reported micro-freezes.
- Restore production hot paths after the completed diagnostic capture.

## Non-goals

- Increasing or dynamically adapting the endpoint buffer.
- Changing endpoint period negotiation, 44.1-to-48 kHz conversion, mixer block
  size, or WASAPI fallback policy.
- Changing `GameTimeOffset`, `SkipMargin`, judgement timing, chart timing, or
  the wall-time frequency of `SkipInterval`.
- Falsifying the DirectSound play cursor to hide drift.
- Returning the write cursor in place of the play cursor.
- Suppressing an out-of-margin correction.
- Coalescing arbitrary `SetCurrentPosition` calls inside the DirectSound
  facade.
- Crossfading, slewing, or time-stretching genuine seeks.
- Redesigning buffer snapshot publication. Its measured work ended before
  gameplay.
- Claiming that the clock path caused the original micro-freezes before the
  experiment is accepted.
- Retaining investigation-only telemetry or adding a debug test phase.

## Selected architecture

### Backend commitment state

`WasapiAudioPatch` publishes one process-lifetime, read-only commitment state.
It becomes true only after the `DirectSoundCreate8` detour has been
successfully enabled and its installation transaction has committed. A config
request alone is not sufficient.

Initialization already installs the WASAPI audio hook before the framerate
transaction. Framerate preflight reads the committed state once and uses it to
select the resync-policy hook contract. There is no runtime backend switch or
fallback, so the selected state remains immutable for the process lifetime.

If the state is absent or false, the resync-policy hook is not installed. This
makes original DirectSound behavior the fail-open result and avoids a backend
check on every watchdog visit.

The WASAPI policy hook is selected independently of whether the framerate
profile is native or transformed. At transformed rates, the existing
skip-margin and skip-interval conversions remain unchanged. At native 60 FPS,
WASAPI may select this one audio-policy hook in addition to the outer cadence
hook; no other high-framerate transformation is enabled.

### Interval-only seek filter

The existing mid-hook at RVA `0x002401C4` is renamed from
`AudioResyncDiagnostic` to an audio resync policy or equivalent behavior name.
The hook remains owned by the framerate transaction because that transaction
already owns the executable byte contract, the skip-margin/interval hooks, and
rollback.

At this address the original function has already decided to enter the final
seek block. The hook reconstructs only the values needed for the decision:

- signed drift from stack slot `[ebp-0x0C]`;
- configured margin from stack slot `[ebp-0x24]`.

It computes absolute drift in a widened signed type so `INT32_MIN` cannot
overflow. The policy is:

```text
if WASAPI hook was not committed:
    execute the original seek block
else if the stack values cannot be read or the margin is invalid:
    execute the original seek block
else if abs(drift_ms) > margin_ms:
    execute the original seek block
else:
    jump to the original function epilogue
```

The original comparison is strictly greater than the margin. Equality is
therefore an interval-only request and is suppressed.

No interval recomputation is needed. If execution reached this block without
exceeding the margin, the already-proven binary control flow establishes that
the periodic condition admitted it. This keeps the hook independent of target
FPS and avoids duplicating the game's modulo calculation.

Suppression jumps to EA `0x006401D4` (RVA `0x002401D4`), bypassing
`GC120FPS_CSoundManager_SetStageBgmGroupPositionMs` and both downstream
secondary-buffer seeks. Registers and flags not explicitly used for the branch
remain as the original epilogue expects.

The hook performs no stream formatting, logging, allocation, clock query, file
I/O, or counter update.

### Hook preflight and rollback

The existing expected-byte contract at RVA `0x002401C4` remains mandatory.
Preflight also verifies that the skip target at RVA `0x002401D4` is the
expected epilogue boundary for the supported executable.

Unexpected bytes, an unavailable hook target, or an installation failure uses
the existing transactional failure path. Installed hooks and direct writes are
rolled back. The process must not continue with a partially installed
framerate/audio policy.

No executable patch is installed for this policy when WASAPI is not committed.
Passthrough DirectSound retains:

- the original periodic hard seek at 60 FPS; and
- the currently implemented margin and interval scaling behavior at
  transformed rates.

### Published endpoint clock

Add a single-writer, multi-reader `PresentedClockPublication` owned by
`ExclusiveAudioEngine`. Its logical payload is:

```text
valid
presented_output_frame
sample_qpc_100ns
submitted_output_frame_end
```

The audio thread is the only writer. It uses the same bounded
sequence-protected publication pattern already used by
`AudioCursorTimeline`: mark a sequence as being written, store the payload,
then publish a stable even sequence with release ordering. Readers make a
bounded number of attempts and accept a payload only when the before and after
sequence values are equal and stable.

All shared fields use lock-free atomic storage supported by the 32-bit target.
The reader never waits for the writer and never takes
`endpoint_service_mutex_`.

The publication uses the endpoint/output frame domain already produced by
`EndpointClockMapper`; it never exposes the raw device-frequency position to
game threads.

### Audio-thread publication order

The render thread retains its existing `IAudioClock` read at each endpoint
event. After a successful clock mapping, mixer render, endpoint submission,
and pacing-tracker commit, it publishes:

- the mapped presentation frame from that event's clock sample;
- the clock sample's endpoint-correlated QPC value;
- the newly committed `submitted_tail`.

Publishing after successful submission ensures the extrapolation bound refers
only to audio actually released to WASAPI. A failed submission never expands
the published range.

The initial state is invalid. Until the first post-start render publication,
`CurrentOutputFrame` returns its existing no-clock fallback. On shutdown, the
audio thread invalidates the publication before releasing endpoint-owned
state.

Endpoint clock regression, invalid mapping, and endpoint HRESULT failures keep
their existing render-thread failure behavior. They are not hidden by the
publication.

### Game-thread projection

`CurrentOutputFrame` no longer dereferences `endpoint_`, locks
`endpoint_service_mutex_`, calls `IAudioClock`, or mutates failure state.

It:

1. reads one coherent published payload;
2. obtains the current QPC from the cached process QPC frequency;
3. calculates elapsed 100 ns units since `sample_qpc_100ns`;
4. converts elapsed time to output frames with checked integer floor
   arithmetic;
5. adds that projection to `presented_output_frame`;
6. clamps the result to the inclusive last submitted frame,
   `submitted_output_frame_end - 1`;
7. monotonically publishes the last returned output frame with a lock-free
   atomic maximum; and
8. returns that frame to the existing source-timeline resolver.

The floor conversion is:

```text
elapsed_frames =
    floor(elapsed_100ns * output_sample_rate / 10,000,000)
```

It must use overflow-safe quotient/remainder arithmetic rather than an
unchecked wide product.

The projection represents estimated presentation progress, not write-cursor
progress. Bounding it by the submitted tail prevents the game from observing
audio that has not been queued. Clamping to the last inclusive frame also
keeps the result inside the timeline's half-open render spans.

A stale snapshot naturally advances only to its submitted bound and then
freezes until a newer publication arrives. No arbitrary latency is added.

If the reader cannot obtain a coherent payload, QPC is unavailable or
regresses, arithmetic overflows, the payload is invalid, or its submitted range
is empty, it returns the last successfully projected frame when one exists.
Before the first successful projection it returns no value, preserving the
facade's existing stored-cursor fallback.

There is deliberately no synchronous endpoint-clock fallback. A publication
failure must not reintroduce the game-thread COM/driver boundary.

### DirectSound cursor contract

`SecondarySoundBuffer::GetCurrentPosition` continues to resolve the projected
output frame through the current playback generation's
`AudioCursorTimeline`. Pending-generation, unmapped, stopped, playing,
draining, looping, and source-rate conversion semantics remain unchanged.

The play cursor remains derived from estimated endpoint presentation. The
write cursor remains the existing actual endpoint-packet projection ahead of
that play cursor. No fixed `GameTimeOffset`, packet, converter, or endpoint
latency is subtracted from the play cursor.

### Temporary diagnostics removal

The implementation removes the investigation-only diagnostic changes:

- delete `AudioDiagnostics.cpp` and `AudioDiagnostics.h`;
- remove their CMake entry and includes;
- remove render-event, render-clock, mixer, submission, render-work, cursor,
  buffer-lock, and buffer-unlock timing probes;
- remove the temporary performance fields from runtime summary formatting;
- remove the facade diagnostic callbacks added only for buffer timing;
- remove per-seek `PLOG_INFO` output and the resync reason counters;
- remove those resync counters from the framerate periodic status line.

Existing production startup reporting, fatal error reporting, pacing counters,
and mixer/cursor correctness counters from the accepted WASAPI design remain.
They are not expanded for this experiment.

Neither the silent seek policy nor the published-clock read path emits new
telemetry.

## Failure handling

- WASAPI commitment cannot be proven: omit the policy hook and preserve the
  original seek.
- Seek-site state cannot be safely classified: execute the original seek.
- Drift exceeds the configured margin: execute the original seek.
- Hook bytes or epilogue bytes differ: fail the transaction before mutation.
- Hook installation fails: use the existing complete rollback and fail-closed
  startup path.
- No coherent clock publication exists: return the last stable frame or the
  facade's existing stored cursor before first publication.
- QPC conversion is invalid or overflows: return the last stable frame.
- The clock publication is stale: stop at its submitted bound; never query the
  endpoint from the game thread.
- The render thread receives an endpoint clock or submission failure: preserve
  the existing fatal audio failure path.
- Shutdown begins: invalidate publication before endpoint teardown and never
  expose endpoint lifetime to readers.

None of these cases silently increases the buffer, changes backend, changes
`GameTimeOffset`, or converts a genuine margin correction into a no-op.

## Component changes

### `WasapiAudioPatch`

- Expose the successfully committed, process-lifetime backend state.
- Publish true only after the DirectSound detour transaction commits.
- Publish false for disabled configuration and complete rollback.
- Do not expose configuration intent as active state.

### Framerate patch plan and transaction

- Rename the diagnostic hook ID, storage, callback, and byte-contract label to
  describe resync policy.
- Select the policy contract only for committed WASAPI.
- Allow native timing to select the outer-frame hook plus this WASAPI-specific
  policy hook without selecting other transformed hooks.
- Retain skip-margin and skip-interval conversions exactly as they are for
  transformed timing.
- Preflight the original seek-block instruction and the epilogue skip target.
- Remove resync logging and counters.

### `ExclusiveAudioEngine`

- Own the QPC frequency, clock publication, and last returned frame.
- Publish after successful endpoint submission and pacing commit.
- Resolve `CurrentOutputFrame` exclusively from the publication.
- Remove `endpoint_service_mutex_` once it has no remaining cross-thread owner.
- Keep endpoint creation, clock reads, render submission, shutdown, and
  destruction on the audio thread.
- Remove temporary performance diagnostics.

### DirectSound facade

- Keep cursor resolution and `SetCurrentPosition` semantics unchanged.
- Remove temporary buffer lock/unlock timing callbacks.
- Do not add a special BGM identity check or generic seek coalescing.

## Verification and acceptance

This experimental debug phase does not add an automated test target, fixture,
or broad regression suite. Verification is proportional to the two narrow
changes.

### Production build and static review

- Build the normal 32-bit production DLL through the supported MSVC/CMake
  preset.
- Confirm the supported executable passes the seek-block and epilogue
  preflight.
- Review the owned diff to establish that the suppress branch exists only in
  the committed-WASAPI hook plan.
- Establish that passthrough DirectSound does not install the policy hook.
- Establish that out-of-margin drift continues into the original setter.
- Establish that an in-margin arrival skips directly to the original epilogue.
- Establish that `CurrentOutputFrame` contains no endpoint dereference,
  `IAudioClock` call, endpoint-service mutex, logging, or allocation.
- Establish that all live endpoint `ReadClock` calls are audio-thread-owned.
- Establish that temporary diagnostic source, calls, and summary fields are
  removed.
- Run the repository's existing verification required by the normal production
  build, but create no new debug-only tests.

### Focused 240 FPS runtime A/B

The existing capture is the baseline; it need not be repeated unless the
machine, endpoint, 10 ms fixed buffer, config, song, or 240 FPS external cap
changes. The experimental run uses the production DLL implementing this design
under those same conditions. Play for at least 90 seconds so approximately 30
original watchdog intervals would occur.

Acceptance requires:

- no repeating, skipping, or stutter at the former approximately three-second
  cadence;
- no new continuous drift between BGM and chart;
- no audible regression during ordinary song start, playback, and song end;
- no endpoint startup or runtime failure;
- no operator-observed visual micro-freezes attributable to the previous
  in-song cadence or cursor path;
- gameplay and judgement behavior otherwise unchanged.

Passthrough DirectSound is outside this runtime A/B because the hook-plan
review establishes that it does not install the policy. A native-60-FPS
matrix, buffer-size matrix, artificial underrun, or synthetic seek suite is
not required for this experiment.

The audio-stutter result and micro-freeze result are reported separately. If
the three-second audio stutter disappears but visual micro-freezes remain, the
seek root cause is accepted while the clock-path experiment is not claimed as
the complete visual fix.

## Rollback and promotion

The experimental behavior makes no persistent game-data or configuration
mutation. Rollback is the prior production DLL or disabling the existing
WASAPI-exclusive backend.

The design is promoted from experimental only after the focused 240 FPS
operator run accepts audio continuity and BGM/chart synchronization. The
published-clock path is credited with resolving micro-freezes only if the
operator observation changes accordingly; static plausibility alone is not
runtime proof.
