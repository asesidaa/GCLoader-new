# E-040: WASAPI periodic BGM resync stutter

## Scope and evidence

This finding combines:

- the 240 FPS runtime capture in `H:\gc\loader-log.txt` from
  2026-07-23 23:41:59 through 23:44:39;
- a live `idalib` daemon connected to
  `H:\gc\game471.exe.i64`;
- the current DirectSound facade, mixer, WASAPI clock, framerate diagnostic,
  and session-log source.

The IDA requests were read-only. The daemon remained running after the trace.
No production fix was applied as part of this investigation.

## Original game watchdog

`GC120FPS_GameplayAudioSync_CheckAndSeek` at EA `0x00640070`
(RVA `0x00240070`) is a game-level stage-BGM synchronization watchdog.
It:

1. derives expected BGM milliseconds from `Tune+0x10` plus `Tune+0x14`;
2. subtracts `GameTimeOffset`;
3. verifies that DirectSound group 2 is playing;
4. obtains group 2's play cursor in milliseconds;
5. computes the signed difference between expected time and that cursor;
6. enters the seek block when either:
   - the absolute difference exceeds `SkipMargin`; or
   - `SkipInterval` is positive and the gameplay frame sum is exactly
     divisible by that interval.

The second condition deliberately seeks even when the difference is inside
the configured margin.

The shipped `data/system.cfg` values are:

```text
GameTimeOffset = 0
SkipMargin     = 48
SkipInterval   = 180
```

At the original 60 FPS, 180 frames is three seconds. The current framerate
hook scales the division interval to 720 frames at 240 FPS, preserving the
same three-second wall-time cadence.

The final seek block begins at EA `0x006401C4`
(RVA `0x002401C4`). It calls
`GC120FPS_CSoundManager_SetStageBgmGroupPositionMs` at `0x00611400`.
That function visits the two stage-BGM channels and reaches
`GC120FPS_DSoundChannel_SetPlayCursorMs` at `0x006150C0`, which converts
milliseconds to bytes and calls the secondary DirectSound buffer's
`SetCurrentPosition` vtable slot.

## Runtime correlation

The captured song produced 32 observed seek entries:

- first seek: 23:43:04.157;
- last seek: 23:44:37.181;
- minimum spacing: 2,999 ms;
- maximum spacing: 3,010 ms;
- average spacing: 3,000.77 ms;
- reason classification: 32 interval, 0 margin;
- observed absolute difference: 2 through 19 ms;
- configured margin: 48 ms.

The cadence and branch classification match the binary's periodic condition
exactly. This was not a margin-driven resync storm.

## WASAPI render-thread elimination

The four complete 30-second gameplay windows showed:

- zero render-work deadline overruns;
- zero render-event intervals above 150 percent of the fixed period;
- zero late event wakes;
- zero confirmed output gaps or skipped output frames;
- zero chronic pacing failures;
- zero endpoint HRESULT failures;
- maximum complete render work of 799 microseconds against a 10,000
  microsecond endpoint period.

The continuing in-song stutter therefore does not correlate with WASAPI
render starvation, endpoint failure, or an undersized fixed buffer.

## Current seek application

`SecondarySoundBuffer::SetCurrentPosition` converts the requested byte
position to a source frame, creates a new playback generation, and publishes
an unconditional mixer seek.

On the first mixer block that observes the new generation,
`VoiceNodeProcess`:

- resets the per-voice miniaudio converter;
- replaces the source cursor with the requested frame;
- resets the epoch source/output mapping;
- renders the rest of the block from the new origin.

The captured endpoint rejected 44.1 kHz exclusive mode and ran at 48 kHz, so
the stage-BGM voice used the 44.1-to-48 kHz converter. There is no crossfade,
slew, or no-op coalescing around this transition. Each periodic request can
therefore create a hard 2-to-19 ms source discontinuity followed by a
converter restart. This is the high-confidence cause of the reported
repeating/skipping audio stutter.

## Game-thread observations and remaining micro-freeze question

During steady gameplay, DirectSound cursor resolution called the WASAPI
endpoint clock approximately 480 times per second, or twice per presented
frame at 240 FPS. Complete endpoint-clock queries averaged approximately
17 microseconds in each captured gameplay window. The cumulative maximum was
1,012 microseconds, with 11 queries above one millisecond across the final
90 seconds.

The detailed render, cursor, and buffer timing instrumentation was added for
this investigation after the original user report, so it cannot explain that
report. The resync observation hook also performs synchronous session-log work
at the seek site. None of this diagnostic work is needed after the capture, and
retaining it would confound the correction experiment. It must be removed
rather than retained or redesigned.

The endpoint-clock query remains a real synchronous game-thread boundary and a
credible hitch risk, especially on a slower driver. The current aggregate
capture does not establish that it caused a visible frame-time spike. The
reported visual micro-freeze therefore remains unproven and must not be
attributed to the healthy WASAPI render thread.

Buffer snapshot publication copied 438,205,406 bytes before gameplay and
recorded a 3,924-microsecond maximum lock. No further buffer locks occurred
after gameplay began. That path can explain loading-time hitches but not the
continuing in-song micro-freezes.

## Correction boundary for design

The evidence supports separating two experiments:

1. prevent harmless interval-only BGM re-anchors from becoming hard WASAPI
   seeks while retaining genuine margin-driven correction;
2. replace synchronous gameplay-thread endpoint-clock reads with a published
   audio-thread clock, while removing the now-complete temporary diagnostics.

The game requests `DSBCAPS_GETCURRENTPOSITION2`, so falsifying the play cursor
by substituting a projected write cursor is outside the correction boundary.
Changing buffer duration is also unsupported by the capture.
