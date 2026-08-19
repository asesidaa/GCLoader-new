> **HISTORICAL BINARY/HOOK EVIDENCE.** The post-ASIO rollback does not install
> the high-FPS input/judgement hooks catalogued here.

# High-FPS Input/Judgement Hook Manifest

Date: 2026-08-16
IDA database: `H:\gc\game471.exe.i64`
Supported executable: `H:\gc\game471.exe`

The supported executable is exactly 3,691,008 bytes with SHA-256
`FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.
Every entry below was checked against the already-running IDA-CLI daemon for
that database. Runtime installation must validate every hook and helper prefix
before creating the first hook.

The approved correction rule is documented in
[High-FPS Authoritative Input Judgement Correction](../superpowers/specs/2026-08-16-high-fps-authoritative-input-judgement-correction-design.md).
Each native core call freezes all ready transitions with `T <= R` into one
immutable sample. Query order and native handler return values do not consume
or mutate that sample. Current-note and free-tap routing use the same physical
event timestamp, and grade correction is permitted only after native-shaped
input selection identifies the event used by the handler.

## Hook entries

| Site | VA | RVA | Expected prefix |
|---|---:|---:|---|
| Core judgement | `0x5D68E0` | `0x1D68E0` | `55 8B EC 6A FF 68 31 A6 67 00 64 A1 00 00 00 00` |
| Note dispatcher | `0x5D5720` | `0x1D5720` | `55 8B EC 83 EC 10 89 4D F4 C6 45 FB 00` |
| Direction matcher | `0x5D2E50` | `0x1D2E50` | `55 8B EC 6A FF 68 62 92 67 00 64 A1 00 00 00 00` |
| Held age | `0x6594D0` | `0x2594D0` | `55 8B EC 83 EC 10 89 4D F0 C7 45 FC 00 00 00 00` |
| Direction | `0x659390` | `0x259390` | `55 8B EC 83 EC 14 89 4D F0 8B 4D F0 E8 2F 7D DA FF` |
| Late gate | `0x5D0BE0` | `0x1D0BE0` | `55 8B EC 83 EC 0C 89 4D F4 C6 45 FB 00` |
| Grade | `0x5D0E00` | `0x1D0E00` | `55 8B EC 83 EC 4C 89 4D CC 8B 45 08 D9 80 B0` |
| Free-tap permission branch | `0x5D76CE` | `0x1D76CE` | `0F B6 85 57 FF FF FF 83 F8 01 75 0F` |

## Read-only helpers

| Helper | VA | RVA | Expected prefix |
|---|---:|---:|---|
| Note descriptor | `0x43CC50` | `0x03CC50` | `55 8B EC 83 EC 08 89 4D F8 C7 45 FC 00 00 00 00` |
| Normalize direction | `0x5D2E00` | `0x1D2E00` | `55 8B EC 83 EC 08 89 4D FC E8 C2 E3 E2 FF` |
| Angle to direction | `0x62E1D0` | `0x22E1D0` | `55 8B EC 83 EC 08 C7 45 FC 05 00 00 00 D9 EE` |

The descriptor helper receives the dispatcher's `this` in ECX and `a2`/`a3`
on the stack; the dispatcher does not pass its `a4` to this helper. This is
visible at `0x5D572D..0x5D5738` as two pushes followed by restoration of the
dispatcher object into ECX. The returned descriptor stores note type at offset
0, the native mute predicate at offset 8, mute/outer-dispatch time at offset
152, unmute/playable time at offset 156, late limit at offset 160, and target
degrees at offset 240. The loader reads and order-validates the first five
fields defensively for every dispatched note and reads target degrees only for
FLICK and SLIDE HOLD.
At bridge activation, raw direction codes 0 through 9 are normalized once with
the validated normalization helper. For direction notes only, dispatch resolves
the target, target +20 degrees, and target -20 degrees through the validated
angle helper and records the descriptor address as the note identity.

## Caller inventory

- Grade helper: `0x5D1F2A` (normal family) and `0x5D34C5` (flick).
- Late gate: `0x5D1E41`, `0x5D33F1`, `0x5D369F`, `0x5D3A5D`,
  `0x5D3E23`, and `0x5D42BA`.
- Pressed wrapper: normal `0x5D1EC0`; free tap `0x5D20E0` and
  `0x5D2176`; beat `0x5D3A26`; scratch `0x5D3D83`, `0x5D3DA6`,
  `0x5D3DC9`, and `0x5D3DEC`; hold `0x5D4325`.
- Held wrapper: direction matcher `0x5D2F93` and `0x5D303B`; hold
  `0x5D43B8`.
- Held-age wrapper: `0x5D2FC8`.
- Direction wrapper: `0x5D316F`.
- Duration helper `0x5D04F0` callers: `0x5D069C`, `0x5D06E6`,
  `0x5D3840`, `0x5D3B8A`, `0x5D3FAA`, `0x5D4424`, and `0x5D562F`.
  This helper and all of these calls remain untouched.
- Free-tap permission branch: `0x5D76CE` reads the native byte at
  `EBP - 0xA9`; permission one reaches the existing call at `0x5D76E4` to
  `0x5D2040`. The loader may change only that byte from zero to one. It never
  calls `0x5D2040`, changes EIP, or implements free-tap effects itself.

Core `0x5D68E0` dispatches a descriptor after `R > descriptor[38]` regardless
of the mute flag. Later, it disables free tap only after finding a current
descriptor whose offset-8 mute predicate is false. Therefore a muted HIDDEN or
HIDDEN2 descriptor intentionally receives its note-handler input and also
leaves the native post-note free-tap/keysound path enabled for that sample.

## Authoritative query-role contract

The runtime classifier uses the direct call instruction RVA, active note type,
booster component, requested frame shape, and direction-matcher scope as one
contract. A current-note component is exactly 0 or 1. Unsupported combinations
remain native and are reported as contract anomalies; there is no permissive
unscoped gameplay-query path.

| Role | Direct caller RVA | Accepted note/context | Frame shape |
|---|---|---|---|
| `PressedCurrentNote` | `0x1D1EC0` | NORMAL, MERRY GO ROUND, HIDDEN, HIDDEN2, CRITICAL; component 0 or 1 | current |
| `PressedCurrentNote` | `0x1D3A26` | BEAT; component 0 or 1 | current |
| `PressedCurrentNote` | `0x1D3D83`, `0x1D3DA6`, `0x1D3DC9`, `0x1D3DEC` | SCRATCH; component 0 or 1 | current |
| `PressedCurrentNote` | `0x1D4325` | HOLD or DUAL HOLD; component 0 or 1 | current |
| `PressedFreeTap` | `0x1D20E0`, `0x1D2176` | no active note and no component | current |
| `HeldCurrent` | `0x1D43B8` | HOLD or DUAL HOLD; component 0 or 1 | current |
| `HeldCurrent` | `0x1D2F93` | verified FLICK or SLIDE HOLD matcher scope | current |
| `HeldAuthoredMinusTwo` | `0x1D303B` | verified FLICK or SLIDE HOLD head matcher scope | exactly recognition frame minus 2, without unsigned wrap |
| `HeldAgeAuthored60` | `0x1D2FC8` | verified FLICK or SLIDE HOLD matcher scope, head or continuation | current |
| `DirectionCurrent` | `0x1D316F` | verified FLICK or SLIDE HOLD matcher scope, head or continuation | current |

Nested direction queries additionally require the exact matcher call site:
FLICK head component 0/1 uses `0x1D3425`/`0x1D3448`; SLIDE HOLD head component
0/1 uses `0x1D36D3`/`0x1D36F6`; SLIDE HOLD continuation component 0/1 uses
`0x1D37A7`/`0x1D37CA`. Only the head requests frame-minus-two freshness.
Continuation still invokes held age, but native acceptance ignores both age and
history and uses current held state plus the target direction.

The outer pressed/held wrappers capture their direct call-site RVA and requested
frame. The direction-matcher wrapper captures and validates its own direct caller
before entering a head/continuation scope around exactly one native matcher call;
nested held, history, held-age, and direction helpers inherit that validated
scope. Late-gate and grade wrappers derive their direct caller as
`_ReturnAddress() - executable_base - 5`, subtracting the near-call length once.
The bridge alone completes these invocations with the active dispatcher note type,
identity owner, component, and recognition frame. At target FPS 60 the
authoritative callbacks are absent: Switch retains only its independent native
query and fixed alias composition.

Late-gate caller matching is `0x1D1E41` for the normal family, `0x1D33F1` for
FLICK, `0x1D369F` for SLIDE HOLD, `0x1D3A5D` for BEAT, `0x1D3E23` for SCRATCH,
and `0x1D42BA` for HOLD/DUAL HOLD. Grade caller matching is `0x1D1F2A` for the
normal family and `0x1D34C5` for FLICK. Duration helper `0x5D04F0` is not a
timing-site hook and remains untouched.

The dispatcher branches only for types 1 through 10 and 15. Types 11 through
14 take its default return and have no independent pressed/held wrapper call in
the whole-binary caller inventory.

Both grade callers perform input acceptance first. They then store the grade
helper's return as grade data; neither uses that return to clear or undo the
handler's accepted-hit flag. The grade wrapper adds only the selected event's
rounded `T - R` delta to the native argument. The core recognition time and all
duration/repetition mechanics remain native.

## Native handler-result audit

The dispatcher return is not an input-consumption signal. It is retained only
as bounded observation after the immutable sample has already been exposed:

| IDs | Native result behavior | Immutable-sample rule |
|---|---|---|
| 0, 11-14 | No independent input handler | No synthetic current-note query is exposed |
| 1, 6-9 | NORMAL-family state can change before a false return | Native false does not relabel or replay the sample |
| 2 | FLICK can evaluate or mutate direction state before a false return | Direction observations remain visible for the full core scope |
| 3 | HOLD head acceptance starts native hold state while the handler remains false until completion | The head may use an edge; continuation uses held/history state only |
| 4 | SCRATCH returns true only for eventual long-form completion | Each native-shaped query observes the same sample |
| 5 | BEAT returns true only for eventual long-form completion | Repeat queries do not consume the selected event |
| 10 | SLIDE HOLD head/direction evaluation can occur while the handler remains false | Head direction may select an edge; continuation creates no edge |
| 15 | DUAL HOLD forwards the HOLD result for its booster components | Both components observe the same immutable sample |
| Free tap | Executes after note processing and has no note-completion result | `T <= boundary < R` may promote the native permission byte; other cases remain native |

This table was rechecked read-only through `AgentSession.connect` against the
same live IDA-CLI daemon. The free-tap prefix, call, and frame slot were also
rechecked on 2026-08-16 without mutating or saving the IDB. The focused live
audit artifact is
`H:\gc\runs\20260815T182438Z-297470b1\artifacts\highfps-judgement-live-audit-2026-08-16.json`
(SHA-256
`2E5FAF3688A9E02272595DBC6B16A67943C9DEAA33582016F70E9C829FB9524F`).

## Bounded runtime diagnostics

The cumulative summary distinguishes captured, mapped, deferred, delivered,
and same-control coalesced transitions; exact and rounded anchors; first and
suppressed anchor failures; native and forced free-tap presentations; separate
transport and mapped-pending evictions; transport, mapped-pending, and maximum
depths; authored-history rotations; gate rescues; accepted edges; grade
adjustments; recovered/suppressed inputs; Switch alias and diagonal
acceptances; timing-delta aggregates; note-family counts; hook, contract, and
invariant anomalies; ring overwrites; and each real epoch-reset reason.
Authored-history rotation and same-control coalescence are observations, not
input loss. Counters remain monotonic across gameplay-generation resets.

The fixed 256-record ring stores meaningful transition-delivery,
gate-candidate, accepted-edge, grade-selection, input-correction,
free-tap-route, contract-anomaly, and actual-input-loss records. A record can
retain event `T`, recognition `R`, `T - R`, note type, `booster_component`,
mute time, unmute time, late limit, route, requested/source input, match reason, gate/grade
deltas, `grade_native_argument_ms`, `grade_forwarded_argument_ms`, note target,
signed grade error and result, Switch style, native handler result, epoch,
playback generation, sequence, cohort, anchor source, cursor-query QPC span,
native/forced free-tap path, and loss source. Per-event keys suppress repeated
records from native helper re-queries. Periodic output drains the ring every
five seconds but formats at most 32 records; further records and overwrites are
summarized. Transport and mapped-pending loss warnings remain separate and
report the first occurrence and then each additional 64. No per-core,
per-query, held-continuation, or ordinary zero-step log is emitted.

## Late-gate/input ordering audit

The table below records an independent review of every dispatcher ID plus the
post-note free-tap path. `PreviewButton` and `PreviewDirection` mean a
non-consuming candidate is selected when the note begins because native code
calls the late gate before its input query. `SelectedBeforeGate` means native
code queries input first, so the late gate uses the edge actually selected by
that query. A preview never grades or mutates an edge.

| ID | Type | Native handler evidence and ordering | Late-gate edge mode | Preserved behavior |
|---:|---|---|---|---|
| 0 | NONE | Dispatcher default; no independent input or late gate | `None` | Native lifecycle only |
| 1 | NORMAL | `0x5D1FA0 -> 0x5D1D50`; gate `0x5D1E41` precedes pressed `0x5D1EC0`; grade `0x5D1F2A` | `PreviewButton` | Gate previews; grade adjusts only an observed selection |
| 2 | FLICK | `0x5D3320`; gate `0x5D33F1` precedes direction matcher `0x5D3425`/`0x5D3448`; grade `0x5D34C5` | `PreviewDirection` | Gate previews a compatible current or retained-held rise; grade requires an accepted match |
| 3 | HOLD | `0x5D41B0`; head gate `0x5D42BA` precedes pressed `0x5D4325`, then held `0x5D43B8`; duration `0x5D4424` | `PreviewButton` | Head can select an edge; duration/release stay native |
| 4 | SCRATCH | `0x5D3C60`; pressed `0x5D3D83`/`0x5D3DA6`/`0x5D3DC9`/`0x5D3DEC` precedes gate `0x5D3E23`; duration `0x5D3FAA` | `SelectedBeforeGate` | The observed direction edge feeds the gate; duration stays native |
| 5 | BEAT | `0x5D3920`; pressed `0x5D3A26` precedes gate `0x5D3A5D`; duration `0x5D3B8A` | `SelectedBeforeGate` | The observed repeat edge feeds the gate; cadence and duration stay native |
| 6 | MERRY GO ROUND | `0x5D5660 -> 0x5D1D50` at `0x5D56D2`; normal-family gate precedes button | `PreviewButton` | Adds only `T - R` to the native segment-adjusted argument |
| 7 | HIDDEN | `0x5D1FA0 -> 0x5D1D50`; normal-family gate precedes pressed; muted descriptors do not disable the later free-tap call | `PreviewButton` | Same accepted-edge rules as NORMAL plus native dual note/free-tap presentation |
| 8 | HIDDEN2 | `0x5D1FA0 -> 0x5D1D50`; normal-family gate precedes pressed; muted descriptors do not disable the later free-tap call | `PreviewButton` | Same accepted-edge rules as NORMAL plus native dual note/free-tap presentation |
| 9 | CRITICAL | `0x5D1F70 -> 0x5D1D50` at `0x5D1F92`; normal-family gate precedes each booster component's button | `PreviewButton` | Both booster components observe one sample; native aggregation stays native |
| 10 | SLIDE HOLD | `0x5D35C0`; head gate `0x5D369F` precedes matchers `0x5D36D3`/`0x5D36F6`; continuation matchers `0x5D37A7`/`0x5D37CA`; duration `0x5D3840` | `PreviewDirection` | Head can select a current or retained-held rise; continuation and duration stay native |
| 11 | SLIDE COUNTER | Dispatcher default; no independent input query | `None` | Native lifecycle marker |
| 12 | TURN | Dispatcher default; no independent input query | `None` | Native lifecycle marker |
| 13 | SPIN | Dispatcher default; no independent input query | `None` | Native lifecycle marker |
| 14 | FINISH | Dispatcher default; no independent input query | `None` | Native lifecycle marker |
| 15 | DUAL HOLD | `0x5D5540 -> 0x5D41B0` at `0x5D555C`; inherits HOLD head ordering; duration path includes `0x5D562F` | `PreviewButton` | Both booster components observe one sample; duration stays native |
| - | Free tap | Permission branch `0x5D76CE`; native implementation `0x5D2040`; pressed `0x5D20E0`/`0x5D2176` after note processing | `None` | Equality belongs to free tap; muted current descriptors already permit it natively; only non-muted `T <= mute_time < R` needs forced routing |

This audit was performed read-only through `AgentSession.connect` to the
existing IDA-CLI daemon. It did not mutate or save the IDB. Runtime diagnostics
retain selected timing adjustments and native handler results only as bounded
observation. A false native handler result is not labelled a miss, and no new
unbounded log source is installed.

## Native x86 interfaces

- Core: `void __thiscall(void* self, int recognition_ms, int gameplay_frame)`.
- Dispatcher: `char __thiscall(void* self, int a2, int a3, int a4,
  unsigned booster_component, int recognition_ms)`.
- Direction matcher: `char __thiscall(void* self, int a2, int descriptor,
  int* booster, unsigned char* history_match, char left, char right,
  char continuation)`.
- Held age: `int __thiscall(void* self, int device, int logical_input,
  int gameplay_frame)`.
- Direction: `int __thiscall(void* self, int device, int booster,
  int gameplay_frame)`.
- Late gate: `char __thiscall(void* self, int state, int a2, int a3,
  int recognition_ms, int late_limit)`.
- Grade: `int __thiscall(void* self, float* descriptor, int grade_time)`.
- Descriptor: `void* __thiscall(void* table, unsigned row, unsigned column)`.
- Normalize direction: `int __thiscall(void* self, int raw_direction)`.
- Angle to direction: `int __cdecl(float degrees)`.
- Free-tap permission is a mid-hook at `0x5D76CE`, not a callable native
  interface. It changes only the byte at `EBP - 0xA9` before the original
  `movzx` executes.

The inline hooks use `__fastcall` shims for native thiscall entries, retaining
the original ECX object and ignoring the shim's EDX argument. Every wrapper
falls back to one native call if loader-side correction fails.

Hex-Rays previously rendered the late gate as `__stdcall`, but that label is
not consistent with the machine code. The entry stores ECX and uses it as the
receiver, and each of its six callers pushes five stack arguments, loads the
receiver into ECX, consumes the result from AL, and relies on `retn 14h` for
callee cleanup.

Hex-Rays also renders direction normalization as `__stdcall`. Both native
callers nevertheless load the active judgement object into ECX, and the entry
stores that receiver before reading its one stack argument and returning with
`retn 4`. The supported executable does not subsequently read the saved
receiver, but the loader still forwards the live judgement receiver instead
of relying on that implementation detail.

The optimized x86 Release objects were also checked after hook compilation.
The shim cleanup sizes match the native entries: core `ret 8`, dispatcher
`ret 14h`, direction matcher `ret 1Ch`, held age and direction `ret 0Ch`, late
gate `ret 14h`, and grade `ret 8`. Shims that execute helper calls before the
trampoline preserve and restore the incoming ECX receiver; the remaining
shims reach the trampoline before ECX is clobbered. The shared pressed and held
query shims both use `ret 0Ch`, and their common trampoline callback restores
the saved native receiver to ECX before forwarding all three stack arguments.
