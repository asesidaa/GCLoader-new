> **RETAINED DIAGNOSTIC HISTORY — NOT A CURRENT DESIGN.** The associated
> instrumentation was removed; use the completed evidence audit instead.

# High-FPS Gameplay Input Edge Diagnostics Design

Date: 2026-08-09

## Context

At 240 FPS, keyboard-driven gameplay entries are intermittently missed. The
symptom is independent of the selected audio backend and sample bit depth. It
also does not depend on whether the chart object is a short note or a long
note: the initial hit can be missed for either. Once the initial hit of a long
note is accepted, its held portion remains stable and does not drop.

That distinction points specifically at initial pressed-edge delivery. A
short note exposes the problem more often because it has no sustained phase;
a long note switches to a persistent held-state query after its entry has
been accepted.

The current loader logs establish that the native input worker starts, Raw
Input registration remains active, and the game remains foreground. The
source establishes that the worker publishes a 32-bit FastIO level snapshot,
but the available Info-level log does not show each transition or which level
the game observed on the frame where gameplay judgment queried it. Existing
per-transition `PLOG_DEBUG` messages are also unsuitable for a rhythm-game
session: enabling them produces unbounded synchronous log traffic without
correlating the game's internal frame-indexed input state.

This design adds a temporary, read-only diagnostic build that follows one
gameplay press through the loader and the executable. It does not authorize an
input fix. The resulting trace decides which boundary a later fix may own.

## Binary and Source Evidence

The source-facing stages are:

1. `InputPollingRuntime` receives a Raw Input transition, updates
   `InputMapper`, and atomically publishes the composed FastIO level word.
2. `iDmacDrvRegisterRead(FIO_NODE_0_INPUT)` atomically loads and returns that
   word. It does not latch or queue transitions.

The executable-facing stages were verified against
`H:\gc\game471.exe.i64`, image base `0x00400000`, through the existing
daemon-backed IDA session:

| Stage | VA | RVA | Verified behavior |
|---|---:|---:|---|
| GW XIO aggregate update | `0x00456360` | `0x00056360` | Polls input devices and rebuilds current/edge fields on every native update. |
| GW edge construction | `0x00455C80` | `0x00055C80` | Derives pressed and released masks from current versus previous held masks, then updates repeat state. |
| `CInputDevice` frame update | `0x0062CFB0` | `0x0022CFB0` | Samples the ten logical gameplay inputs into a frame-indexed history ring. Repeated writes to the same frame OR state; a newer frame replaces its slot. |
| `CInputDevice` pressed query | `0x0062DFB0` | `0x0022DFB0` | For logical inputs `0..9`, returns true when the requested bit is present at frame N and absent at frame N-1. |
| `CInputDevice` held query | `0x0062DF50` | `0x0022DF50` | Returns the requested logical bit from frame N without edge comparison. |
| Gameplay held wrapper | `0x00659570` | `0x00259570` | Resolves an input device and calls its held-state virtual method. |
| Gameplay pressed wrapper | `0x00659640` | `0x00259640` | Resolves an input device and calls its pressed-edge virtual method. |

The gameplay pressed wrapper is called by the normal button, direction,
sustained-note entry, and special-note paths in the `0x005Dxxxx` region. The
held wrapper is used after sustained-note entry and for continuation checks.
This matches the runtime observation: entry can disappear while an already
accepted hold remains stable.

The logical-to-XIO masks at `0x00783040` are also verified for inputs `0..9`:

| Logical input | Meaning | XIO mask |
|---:|---|---:|
| 0 | Left booster Up | `0x00000040` |
| 1 | Left booster Down | `0x00000001` |
| 2 | Left booster Left | `0x00000004` |
| 3 | Left booster Right | `0x00000010` |
| 4 | Left booster button | `0x00000020` |
| 5 | Right booster Up | `0x00000100` |
| 6 | Right booster Down | `0x00000200` |
| 7 | Right booster Left | `0x00000400` |
| 8 | Right booster Right | `0x00000800` |
| 9 | Right booster button | `0x00100000` |

The loader already has the corresponding logical-to-FastIO mapping in
`InputSnapshotState`. Diagnostics use these two explicit tables to correlate
representations; they do not infer mapping from field names.

## Diagnostic Question

For each aggregate FastIO off-to-on transition representing logical gameplay
input `0..9`, which is the last boundary that demonstrably observed it?

The possible outcomes are:

1. The input worker never publishes the rise.
2. The rise is published but no game register read observes it.
3. iDmac exposes the rise but GW XIO does not construct the corresponding
   pressed edge.
4. GW XIO constructs the edge but `CInputDevice` does not store the logical
   transition in its frame history.
5. The history contains the transition but the native gameplay pressed query
   never returns true for it.
6. A native or Switch-aliased pressed query returns true. In this case the
   loader/input pipeline is exonerated and the next investigation must trace
   the note-specific judgment branch. This diagnostic must not infer note
   acceptance merely from a query result or from audible key sound.

## Goals

- Correlate each gameplay input rise across publication, iDmac observation,
  GW edge construction, frame-history storage, and gameplay queries.
- Record both native query results and the final result returned after Switch
  direction-to-button aliasing.
- Record gameplay frame, call site, outer-frame epoch, and authored-60-Hz
  phase so a 240-FPS phase-dependent loss is visible.
- Distinguish short/long chart-note type from physical press duration.
- Bound all storage and log volume for a full chart run.
- Keep input, iDmac, executable-hook, and query hot paths free of allocation,
  locks, formatting, and logging.
- Preserve every native return value, input word, frame-history value, and
  Switch semantic decision.
- Compare one controlled 60-FPS baseline with a 240-FPS reproduction.

## Non-Goals

- Latching, stretching, queueing, replaying, or consuming an input edge.
- Gating input or judgment to authored 60-Hz frames.
- Changing Raw Input ownership, worker cadence, thread priority, or mappings.
- Changing Arcade/Switch gameplay semantics.
- Changing note windows, note matching, key-sound playback, or audio code.
- Adding permanent user-facing configuration or ConfigGUI controls.
- Treating successful static/build checks as runtime acceptance.
- Selecting a production fix before the diagnostic trace identifies the
  failing boundary.

## Approaches Considered

### Source-only counters

Counting published rises and gameplay-query successes would be simple, but it
would collapse iDmac, XIO edge construction, and `CInputDevice` history into
one unknown region. It cannot answer the diagnostic question and is rejected.

### Debugger breakpoints

Read-only breakpoints can inspect `0x00455C80` and individual judgment paths
without changing the DLL. They are useful for short, controlled interactions
but generate too many debugger stops/events during ordinary chart play and do
not observe the loader worker boundary cleanly. They remain a fallback for a
single note-specific branch after the correlated trace narrows the scope.

### Correlated source and executable trace

The selected approach combines two source observations with two read-only
executable hooks and the already-owned gameplay-query hooks. It produces the
complete boundary chain while keeping every hot-path operation bounded.

## Activation and Lifetime

Diagnostics are controlled by a CMake option named
`GC_ENABLE_INPUT_EDGE_DIAGNOSTICS`, defaulting to `OFF`. No TOML field or GUI
control is added. The candidate used for the 60/240 comparison is built with
the option enabled and logs one startup line containing:

- diagnostic build state;
- executable image base;
- target FPS;
- gameplay input style;
- all diagnostic hook preflight/install states; and
- the fixed record capacities.

A normal build compiles out executable diagnostic-hook installation and
periodic diagnostic output. The diagnostic option is temporary evidence
infrastructure, not a new production feature. After the root cause and fix
are accepted, the option and diagnostic hooks must either be removed or
receive separate approval to remain.

## Diagnostic Architecture

### `InputEdgeDiagnostics`

Add a focused input diagnostics unit that owns:

- the two explicit logical mapping tables;
- per-action press generations for logical inputs `0..9`;
- cumulative and interval counters;
- fixed-capacity stage/event records;
- current diagnostic hook state;
- frame/phase stamps supplied by the framerate runtime; and
- snapshot/finalization logic used by the periodic logger.

The module exposes narrow `noexcept` recording functions. Disabled builds use
inline no-op implementations so normal paths do not acquire a new runtime
dependency.

The module does not own a worker thread. The existing framerate statistics
cadence requests one immutable diagnostic snapshot every five seconds and
emits the compact summary outside the input and query hooks. No diagnostic
hook calls the logger.

### Per-action press generation

An aggregate FastIO off-to-on transition starts a new generation for its
logical action. Because the published word already ORs all physical sources,
there can be only one active aggregate generation per action until that bit
falls.

Each generation records, when observed:

- sequence number and logical action;
- publication and release QPC timestamps;
- published FastIO word;
- first iDmac observation timestamp and word;
- first matching XIO pressed-edge timestamp and mask;
- first matching `CInputDevice` history transition, object identity, and
  gameplay frame;
- first native pressed-query success, input-device ID, gameplay frame, and
  note-judgment caller RVA;
- final Switch-aware query result and accepted alias, if any; and
- the outer epoch/authored phase at every game-side stage.

Fields shared between the input-worker producer and game-thread producer use
atomics or a sequence-stamped fixed record. There is no mutex and no dynamic
allocation.

A generation completes immediately when a native pressed query succeeds. If
it has not completed when the aggregate bit falls, it remains eligible for
game-side observation for 50 ms, then finalizes at the last confirmed stage.
The 50-ms value is diagnostic classification time only; it never changes or
delays game input.

### Bounded records

The implementation uses fixed capacities:

- 10 live per-action generations;
- 256 publication/iDmac transition records;
- 256 nonzero XIO edge records;
- 512 `CInputDevice` frame records; and
- 512 consolidated gameplay-query frame records.

Overwriting an old completed record increments an overwrite counter. It never
blocks a producer. Incomplete generation samples are logged at most four per
five-second interval and at most 32 per process; additional incomplete samples
increment a suppressed counter reported in the summary.

## Observation Points

### Publication

`InputPollingRuntime::Publish` calls the diagnostic recorder only when the
FastIO word changes. The recorder derives rising and falling logical gameplay
bits from `previous ^ next`, starts/finalizes generations, and records QPC.

The existing unbounded per-transition and per-snapshot debug messages are not
used for this test. The diagnostic records replace their evidentiary role.

### iDmac register read

`iDmacDrvRegisterRead(FIO_NODE_0_INPUT)` records the returned word after the
atomic load. Every read increments a counter; QPC and transition records are
written only when the observed word changes. A high bit marks the current
matching publication generation as observed by the game.

The function still returns the original word without waiting or clearing any
state.

### GW XIO edge construction

Install a checked inline hook at RVA `0x00055C80`. The detour calls the
original function exactly once, preserves its return value, and then inspects
the caller and the held/pressed/released/repeat output masks.

Only slot 2, digital group 0 participates in the gameplay accessors used by
this title. The hook distinguishes the three verified compute call sites,
learns the aggregate base from the first slot-0 aggregate invocation, and
validates the output pointers against the verified 1,100-byte slot stride and
group-0 field offsets. The relevant group call instruction is `0x00456582`;
its slot-2 pressed field is the value returned by
`GWInputXio_GetPressedEdgeBits(0, 2)`. Other slots/groups increment aggregate
counters but cannot complete a logical stage. A matching bit in that exact
pressed mask marks the active logical generation as having reached GW XIO.

The hook performs no logging and records QPC only for a nonzero pressed or
released mask.

### `CInputDevice` frame history

Install a checked inline hook at RVA `0x0022CFB0`. The detour calls the
original update exactly once and preserves its return value. After the update,
it reads logical inputs `0..9` through the object's original held-state virtual
method for the supplied frame and composes one ten-bit diagnostic mask.

Comparing this mask with the previous frame mask for the same object identifies
the transition actually present in the history queried by gameplay. This
avoids depending on private vector offsets or duplicating the game's ring
index arithmetic.

The record includes repeated/successive frame counts, object identity, and
phase stamp. This reveals same-frame accumulation, frame skips, frame repeats,
and a possible update/query frame-domain mismatch.

### Gameplay pressed and held queries

RVA `0x00259640` and `0x00259570` are already owned by
`SwitchInputPatch`. Diagnostics must not install a second hook at either
address.

Instead, the outer detour captures its note-judgment caller RVA once and puts
it in `OriginalQueryContext`. Then:

- every call through `query_original` records the native logical input,
  gameplay frame, result, and captured note-judgment caller RVA;
- the outer pressed/held detour records the final value returned to gameplay;
  and
- Switch mode records the direction alias that satisfied a requested booster
  button.

When diagnostics are enabled in Arcade mode, the same existing hook layer may
be installed solely to observe and return the unchanged native value. With
diagnostics disabled, Arcade mode retains its current no-hook behavior.

Queries are consolidated per gameplay frame into requested and successful
bitmasks. No individual query is logged from the hook.

### Framerate phase stamp

Expose a read-only diagnostic accessor from the framerate runtime containing:

- target FPS;
- outer-frame epoch;
- current authored-60-Hz decision; and
- deterministic authored phase state.

The accessor returns atomically published values and cannot advance or modify
the clock. At 240 FPS, interval summaries include per-phase counts for
publication-correlated history edges and successful pressed queries. At the
60-FPS baseline, every native frame is the authored opportunity.

## Hook Safety and Failure Policy

Each new executable hook has an exact RVA, expected-byte prefix, and focused
contract test. Diagnostic preflight completes before either new hook is
installed.

The entry prefixes are:

| Site | Expected prefix |
|---|---|
| `0x00455C80` | `8B 4C 24 04 8B 01 8B 54 24 14 56 8B 32 33 F0 F7 D0 23 F0` |
| `0x0062CFB0` | `55 8B EC 83 EC 78 89 4D A0 8B 4D A0` |

The three edge-compute call-site prefixes used for classification are also
preflighted:

| Call instruction | Expected bytes through stack cleanup/next instruction |
|---|---|
| `0x00456550` | `E8 2B F7 FF FF 83 C4 18` |
| `0x00456582` | `E8 F9 F6 FF FF 83 C4 18` |
| `0x004565BF` | `E8 BC F6 FF FF` |

If preflight or installation fails:

- remove only diagnostic-owned hooks already created;
- disable executable-stage diagnostics;
- log one error naming the stage/RVA;
- leave the input worker, iDmac return path, framerate patch, and existing
  Switch behavior unchanged; and
- mark summaries `incomplete=true` so source-only counts cannot be mistaken
  for a complete trace.

Diagnostic failure must not force Switch mode to Arcade. Conversely, an
existing Switch hook failure retains its current all-or-nothing fallback and
is reported distinctly from diagnostic-hook state.

All memory reads in detours are either original function arguments, verified
output pointers after the original call, or original virtual calls on the
supplied object. The GW pointer arithmetic is used only to classify verified
slot/group output arguments; no undocumented ring-buffer offset is read
directly.

## Log Contract

Startup emits one `InputEdgeDiag: active` line.

Every five seconds, one aggregate line reports:

- interval and cumulative publication rises by logical input;
- iDmac-observed rises;
- XIO pressed edges;
- frame-history transitions;
- native pressed-query successes;
- final Switch-aware successes;
- held-query true counts;
- pending/incomplete/overwritten/suppressed generations;
- maximum publication-to-iDmac and frame-update gaps;
- repeated/skipped history and query frames; and
- 240-FPS authored-phase histograms.

Up to four bounded incomplete-generation lines may follow. Each contains only
the generation/action, last confirmed stage, stage-to-stage microsecond
latencies, relevant masks, history/query frames, call-site RVA, and phase
stamps. Successful presses are represented by counters and are not logged
individually.

The logger never prints physical key labels, controller paths, or every
register/query call. The output must remain useful after a complete song
without becoming an input-event transcript.

## Automated Verification

### Pure diagnostic state tests

Feed synthetic stage events and verify:

- FastIO rising/falling bits create and finalize the correct logical
  generations;
- independent left/right and simultaneous inputs remain separate;
- publication-only, iDmac-only, XIO-only, history-only, and query-success
  sequences classify at the correct last stage;
- native and Switch-aliased successes are distinguishable;
- a held state without a new transition does not create another generation;
- release plus the 50-ms diagnostic grace finalizes without affecting input;
- per-phase histograms and gap maxima are correct;
- fixed buffers overwrite without blocking and report exact counts; and
- anomaly output obeys the four-per-interval and 32-per-process caps.

### Hook and policy tests

- Lock the two new RVAs and expected-byte prefixes.
- Verify diagnostic hook-state resolution for full success and each partial
  failure.
- Extend Switch policy tests so diagnostic callbacks cannot change native,
  alias, Arcade, or held return values.
- Verify query consolidation across repeated calls in one gameplay frame.
- Verify enabled Arcade diagnostics return native results unchanged.
- Verify disabled builds retain the current Arcade no-hook plan.

### Build verification

- Build focused input, Switch, framerate, and configuration tests with the
  option both `OFF` and `ON` under the x86 MSVC preset.
- Build `iDmacDrv32` in the diagnostic configuration.
- Run the focused tests and then the complete CTest suite.
- Confirm a normal distribution build has diagnostics disabled and adds no
  diagnostic runtime artifacts.

Automated tests prove state classification, hook contracts, unchanged policy
returns, and build integration. They do not prove where the live game loses an
edge.

## Manual Diagnostic Matrix

The user performs runtime testing; agent reports must separate build/static
verification from observed gameplay behavior.

Use the same input mapping, gameplay input style, chart segment, and audio
backend for both runs so FPS is the deliberate variable:

1. Build and deploy the diagnostic DLL while the game is stopped.
2. Capture a fresh 60-FPS baseline log.
3. Play a segment containing ordinary short notes and at least one long note.
4. Capture a fresh 240-FPS log with the same actions and chart segment.
5. Note approximately where an entry visibly/audibly misses; no exact manual
   timestamp is required.
6. Compare per-action stage counts, incomplete generations, frame gaps, and
   phase histograms between the two runs.

The first diagnostic verdict is the earliest stage whose count/records diverge
from the preceding stage at 240 FPS but not at 60 FPS. If native pressed queries
succeed for every published rise, this diagnostic phase closes without an
input-pipeline fix and the next design traces note-specific judgment outcomes.

## Evidence and Cleanup Boundary

The first diagnostic implementation may add only the observations, tests,
build option, and log contract described here. It must not combine a candidate
edge latch or gameplay fix with evidence collection.

After the user's two logs are analyzed:

- record the confirmed failing boundary;
- remove superseded/noisy diagnostic probes;
- write a separate binary-backed correction design scoped to that boundary;
  and
- obtain runtime acceptance before calling the correction complete.

Source, tests, design documents, and commits belong in
`H:\gc\artifacts\GCLoader`. `H:\gc` is the runtime/deployment tree and is not
modified by this design/specification step.
