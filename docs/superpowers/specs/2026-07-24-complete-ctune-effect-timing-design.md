# Complete CTuneEffect High-FPS Timing Design

Date: 2026-07-24

Status: design approved in conversation; implementation remains pending written
spec review.

## Authority and relationship to the existing framerate design

This document refines and supersedes the gameplay `CTuneEffect` portions of
`2026-07-19-complete-high-fps-timing-fix-design.md`. The earlier design remains
authoritative for non-effect timing domains, configuration, external-cap
validation, startup transactionality, and the existing failure policy.

The runtime/deploy tree is `H:\gc`. Source, tests, specifications, and commits
belong in `H:\gc\artifacts\GCLoader`.

Binary facts in this design come from the daemon-backed IDA database
`H:\gc\game471.exe.i64`, whose input is `H:\gc\game471.exe` with:

```text
image base: 0x00400000
SHA-256: FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
```

The live asset source used by the audit is:

```text
H:\gc\data\effect\game
```

The asset directory is evidence, not a source directory to copy into or modify
from GCLoader.

## Context

The current high-FPS patch corrects several gameplay-effect paths, but runtime
testing at 240 FPS still shows incorrectly accelerated effects. Tutorial
effects backed by `img_big13.bin` are a visible example. Hit effects and
possibly other chart/gameplay effects are also affected, while some effect
paths already appear correct.

This mixed result is expected from the executable's architecture:

- `sub_5F08A0` advances ordinary managed effects through `sub_5F1740`.
- The manager does not advance instances with flag `0x2000` or `0x4000`.
- Many gameplay effects are preallocated with `0x4000`, framed directly by
  their producer, rendered once, and removed by `sub_5F1180`.
- Direct producers do not share one clock domain. Some write target-frame
  elapsed values, some divide milliseconds by a frame duration, some write a
  normalized fraction of an authored definition length, and some write zero.
- `sub_5F1F70` consumes `effect + 0x08`, applies the effect speed multiplier,
  and derives nested-child frames from the parent's already-authored frame.

Consequently, neither the existing manager gate nor a new renderer-wide frame
mapping can correct every effect. A renderer-wide mapping would double-map
valid normalized and child-derived frames while still missing target-domain
lifetime comparisons performed before rendering.

## Goals

- Correct every gameplay `CTuneEffect` timing path, not only tutorial effects.
- Cover managed effects and externally framed effects, including hit,
  judgement, tutorial, countdown, chart/gimmick, player-position, remote, and
  nested-child effects.
- Preserve effect paths that are already authored-60 and correct.
- Make every effect-frame producer's clock domain explicit and auditable.
- Use `data\effect\game` as a second, asset-side completeness source.
- Keep authored definition lengths and keyframes in their original 60 FPS
  domain.
- Keep target-frame comparisons in the target-FPS domain by scaling authored
  durations where required.
- Preserve native 60 FPS behavior through the existing transformed-plan
  bypass.
- Preserve exact expected-byte checks, transactional installation, and
  fail-fast runtime conversion behavior.
- Provide exhaustive static proof before asking for gameplay acceptance.
- Keep build/static completion distinct from user-confirmed in-game success.

## Non-goals

- Modifying, repacking, or replacing files under `data\effect\game`.
- Hardcoding effect lengths from the current asset package into GCLoader.
- Converting MovieClip, navigator, particle, stage-geometry, or other
  non-`CTuneEffect` systems as part of this change.
- Excluding a producer merely because it is reached from a stage renderer. If
  that producer creates a `CTuneEffect` backed by the game effect package, its
  frame boundary is in scope.
- Interpolating new artwork between discrete authored frames.
- Hooking `sub_5F1F70` globally.
- Maintaining a side table keyed by effect pointers.
- Rewriting the complete CTune effect engine into a target-frame engine.
- Treating a passing build or static audit as gameplay acceptance.

## Alternatives considered

### Map every effect at renderer entry

This would place one hook near the final consumer, but the input domain is not
uniform. Normalized-progress paths already calculate:

```text
definition_length * normalized_progress
```

and nested effects derive their frame from the parent's authored frame. Both
are already authored values. Mapping either again would slow the animation.
The renderer hook would also be too late for lifetime comparisons that use the
same frame before the renderer is called.

This approach is rejected.

### Tag externally framed effect instances

Each producer could tag an effect pointer with its source domain, allowing a
selective renderer mapping. The tags would require pointer-lifetime tracking,
reuse protection, cleanup, and special handling for nested effects. Producers
would still need separate hooks for lifetime comparisons, so the tag system
would not eliminate producer analysis.

This approach is rejected.

### Rewrite CTuneEffect around target frames

The manager, externally framed paths, duration checks, loops, speed
multipliers, and nested effects could all move to target-frame units, with an
authored conversion only at asset sampling. This is conceptually uniform but
is an invasive engine rewrite with a much larger regression surface.

This approach is rejected.

### Normalize proven producer boundaries

The selected architecture keeps each existing clock owner intact and converts
only where a value crosses into a consumer with a different domain. Every
producer is recorded, including producers that require no hook. This preserves
already-correct effects and yields a finite static proof obligation.

## Timing-domain contract

The asset package is authored at 60 FPS. The patch enforces the following
contract:

| Producer value | Consumer expectation | Treatment |
|---|---|---|
| Managed `+1` advance | Authored frame | Run the manager advance only on authored-60 boundaries |
| Target-FPS elapsed frame | Authored frame | `floor(frame * 60 / target_fps)` |
| Authored duration | Target-frame comparison | `round_half_up(duration * target_fps / 60)` |
| Elapsed milliseconds | Authored frame or lifetime | Divide by `1000 / 60` at the final effect operand |
| Normalized progress times definition length | Authored frame | Preserve unchanged |
| Child frame derived from authored parent | Authored frame | Preserve unchanged |
| Zero/reset or negative sentinel | Authored/sentinel value | Preserve unchanged |

The central invariant is:

```text
effect + 0x08 is an authored-60 frame whenever sub_5F1F70 consumes it
```

When a target-frame clock is the consumer instead, the authored duration is
scaled into target frames before comparison.

The existing helpers remain the arithmetic authority:

```text
MapPositiveTargetFrameToAuthored60
ScalePositiveDuration
```

Both preserve signed nonpositive values. All checked arithmetic and failure
semantics remain unchanged.

## Selected architecture

### Code-side producer manifest

Add one authoritative CTune effect timing manifest. The current binary audit
starts from all 34 code references to `sub_5F07A0` and then adds direct frame
writers that do not immediately follow registration.

The census must include:

- every `sub_5F07A0` registration call and its reaching
  `effect + 0x08` writer;
- every other direct `effect + 0x08` writer;
- all nine calls to authored-duration query `sub_5F0450`, which occur in
  exactly three owner functions in the current binary;
- the manager advance in `sub_5F1740`;
- reset helpers;
- renderer and nested-child writes in `sub_5F1F70`;
- CTune flow-item rendering through `sub_5F0220`.

Each manifest row records:

```text
stable site id
owner function and purpose
VA and RVA
expected instruction bytes when hookable
effect slot or proven dynamic slot range
asset group/definition set when known
source clock domain
consumer clock domain
disposition
transform, if any
runtime counter id, if hooked
IDA evidence note
```

Allowed dispositions are:

```text
hook
manager_gated
already_authored_normalized
reset_or_constant
child_inherited
non_ctune_out_of_scope
```

There is deliberately no `unknown` completion state. The static audit and
tests fail until every row has a final disposition.

Hook rows supply the effect subset of `FramerateHookContract`. Evidence-only
rows remain in the same manifest so an already-correct path cannot disappear
from the audit merely because it installs no code.

### Asset-side catalog

Add a deterministic, read-only offline catalog utility under
`tools/analysis/`. It accepts an explicit game-effect directory and never
writes to that directory.

The utility catalogs:

- SHA-256 and byte size of `efcdata.dat`, `effect.dat`, and `uvdata.dat`;
- each container's big-endian declared size, entry count, and bounded offset
  table;
- every proven effect definition and authored length;
- referenced texture/UV slots;
- all `imgN`, `img_bigN`, and language-variant images;
- PNG signature, dimensions, and SHA-256 for each image payload.

The current directory contains 34 `.bin` image files, all with PNG signatures.
The three data-container headers currently report top-level counts of 89, 43,
and 16 respectively. These values are catalog canaries, not runtime constants.

The parser labels only fields proven by the executable's loader or corroborated
structure. Unknown fields remain raw; they are not assigned speculative names.

The producer manifest cross-references the catalog by group/definition or by a
proven dynamic range. This answers both:

1. Which code path supplies an effect's frame?
2. Which authored definitions and textures can that path display?

The tutorial family is a known cross-check:

- group-0 definitions 61 through 69 use texture slot 13;
- their authored lengths range from 13 through 38 frames;
- gameplay effect slots `0xB2` through `0xC0` select those definitions;
- `img13`/`img_big13` and language variants provide the corresponding texture
  payloads.

The catalog is verification-only. GCLoader does not parse it at startup and
does not refuse customized assets.

### Semantic hook families

Effect hooks are grouped by the domain crossing they implement rather than by
an incidental effect name.

#### Managed advance and authored cadence

Retain:

- the gameplay effect manager gate at VA `0x00664E2D`,
  RVA `0x00264E2D`;
- period-4/5/6/8/16 authored-cadence conversions;
- remote authored-cadence conversions where they feed CTune visuals.

The manager gate remains necessary but is not considered complete coverage,
because `0x4000` effects bypass it.

#### Milliseconds to authored frames

Retain the existing final-operand redirects for GREAT/GOOD, direct gameplay
effects, chart effects, and fixed visuals. Each redirect substitutes the
process-lifetime `1000.0F / 60.0F` operand only at the authored effect sink.
The global runtime frame duration remains `1000 / target_fps`.

#### Target frames to authored frames

Use `MapPositiveTargetFrameToAuthored60` at the earliest shared producer value
that feeds all related authored consumers.

The initial static pass confirms two missing sites in this family:

| Site | Original bytes | Incoming value | Required action |
|---|---|---|---|
| VA `0x005F0310`, RVA `0x001F0310` | `89 42 08` | EAX is CTune flow-item duration divided by runtime frame milliseconds | Map EAX before the original `effect + 0x08` store |
| VA `0x00649593`, RVA `0x00249593` | `89 95 74 FF FF FF` | EDX is tutorial/chart elapsed target frames | Map EDX before the shared local store |

The flow-item site belongs to `sub_5F0220`. It calculates:

```text
int((flow_end_ms - flow_start_ms) / runtime_frame_ms)
```

and writes that target-frame count directly to `effect + 0x08`. Mapping at the
store keeps the flow simulation in its existing target/millisecond domain and
changes only the authored asset index. This path is a likely source of the
remaining flow/hit-effect acceleration.

The tutorial site stores EDX in a shared local. The same local is:

- compared with authored definition lengths at VA `0x00649631`,
  `0x00649658`, and `0x00649795`;
- written to one effect at VA `0x006498F9`;
- written to its paired effect at VA `0x006499AD`.

Mapping the shared value at `0x00649593` corrects both lifetime selection and
both frame stores. Hooking only the two final stores would leave the authored
length comparisons in the wrong domain.

#### Authored durations to target-frame comparisons

Use `ScalePositiveDuration` when an authored definition length is compared
with a target-frame distance.

The initial static pass confirms:

| Site | Original bytes | Incoming value | Required action |
|---|---|---|---|
| VA `0x0064A934`, RVA `0x0024A934` | `89 45 9C` | EAX is an authored duration returned by `sub_5F0450` | Scale EAX before the original local store |

The field supplying the other side of the comparison is derived at
VA `0x005EB82F` by dividing milliseconds by the runtime profile's
`1000 / target_fps`. Therefore the distance is in target frames and the
authored duration must be scaled upward before the comparison.

#### Explicit no-hook paths

The manifest records, but does not patch:

- frame-zero registrations;
- target-cue functions that write
  `definition_length * normalized_progress`;
- chart-render paths with the same normalized authored calculation;
- nested-child frames derived from the parent frame through `sub_5F17A0`;
- native authored manager increments already covered by the manager gate.

These rows are essential regression protection against double mapping.

The three confirmed additions above are not assumed to be the final hook
count. Implementation cannot close until the complete manifest proves that
all remaining rows fall into a final disposition and any additional domain
crossing receives the matching semantic transform.

### Source organization

Add a focused `FramerateEffectTiming` module containing:

- the producer manifest types and entries;
- the effect-only hook-contract view;
- pure context/register transforms for effect frame and duration crossings;
- stable per-site identifiers used by tests and diagnostics.

`FrameratePatchPlan` merges the effect hook-contract view with the existing
non-effect contracts. `FrameratePatch.cpp` retains thin SafetyHook callbacks,
hook storage, runtime state, and the existing transaction integration. This
avoids exposing the complete private runtime state merely to move callbacks
between files.

Existing generic arithmetic stays in `FramerateAuthoredClock`; it is not
duplicated in the effect module.

## Safety and compatibility

### Transactional installation

Every installed effect hook has an exact expected-byte contract and is part of
the existing all-or-nothing framerate transaction. A byte mismatch, hook
creation failure, or capacity error aborts transformed framerate installation.
The loader never continues with a partial mix of old and new effect timing.

Native 60 FPS continues to omit the transformed effect-hook set.

### Runtime conversion failures

Effect callbacks use the existing checked helpers. A memory read or arithmetic
failure follows the existing one-shot modal/log publication, process
termination, and fail-fast path. There is no silent clamping or guessed
fallback.

### Binary and asset drift

The static producer report is pinned to the analyzed executable SHA-256.
Hooked rows are additionally guarded by their original bytes.

The asset catalog is not a runtime gate. Replacement images do not alter the
clock-domain rule, and effect lengths continue to come from the game's live
definition objects. If the executable or data containers change, the catalog
and producer audit must be regenerated before claiming exhaustive static
coverage.

### Excluded central state

The design adds no:

- effect-pointer registry;
- renderer detour;
- asset mutation;
- global frame-duration replacement;
- effect-definition length hardcoding.

## Diagnostics

Startup diagnostics add:

```text
effect_timing=producer_boundary
effect_manifest_rows=<count>
effect_hooks=<count>
effect_manager_gated=<count>
effect_already_authored=<count>
effect_reset_or_constant=<count>
effect_child_inherited=<count>
```

At native 60 FPS, startup reports:

```text
effect_timing=native_bypass
```

Periodic runtime statistics remain on the existing five-second cadence and
add separate totals for:

- managed effect advance/skip;
- effect cadence run/reject;
- authored-millisecond operand redirects;
- target-frame-to-authored mappings;
- authored-duration-to-target scaling;
- each newly added producer-boundary site.

At minimum, flow-item frame mapping, tutorial elapsed mapping, and chart
pre-roll duration scaling have distinct counters. Logging is aggregated; no
per-effect or per-frame line is emitted.

The site counters help correlate a gameplay scenario with the code path it
actually exercised. An evidence-only row is never expected to produce a
runtime counter.

## Verification

### Pure and context tests

Tests cover 60, 120, 144, and 240 FPS.

Required cases include:

- integer-ratio target-to-authored mapping;
- non-integer-ratio floor mapping;
- round-half-up duration scaling;
- zero and negative-sentinel preservation;
- overflow rejection;
- flow-item EAX mapping before the original store;
- tutorial EDX mapping before the shared local store;
- chart pre-roll EAX scaling before the comparison local store;
- unchanged native-60 values.

At 240 FPS, representative assertions include:

```text
target frame 8 -> authored frame 2
authored duration 25 -> target duration 100
```

### Plan and manifest tests

Tests assert:

- exact RVA and byte pattern for every effect hook;
- no duplicate site id or RVA;
- no `unknown` disposition;
- every `hook` row has exactly one installed hook contract;
- every installed effect contract has exactly one manifest row;
- transformed plans include the effect set;
- native-60 plans exclude the transformed effect set;
- hook-plan capacity is sufficient;
- existing non-effect contracts are unchanged.

### Asset catalog tests

The offline parser is tested with synthetic byte fixtures so the repository
does not need to commit the runtime game assets.

Tests cover:

- big-endian declared-size and count parsing;
- monotonic, in-bounds offset tables;
- truncated and overlapping records;
- proven definition-length and texture-slot fields;
- PNG signature and IHDR parsing;
- deterministic hashes and output ordering.

The live catalog run records all current files and fails static verification
on malformed containers or an incomplete image/definition cross-reference.

### IDA-backed static proof

The durable producer report records the daemon target, executable hash, exact
queries, and reviewed results. Completion requires:

- all 34 current `sub_5F07A0` callsites classified;
- every direct `effect + 0x08` write classified;
- all nine `sub_5F0450` duration calls reconciled across their three owners;
- manager-skipped external effects accounted for;
- child propagation proven authored;
- exact original bytes captured for each hook;
- no unknown or inferred-only domain assignment.

Function names are navigation aids. The proof cites instructions, data flow,
and original bytes rather than relying on names assigned in the IDB.

### Build and automated test gate

Run the focused framerate tests, asset-catalog tests, and the normal GCLoader
build. A successful build and static audit allow only this claim:

```text
implementation and static verification complete
```

They do not allow a gameplay-success claim.

## Runtime acceptance matrix

The user performs gameplay acceptance after receiving the statically verified
build.

| FPS | Purpose |
|---:|---|
| 60 | Native-bypass timing and visual baseline |
| 120 | Integer-ratio transformed behavior |
| 144 | Non-integer-ratio and rounding behavior |
| 240 | Primary high-FPS acceptance |

The scenario checklist covers:

- tutorial prompts, including the texture-slot-13 family;
- ordinary hit/impact effects;
- GREAT/GOOD and related judgement effects;
- gameplay countdown;
- chart/gimmick effects;
- nested/child effects;
- CTune flow-item effects;
- player-position and remote-player visuals.

The asset catalog links each reproducible scenario to its definition and
texture set where the executable provides a proven mapping.

For each scenario:

- wall-clock lifetime matches the 60 FPS baseline within one target frame;
- authored frame order is preserved;
- an authored frame may repeat across target frames but must not be skipped by
  an unintended target-domain index;
- trigger timing and cadence do not shift;
- an already-correct effect does not become slowed or double-mapped;
- relevant site/family counters increase;
- no fatal conversion or installation error occurs.

## Runtime findings and follow-up

If one effect remains wrong, the report must identify:

```text
target FPS
gameplay scenario
visible effect or asset identity
whether it is too fast, too slow, truncated, delayed, or never shown
relevant runtime counters
```

The next investigation starts from the corresponding producer-manifest row and
asset cross-reference. It does not fall back to a global renderer mapping.

## Acceptance boundary

This design is complete when:

- the producer-boundary architecture is implemented;
- the code and asset manifests close with no unknown rows;
- all guards, builds, and automated tests pass;
- IDA-backed static evidence is committed;
- the user completes the runtime matrix and confirms the visible behavior.

Before the final item, the work may be described only as statically complete
and awaiting gameplay acceptance.
