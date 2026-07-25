# 2D Menu Timing Coverage Matrix

- Status: static audit complete; runtime probes and gameplay acceptance pending
- Owner: root agent
- Baseline: see `README.md`

## Executive verdict

The current high-FPS implementation is **not complete for all 2D menu
animation**.

The ordinary Flash-like MovieClip path is much broader than the aggregate
runtime counters alone suggested: current-IDB tracing shows that normal root
clips, recursively nested clips, forward playback, and reverse playback all
converge on the hooked one-frame sink at `0x004DF940`. The hooked goto entry at
`0x004DEA30` also covers frame/label goto-play and goto-stop operations and
their synchronous frame actions. The separate Navigator hook reaches its sole
manual DDS/frame-cell advance sink.

Four material problems remain:

1. The universal MovieClip sink is also used by `Anim::PreProcessor`. Skipping
   it on a non-authored tick can make that caller observe no frame movement and
   call `Stop`, so the current callee-wide gate is context-unsafe.
2. Ranking entry choreography advances raw counters once per draw callback and
   is outside every current timing gate.
3. HitChart entry choreography has the same uncovered per-draw counter model.
4. UnlockReward mixes elapsed-seconds fields with two raw per-callback
   counters; the elapsed portion is correct, but the raw portion is uncovered.

No binary evidence supports restoring the old broad News/Notice task gates.
Those paths use elapsed-time waits and later ordinary MovieClip playback, so
leaving the task updates native is the correct static boundary.

## Corpus result

The XFL audit covered all 59 projects with `DOMDocument.xml`: all 57 top-level
RVB/MTX pairs plus `navi_001_yume_xfl` and `RECOVER_demo2`.

- 2,209 MovieClip definitions were inventoried.
- 1,306 definitions are multi-frame; 1,216 are statically reachable from a
  document timeline.
- 57 of 59 projects have a reachable multi-frame child and 55 have a
  depth-2-or-deeper child; maximum reachable depth is 5.
- The authored corpus contains 1,029 `gotoAnd*` calls, 1,753 `play()` calls,
  4,012 `stop()` calls, and 244 nested scripts that explicitly control another
  playhead.
- Ninety multi-frame definitions are not statically reachable. Several are
  named linkage-style clips, so runtime/native instantiation remains possible.
- Every generated XFL declares `frameRate="30"`, but that metadata is not
  evidence of the game's runtime update cadence.
- The raw `avatar`, `cutin`, `game`, `item`, `menu`, `message`, `news`, `se`,
  `skin`, and `title` directories contain texture/config assets rather than
  additional XFL timelines in this snapshot. Native consumers of those
  textures were still included in the binary callback sweep.

This structure rules out a main-document-only proof: most projects contain
independent nested playheads, deep child graphs, cross-playhead frame actions,
or dynamically linkable clips.

## Final coverage matrix

| Clock/update domain | Binary path and reach | Current handling | Verdict | Confidence |
|---|---|---|---|---|
| Outer authored phase | Central helper `0x00458B70` is called before render/present in both central loop-order branches | Publishes one deterministic shared 60 Hz phase; 240 FPS logs show the expected approximately 1:3 run/skip split | Mechanism executes as designed; exact per-object multiplicity remains a runtime concern | High static |
| Normal root and nested MovieClip playback | Recursive draw traversal, forward, and reverse playback converge on universal sink `0x004DF940`; no second recurring current-frame writer was found | `MovieClipAdvance` gates the sink with the shared authored phase | Covered for normal traversal, including nested clips | High |
| Frame/label goto and frame actions | Numeric/label goto-play and goto-stop route through `0x004DEA30`, then use the same advance sink | Thread-local goto depth bypasses throttling during the semantic goto | Covered for the traced goto APIs and synchronous actions | High |
| Bare `play()` / `stop()` | Interface slots `+0x90/+0x94` change playback state; subsequent recurring progression uses the ordinary sink | State change remains immediate; later automatic advance is gated | Correct static boundary | High |
| Direct/set/forward/reverse MovieClip alternatives | Raw setter `0x004D1540`; forward/reverse wrappers `0x004D1580/0x004D15A0`; recurring movement still reaches `0x004DF940` | Universal sink gate | Reached, subject to the PreProcessor collision below | High |
| Movie asset preprocessing | `0x004F0720 -> 0x004EFB90 -> forward/reverse -> 0x004DF940`; unchanged frame causes `Stop` at `0x004D1730` | Same callee-wide gate returns success without motion on skipped phases | **Incorrect context: can stop a clip during preprocessing** | High semantics; runtime frequency unknown |
| Navigator DDS/frame-cell animation | `0x005B6310` has one code caller; its registrar has ten menu/result task callers | Dedicated `NavigatorAdvance` gate | Covered at its sole native advance sink | High |
| Ranking entry choreography | Tag handler `0x00617070` resets entry counters; registered draw `0x00616C60` increments them once per callback | No current hook or elapsed-time conversion | **Uncovered raw draw clock** | High |
| HitChart entry choreography | Tag handler `0x00665B30` resets per-entry counters; registered draw `0x00665230` increments them once per callback | No current hook or elapsed-time conversion | **Uncovered raw draw clock** | High |
| UnlockReward choreography | IFBL `0x005F7D20 -> 0x00430C00`; elapsed fields coexist with raw countdown/state fields `+0x376C/+0x37D4` | Seconds-based fields are native and correct; raw invocation counters are untouched | **Partially covered mixed clock** | High path; medium visible symptom |
| News/Notice task updates | `0x00618A50`, `0x006544D0`, and `0x0060DB60` use setup/load states and elapsed waits; no separate playhead or raw frame-cell clock was found | Deliberately native; later MovieClips use the shared sink | Correct static boundary; wall-time acceptance pending | High static |
| IFBL elapsed-time callbacks | Callbacks consume the existing global elapsed-seconds value | Native callback cadence | Correct; broad callback throttling would break time semantics | High |
| IFBL integer waits | Universal store at `0x006309D4` preserves cooperative values 0/1 and scales larger authored waits | `IfblWait` transform | Covered at the classified store | High |
| Other registered native 2D draw callbacks | Exhaustive central-registrar sweep: 19 registrations and 21 possible callbacks | Native unless classified above | No additional in-scope raw recurring draw clock found | High within sweep |
| Other recurrent opcode-1 IFBL callbacks | 218 descriptors / 197 unique callbacks swept | Native unless classified above | No additional in-scope raw recurring clock found | High within direct-descriptor inventory |
| Dynamically linked XFL clips | Ninety multi-frame definitions are not statically rooted; native/export instantiation was not observed in this static asset pass | If instantiated as ordinary MovieClips, progression reaches the shared sink | Scheduler coverage likely, content reach unverified | Medium |

## Ranked findings

### 1. High correctness defect: PreProcessor context collision

`0x004DF940` is a universal MovieClip primitive, not an exclusively
render-scheduled clock. `Anim::PreProcessor` records the current frame, invokes
forward or reverse movement, compares before/after, and stops the clip if the
frame did not change. On a non-authored phase the current hook returns `1`
without moving the frame. The caller ignores that return, observes equality,
and calls `Stop`.

The binary semantics are conclusive. What remains unknown is how often menu
assets traverse this preprocessing path after the hooks are installed and
whether it explains any currently missing or prematurely stopped subanimation.
A correct patch must distinguish scheduler traversal from preprocessing rather
than gate the universal callee blindly.

### 2. High coverage gaps: Ranking and HitChart

Ranking and HitChart are XFL-coupled menu/result choreography, but their native
entry effects are not MovieClip advances. Each tag handler clears raw counters
and each registered draw callback increments them once. At higher callback
rates their wall-time durations shrink inversely with FPS. Neither the
MovieClip, Navigator, IFBL-wait, nor input-repeat patch reaches these counters.

### 3. Medium visible-risk gap: UnlockReward

UnlockReward's update combines valid elapsed-seconds animation with a raw
countdown and a raw state sequence. Throttling the whole callback would damage
the elapsed portion, while leaving it entirely native makes the raw state
portion FPS-dependent. The exact visible boundary associated with each raw
counter must be observed before choosing a field-level correction.

## Exhaustive-search boundary

The binary audit revalidated every current hook byte against the fixed IDB,
traced the complete MovieClip interface and normal traversal, swept every
callback registered through the central 2D draw registrar, and classified all
direct recurrent opcode-1 IFBL callback descriptors. Within those explicit
boundaries, Navigator, Ranking, and HitChart are the only menu-side raw
recurrent draw clocks found; UnlockReward is the only additional mixed
elapsed/raw IFBL clock found.

A separate raw 30-invocation counter in `0x005E3EC0` was traced to sole callsite
`0x0064A269` inside the gameplay renderer and is outside this menu audit. Its
classification prevents it from being mistaken for an unresolved menu clock.

## Proof and test gaps

Current automated tests validate phase arithmetic, hook manifests and bytes,
transaction rollback, and non-null callback binding. They do not execute the
hook callbacks or assert:

- root/nested per-object advance counts;
- goto/play/stop and cross-playhead behavior;
- PreProcessor before/after semantics;
- Ranking, HitChart, or UnlockReward wall-time;
- callback multiplicity within one outer epoch;
- 60/120/144/240 displayed-frame sequences.

The deployed 240 FPS counters prove that MovieClip and Navigator hooks execute
at an aggregate approximately 1:3 ratio. They cannot identify asset, object,
nesting depth, callsite, label, duplicate visits, or preprocessing calls, so
they are execution evidence rather than completeness evidence.

## Required runtime probes before patch design

1. Trace `0x004F0720`, `0x004EFB90`, `0x004DF940`, and `0x004D1730` with
   asset/object, call stack, outer epoch, authored phase, goto depth, and frame
   before/after. Confirm whether a skipped preprocessing advance triggers
   `Stop` after installation.
2. Key ordinary MovieClip calls by outer epoch and object at 60/120/144/240
   FPS. Verify that each live root and nested playhead advances exactly once on
   authored ticks and that gotos remain immediate.
3. Measure reset-to-settle wall time for Ranking (`0x00617070/0x00616C60`) and
   HitChart (`0x00665B30/0x00665230`) at 60/120/240 FPS.
4. Log UnlockReward fields `+0x376C`, `+0x3770`, `+0x37D0`, and `+0x37D4`
   alongside active label/state and visible draw activity.
5. Retain Navigator and News/Notice as controls so a later fix does not
   regress already-correct clock domains.

## Audit disposition

This review changed documentation only. It did not modify GCLoader production
code, tests, `game471.exe`, the deployed DLL, or the IDB. The shared IDA daemon
remains available for the runtime-instrumentation and patch-design follow-up.
