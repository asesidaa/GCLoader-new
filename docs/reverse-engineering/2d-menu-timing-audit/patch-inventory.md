# Current GCLoader 2D Menu Timing Patch Inventory

- Status: complete
- Audit date: 2026-07-25
- Owner: current-patch evidence analyst
- Write scope: this findings file only; production code, tests, the XFL corpus, and the IDB remain untouched.

## Scope

This document inventories the **currently deployed source state** that can affect Flash-like 2D/RVB menu animation timing at arbitrary target FPS. It covers direct writes, hooks, timing transforms, transactions, diagnostics, tests, and source-level assumptions, including possible consequences for nested movie clips and script-driven transitions.

It does not claim binary reach. Source evidence can establish what GCLoader installs and how its callbacks behave, but whether a patched machine-code site is a universal scheduler, a single callsite, a content-specific task, or a partial path belongs to the separate IDA trace.

## Fixed baseline

- Worktree: `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`
- Branch: `ctune-effect-timing`
- Commit: `2354d0fea16fda94c344d48e116dbc908fdcb49c`
- Baseline commit message: `docs: prove CTune effect producer coverage`
- `main` at audit start: `f5c95dea4c4ad92de428e0fbc05134a2af1335a8`
- Runtime log used only as supporting evidence: `H:\gc\loader-log.txt`
- Master ledger: `docs/reverse-engineering/2d-menu-timing-audit/README.md`

The deployed DLL was built from this worktree according to the fixed audit ledger. This audit therefore treats `2354d0f` as the deployed source baseline and compares its 2D-related state with `main`; it does not infer the deployed state from older rollback notes.

## Method and evidence labels

1. Read all framerate planning, runtime, transaction, diagnostic, and focused test sources.
2. Search the whole repository for 2D runtime terms, hook identifiers, timing transforms, and transition operations.
3. Enumerate every current direct write and hook with its install guard and callback contract.
4. Model authored-tick selection at 60, 120, 240, and non-multiple targets, including callback sharing within one render frame.
5. Audit tests by assertion and identify omitted content, tags, and call paths.
6. Compare this worktree against `main`.
7. Use `loader-log.txt` only to confirm observed counters/installation in one run, never to prove content completeness.

Evidence labels used below:

- **Source-proven**: directly established by the current C++/headers/tests.
- **Test-proven**: asserted by a current automated test.
- **Runtime-observed**: present in the supplied runtime log.
- **Assumption/inference**: suggested by names, comments, or control flow but not established across the executable.
- **IDA question**: requires binary tracing owned by the IDA analyst.

## Checkpoint ledger

| Checkpoint | Status |
|---|---|
| Scope, method, and deployed baseline recorded | Complete |
| Source hook/write/transform inventory | Complete |
| Deterministic timing-model analysis | Complete |
| Test and diagnostic gap analysis | Complete |
| Main comparison and final coverage verdict | Complete |

## Installation and selection model

**Source-proven.** `FrameratePatchInit` creates one immutable `FramerateProfile`, one `Authored60PhaseClock`, one global atomic authored-tick boolean, one hook storage object, and one transaction. The transaction:

1. preflights the exact expected bytes for every selected direct write and hook before changing memory;
2. applies all direct writes;
3. installs hooks in plan order;
4. rolls installed hooks back in reverse order and restores direct-write bytes if any later operation fails.

At target 60, the direct-write plan is empty. The 2D transforms are not selected; `OuterFrame` remains selected for cap monitoring, and the unrelated audio-resync hook can also be selected when WASAPI committed. Above 60, all 17 direct writes and all transformed-timing hook contracts other than `AudioResyncPolicy` are selected; `AudioResyncPolicy` is additionally selected only when WASAPI committed. The supplied 240-FPS log reports 17 writes and all 46 hooks committed.

This is strong install-integrity evidence, but not semantic coverage evidence: the transaction proves entry bytes matched and hook creation succeeded, not what every entry reaches.

## Current 2D/menu hook inventory

Assume the required executable base `0x00400000`; VA is base plus RVA. “Original” below means SafetyHook's trampoline for the overwritten function/instruction.

| Symbolic ID | RVA / VA | Expected bytes | Hook and ABI contract | Transform at target `F > 60` | Native 60 behavior | Counter(s) | Intended content family |
|---|---:|---|---|---|---|---|---|
| `OuterFrame` | `0x00058B70` / `0x00458B70` | `56 8B F1 8B 06 8B 50 24` | Mid-hook; callback ignores the captured registers, reads QPC, feeds the cap monitor, increments `outer_calls`, then publishes one shared phase result. It does not skip the original instruction. | Advances one process-global rational 60-Hz phase once per hook callback; stores one atomic boolean consumed by the hooks below. | Still observes/counts the outer callback, then returns before phase advance and runtime-stat logging. | `outer`; `authored_ticks`; `authored_non_ticks` | Clock source assumed to correspond one-for-one with target-rate outer updates/presented frames. |
| `MovieClipGoto` | `0x000DEA30` / `0x004DEA30` | `6A FF 68 C9 38 67 00` | Inline function-entry hook declared `char __fastcall(self, unused_edx, int frame, int subframe)` and forwarding as original `thiscall`. A thread-local RAII depth is nonzero for the entire original call. | Does not gate or remap the requested frame. Any `MovieClipAdvance` reached synchronously on the same thread while depth is nonzero bypasses the authored-tick gate. | Hook not installed. | No entry counter. `movieclip_goto_calls` counts nested **advance** calls observed while depth is nonzero, not goto calls themselves. | Explicit goto-frame traversal/frame-action protection. |
| `MovieClipAdvance` | `0x000DF940` / `0x004DF940` | `56 8B F1 8B 06 8B 90 4C 01 00 00` | Inline function-entry hook declared `char __fastcall(self, unused_edx, char forward, char loop)` and forwarding as original `thiscall`. | If goto depth is nonzero: always call original. Otherwise, call original only when the shared authored tick is true; on a non-tick return literal `1` without invoking any original side effect. | Hook not installed; native function executes normally. | `movieclip_calls` = ordinary originals executed on ticks; `movieclip_skips` = ordinary calls suppressed; `movieclip_goto_calls` = advance originals executed under goto depth. | Ordinary automatic MovieClip timeline advance. |
| `NavigatorAdvance` | `0x001B6310` / `0x005B6310` | `55 8B EC 83 EC 08 89 4D FC 8B 45 FC 8B 48 60` | Inline function-entry hook declared `void* __fastcall(self, unused_edx)` and forwarding as original `thiscall`. | Calls original only on the shared authored tick. On a non-tick, returns `self`; rendering is not explicitly hooked here. | Hook not installed. | `navigator_advances`; `navigator_skips` | Shared manual DDS navigator transition/face-cell state, not a MovieClip timeline. |
| `IfblWait` | `0x002309D4` / `0x006309D4` | `89 4A 3C` | Mid-hook over `mov [edx+0x3C],ecx`; `ECX` is the raw wait and `EDX` is the destination owner. On successful safe write, advances `EIP` by 3 to skip the original store. | Preserves raw 0 and 1. For a positive signed value greater than 1, writes nearest-half-up `round(value * F / 60)`. Signed-nonpositive/sentinel bit patterns survive. A scale/store failure is fatal. | Hook not installed; original value stores unchanged. | `ifbl_wait_stores` | Shared IFBL integer-wait store. It can affect script-driven menu/transition timing, but it is not a MovieClip animation hook. |

### Source-level reach: proved versus assumed

- **Source-proven:** `MovieClipAdvance`, `MovieClipGoto`, and `NavigatorAdvance` are callee-entry inline hooks, not individual callsite hooks. If all relevant callers use those exact function entries, every such caller is intercepted.
- **Source-proven:** every ordinary MovieClip callback that occurs after one `OuterFrame` publication and before the next reads the same atomic tick. The phase is not advanced per MovieClip instance or per Navigator call.
- **Source-proven:** the goto exception is dynamic-call-stack based, thread-local, and applies only while the hooked goto original is executing. It does not identify labels, ActionScript opcodes, assets, or clip hierarchy.
- **Source-proven:** there is no current `Anim::DrawTraverse` hook. Coverage of `Anim::DrawTraverse` is indirect and depends on it reaching `0x004DF940`.
- **Historical binary-backed evidence, not re-proved by source/tests:** `E-027` says `Anim::DrawTraverse` mode 1 traverses the root, calls the MovieClip forward wrapper, and reaches `0x004DF940`, with five vtable occurrences belonging to one inheritance/adaptor chain. This supports callee-wide ordinary-instance coverage, but `E-027` explicitly stops short of proving every high-level owner/cadence harmless.
- **Historical binary-backed evidence, not re-proved by source/tests:** `E-039` says `0x005B6310` is the narrow shared manual navigator state advance and that drawing remains native-rate. Current source preserves that exact entry hook and behavior.
- **Assumption requiring current IDA correlation:** `OuterFrame` executes exactly once and before every relevant 2D traversal in a target frame. Source has no scheduler ordering, reentrancy, or thread-affinity assertion. A second outer callback can publish a new boolean while another thread is still processing 2D work.
- **Assumption requiring current IDA correlation:** every script-level goto/play/stop or label transition that must reposition a clip passes through the hooked `0x004DEA30` while any nested `0x004DF940` call remains on the same thread and dynamic stack.
- **Assumption requiring current IDA correlation:** returning `1` from a skipped MovieClip advance and `self` from a skipped Navigator advance faithfully reproduces all non-advance return/side-effect semantics.

## Deliberately absent broad gates

The current transformed plan explicitly omits these former/invalid sites:

| Absent RVA / VA | Historical role | Current consequence |
|---:|---|---|
| `0x00218A50` / `0x00618A50` | Complete News task update | News stays native-rate; source diagnostic says `news_notice_updates=native`. |
| `0x002544D0` / `0x006544D0` | Complete Notice task update | Notice stays native-rate; elapsed-time waits are not throttled with the whole task. |
| `0x00230AB6` / `0x00630AB6` | IFBL loop-count scaling | Script loop cardinality remains original; diagnostic says `ifbl_loops=original`. |
| `0x0024F0C6` / `0x0064F0C6` | Player-position decrement gate | Gameplay-only adjacent contract; decrement stays native. |

The tests assert only that these RVAs are absent from the hook manifest. They do not execute a News/Notice descriptor or prove those tasks have no other gates.

## Direct-write inventory and 2D relevance

For target `F > 60`, let `S(n) = floor((n * F + 30) / 60)`. All 17 writes are absent at native 60. The table includes the complete direct plan so a global-looking value is not silently mistaken for or excluded from a Flash clock.

| Checked-write name | RVA / VA | Expected bytes/value | Replacement | Source-level 2D assessment |
|---|---:|---|---|---|
| `gameplay frame milliseconds` | `0x002FC0A0` / `0x006FC0A0` | `55 55 85 41` (`1000/60`) | float `1000/F` | Named gameplay clock; no source connection to MovieClip/Navigator. |
| `visual frame milliseconds` | `0x002F4604` / `0x006F4604` | `55 55 85 41` (`1000/60`) | float `1000/F` | Potential false-positive risk because the name is broad. Source does not list consumers or prove whether Flash/RVB reads it. |
| `gameplay frame seconds` | `0x002FC280` / `0x006FC280` | `89 88 88 3C` (`1/60`) | float `1/F` | Named gameplay clock; no source connection to Flash. |
| `render smoothing step` | `0x002E8F00` / `0x006E8F00` | `00 00 80 40` (`4.0`) | float `4 * 60/F` | Could affect generic rendered motion by name, but source gives no 2D consumer reach. |
| `render offset-decay step` | `0x002E8F04` / `0x006E8F04` | `00 00 A0 40` (`5.0`) | float `5 * 60/F` | Same unproven global-name risk as smoothing. |
| `XIO repeat initial duration` | `0x00055CCC` / `0x00455CCC` | `C7 00 10 00 00 00` | same instruction with immediate `S(16)` | Menu-adjacent input cadence, not autonomous animation. Can change when a script-driven transition is triggered. |
| `XIO repeat next duration` | `0x00055CDD` / `0x00455CDD` | `C7 00 08 00 00 00` | immediate `S(8)` | Menu-adjacent input cadence. |
| `native keyboard repeat initial duration` | `0x0005F843` / `0x0045F843` | `C7 86 D4 02 00 00 10 00 00 00` | immediate `S(16)` | Menu-adjacent input cadence. |
| `native keyboard repeat next duration` | `0x0005F84D` / `0x0045F84D` | `C7 86 D8 02 00 00 08 00 00 00` | immediate `S(8)` | Menu-adjacent input cadence. |
| `gameplay countdown duration` | `0x002645EE` / `0x006645EE` | `C7 80 14 1D 00 00 78 00 00 00` | immediate `2F` | Gameplay-only by source naming. |
| `render EAX countdown duration` | `0x00249A5E` / `0x00649A5E` | `B8 78 00 00 00` | `B8 <2F>` | Gameplay CTune/effect path, not Flash menu. |
| `render EDX countdown duration` | `0x00249A73` / `0x00649A73` | `BA 78 00 00 00` | `BA <2F>` | Gameplay CTune/effect path, not Flash menu. |
| `palette normalizer operand one` | `0x0022BACF` / `0x0062BACF` | `D8 2D AC BB 6F 00` | same opcode with loader `target_fps_float` address | Palette path; content ownership is not encoded in source. |
| `palette normalizer operand two` | `0x0022BAD5` / `0x0062BAD5` | `D8 35 AC BB 6F 00` | same opcode with loader `target_fps_float` address | Palette path; content ownership is not encoded in source. |
| `chart seconds-to-frames operand` | `0x00262CB6` / `0x00662CB6` | `D8 0D AC BB 6F 00` | same opcode with loader `target_fps_float` address | Chart/gameplay path by name. |
| `non-song menu repeat initial duration` | `0x00382CE8` / `0x00782CE8` | `10 00 00 00` | little-endian `S(16)` | Explicit non-song menu input-repeat timing; still not a MovieClip timeline. |
| `non-song menu repeat interval` | `0x00382CEC` / `0x00782CEC` | `03 00 00 00` | little-endian `S(3)` | Explicit non-song menu input-repeat timing. |

No direct write is named as a MovieClip, Navigator, RVB, nested-clip, ActionScript, label, or timeline write. Autonomous Flash-like animation correction is entirely hook/phase based in current source.

## Other current hook families excluded from Flash-menu coverage

The complete transformed manifest contains 46 hooks: the five menu/clock/script-adjacent contracts above, 34 effect-manifest contracts (33 CTune-related plus the retained non-CTune `FixedVisualFrameOperand`), and seven stage/palette/countdown/audio contracts. The effect manifest explicitly names gameplay, tutorial, chart, player-position, and remote effect owners; it is a separate gameplay-effect runtime, not evidence of Flash/RVB menu coverage. The current branch adds four CTune boundary hooks relative to `main`; none changes the MovieClip, Navigator, IFBL, input-repeat, or outer-phase callbacks.

## Deterministic phase and gate behavior

### Phase algorithm

For transformed target `F`, construction sets `phase = F - 60`. Every `OuterFrame` callback then executes:

```text
phase += 60
if phase >= F:
    phase -= F
    shared_tick = true
else:
    shared_tick = false
```

The initial offset makes the first transformed outer callback an authored tick. Over exactly `F` outer callbacks, the clock emits exactly 60 ticks. It does not use QPC elapsed time to decide or catch up; QPC is only the separate cap monitor and logging clock.

| Target | Runtime behavior | First 24 phase results (`T` = authored tick) | Consequence |
|---:|---|---|---|
| 60 | MovieClip/Navigator/IFBL transforms are not installed; phase publication is bypassed. | Conceptually the standalone clock would emit all `T`, but runtime does not use it. | Native functions run with original cadence and side effects. |
| 120 | One tick every two outer callbacks, first callback included. | `T.T.T.T.T.T.T.T.T.T.T.T.` | Ordinary MovieClip and Navigator originals are permitted on 1, 3, 5, ... |
| 240 | One tick every four outer callbacks, first callback included. | `T...T...T...T...T...T...` | Ordinary MovieClip and Navigator originals are permitted on 1, 5, 9, ... |
| 144 | Rational distribution with two- and three-callback gaps. | `T..T.T..T.T.T..T.T..T.T.` | Tick calls begin 1, 4, 6, 9, 11, 13, 16, 18, 21, 23; 60 ticks occur in 144 calls without an integer-ratio assumption. |

For other targets from 61 through 500 the same Bresenham-like distribution applies. The tests establish 60 tick results per `F` calls and deterministic reconstruction for every explicitly sampled target, but the runtime phase remains call-count based. A cap mismatch is fatal only after the monitor's warm-up and three two-second windows; a single long stall is intentionally tolerated by the median monitor and does not cause phase catch-up.

### Callback sharing and multiplicity

**Confirmed source behavior:**

- All callbacks reading the atomic between two `OuterFrame` publications share one boolean. MovieClip instances and Navigator do not own independent accumulators.
- Sharing a boolean does **not** coalesce calls. On a tick frame, every ordinary `MovieClipAdvance` invocation calls the original once. If one clip is reached twice, it can advance twice. On a non-tick frame, every ordinary invocation returns `1` without the original.
- The same multiplicity rule applies to `NavigatorAdvance`: every call on a tick invokes the original; every call on a non-tick returns `self`. There is no per-frame or per-object deduplication.
- Goto-depth bypass is stronger than the phase gate: every advance synchronously nested under the hooked goto entry runs, even on a non-tick. This preserves explicit positioning only if all relevant goto operations share that dynamic path.
- The shared atomic is a boolean, not an epoch number. Source cannot detect a callback that runs late across an outer-frame boundary, nor assert that all 2D calls are on the outer hook's thread.

**Implication for nested clips:** if `Anim::DrawTraverse` visits each parent and child exactly once and all use `0x004DF940`, they all advance together once on a tick and all remain still on a non-tick. The source neither models a clip tree nor tests visit cardinality, so nested-child correctness depends on the binary traversal and runtime call counts.

### Duration transforms adjacent to scripted transitions

- Input and IFBL authored durations use `S(n) = floor((nF + 30)/60)`, nearest-half-up. At 120, the non-song 16/3 pair becomes 32/6; at 144, 38/7; at 240, 64/12.
- The resulting wall-time error is bounded by half a target frame for the tested duration range. This preserves approximate duration, not exact authored phase alignment with MovieClip ticks.
- IFBL wait 0/1 is intentionally not scaled. Wait 15 becomes 30 at 120, 36 at 144, and 60 at 240.
- IFBL execution itself remains native-rate, as do News and Notice task updates. Therefore elapsed-seconds callbacks are not implicitly synchronized to the MovieClip phase.

## Automated test audit

The tests were inspected as source; this read-only audit did not rebuild or execute them. Existing tests are synthetic unit/manifest/transaction tests linked against `gc_runtime_patches`. None loads `game471.exe`, installs a real hook into it, or loads an XFL/RVB/MTX asset.

| Test target | Exact relevant assertions | What is genuinely protected | What is not represented |
|---|---|---|---|
| `FramerateProfileTests` | Valid target markers; 120/144 duration rounding; mapping monotonicity; one target second maps to 60; duration error bounded across every target 60..500 (`FramerateProfileTests.cpp:41,71,79,87,108,117`). | Pure arithmetic for target profiles, frame mapping, and duration scaling. | No phase publication, hook callback, asset, timeline, script, or runtime consumer. |
| `FramerateAuthoredClockTests` | 60 ticks per target second for 60/61/120/144/165/240/360/500; two reconstructed clocks agree; exact 240 `T...` sequence; IFBL 0/1/15 policy; authored-boundary/cadence counts (`FramerateAuthoredClockTests.cpp:35,42,54,76,119,122`). | The standalone deterministic phase and helper arithmetic. | No explicit 144 callback sequence, no `OuterFrame` integration, no callback ordering/threading, and no MovieClip/Navigator invocation. |
| `FrameratePatchPlanTests` | Native has no writes; transformed has 17; all direct replacement arithmetic and exact expected bytes; exact non-song repeat cases; 46-hook ID/RVA/byte array; four invalid RVAs absent; exact Navigator entry pattern; selected-plan counts with/without WASAPI (`FrameratePatchPlanTests.cpp:100,110,285,296,304,320,434,439-448`). | Compile-time manifest shape, addresses, byte guards, selection counts, and direct-write payloads. | The MovieClip/Navigator callbacks are never called. A callback could gate the wrong polarity, return the wrong value, lose goto bypass, or advance twice and these byte/manifest assertions would still pass. |
| `FramerateRuntimeTests` | Context-register behavior for non-menu transforms; target mapping; every hook ID obtains non-null install/reset function pointers (`FramerateRuntimeTests.cpp:215,220,235,240,246,258,267`). | Switch-to-binding completeness and selected pure register transforms. | `FramerateHookHasRuntimeBinding` only checks non-null pointers. It does not create SafetyHooks or execute `HookMovieClipGoto`, `HookMovieClipAdvance`, `HookNavigatorAdvance`, `HookIfblWait`, or `HookOuterFrame`. |
| `FrameratePatchTransactionTests` | Every one of 17 fake writes and 46 fake hook-index failures rolls back; preflight mismatch mutates nothing; successful fake transaction retains all operations (`FrameratePatchTransactionTests.cpp:180-212,227,276,288-290`). | Generic all-or-nothing ordering and rollback bookkeeping, including moved Navigator/Outer indices. | Fake one-byte memory and fake install callbacks do not validate current executable bytes, SafetyHook trampoline ABI, callback return semantics, or hook coexistence in the real process. |
| `FramerateDiagnosticsTests` | Startup string contains transformed/native mode, `news_notice_updates=native`, `ifbl_loops=original`, counts, and fatal reporting (`FramerateDiagnosticsTests.cpp:92,93,127,148,262`). | Stable diagnostic policy text and fatal publication. | The policy fields are formatted declarations, not introspection. MovieClip/Navigator runtime-stat formatting and counter relationships are not asserted. |
| `FramerateMonitorTests` | Window timing, exact-cadence validation, tolerance, median tolerance of one long stall, disable/fatal-clock behavior (`FramerateMonitorTests.cpp:105,110,114,140,169,179`). | The QPC callback-cadence monitor. | It does not prove `OuterFrame` equals presentation cadence or that phase publication precedes all 2D work. A tolerated stall is not phase-caught-up. |
| `FramerateEffectTimingTests` | Exact CTune registration/duration/hook manifests and pure EAX/EDX transforms (`FramerateEffectTimingTests.cpp:159,181,274,307,318,332-372`). | Gameplay CTune boundary census. | Flash/RVB menus, MovieClips, Navigator, ActionScript, and XFL hierarchy. Its one `ChildInherited` row is a CTune effect relationship, not nested MovieClip coverage. |

### E-027 and E-039 semantic regression status

- `E-027`'s binary-backed chain `Anim::DrawTraverse -> forward wrapper -> 0x004DF940` is **not** encoded in a test. Tests preserve only the `MovieClipAdvance` ID/RVA/entry bytes and the fact that an install callback pointer exists.
- `E-027`'s five-vtable inheritance/adaptor conclusion, root traversal, ordinary child coverage, and 21 elapsed-callback inventory are absent from tests.
- The goto-depth behavior called necessary by `E-027` has no regression test. Neither nesting, exception/RAII unwinding, same-thread scope, frame/subframe forwarding, nor the `return 1` skip result is asserted.
- `E-039`'s exact Navigator entry bytes and plan/binding presence are asserted. Its core semantics are not: no test counts original calls on tick/non-tick, verifies `self` on skip, proves draw remains native-rate, or represents the cross-scene owners from `E-038`.
- Transaction tests protect rollback after the Navigator/Outer indices moved from 40/41 on `main` to 44/45 in this branch, but only generically by index.

### Completely missing 2D regression fixtures

There is no test containing or simulating:

- an XFL/RVB/MTX asset, a timeline tag, a frame label, or an ActionScript bytecode/action;
- a main MovieClip plus nested child clips with independent playheads;
- `gotoAndPlay`, `gotoAndStop`, `play`, `stop`, numeric goto, label goto, or frame-action recursion;
- forward/backward and loop argument effects at `MovieClipAdvance`;
- one instance called multiple times within one outer frame;
- parent/child traversal order, multiple roots, multiple passes, reentrancy, or cross-thread traversal;
- News/Notice IFBL descriptors, float elapsed waits, label transition durations, or callback/input coexistence;
- the manual Navigator counter state and its separate native-rate draw;
- a 60/120/144/240 wall-clock sequence comparing actual displayed frames and labels;
- any check for alternate direct stores or update functions that bypass the three named 2D hooks.

The current suite is therefore strong for manifests, bytes, arithmetic, and rollback, but has no runtime-semantic regression for nested timelines or script-driven transitions.

## Current diagnostics and supplied runtime evidence

### What is emitted

Startup emits target/profile values, declared policy, selected write/hook counts, menu-repeat values, cap-validation policy, and CTune manifest counts. Every five seconds in transformed mode, cumulative atomics report:

- outer calls and authored/non-authored phase counts;
- ordinary MovieClip originals/skips and advance calls under goto depth;
- Navigator originals/skips;
- IFBL wait stores;
- unrelated stage/audio/gameplay-effect counters.

There is no tick-spacing histogram, per-frame epoch, per-object ID, scene/asset name, timeline frame, requested goto target, label, or parent/child relationship.

### Supplied 240-FPS run

`H:\gc\loader-log.txt` is a single supporting run dated 2026-07-25. The deployed DLL observed during this audit has SHA-256 `CF9349DDB4A9EF216A3AA4278A548DD2C074BBF275EC73806D0A3513DA77E7FD`.

- Startup: target 240, transformed deterministic phase, 17 direct writes, 46 hooks, menu repeat 64/12, News/Notice native, IFBL loop cardinality original.
- Transaction: committed with all 17 writes and 46 hooks.
- Cap monitor: measured `240.053 FPS`, relative error `0.000220048`, and validated after three matching windows.
- Final runtime sample:
  - `outer=35193`
  - `authored60=8799`, `non60=26394` (sum exactly equals outer; approximately 1:3)
  - `movieclip=387291`, `skip=1160897`, `goto=2346`
  - `navigator=1175`, `skip=3522`
  - `ifbl_waits=364`

This confirms that the deployed build installed the current plan, the outer phase was exercised at the expected 240-FPS ratio, many calls reached the MovieClip entry, goto-nested advances occurred, and at least one interval reached the Navigator entry.

It cannot prove:

- how many distinct MovieClip instances, assets, roots, child clips, or scenes were covered;
- that each instance advanced once rather than zero/twice on each authored tick;
- that a `goto` counter corresponds to any particular label/action or that every goto path was protected;
- that uncounted alternate timeline stores/functions do not exist;
- that skipped originals had no required non-playhead side effects;
- that Navigator draw stayed native-rate or every navigator-bearing scene ran;
- that News/Notice or any other script transition retained correct wall time;
- behavior at 60, 120, 144, or another non-multiple target;
- completeness merely from a zero counter: zero means the supplied run did not reach that instrumented path.

## Comparison with `main`

Compared with `main` at `f5c95dea4c4ad92de428e0fbc05134a2af1335a8`:

- `FramerateAuthoredClock`, `FramerateProfile`, and `FramerateHookTransforms` are byte-for-byte unchanged.
- The 17-write direct plan and all MovieClip, Navigator, IFBL, and Outer callback bodies are functionally unchanged.
- The current branch adds four CTune hooks (`EffectFlowItemFrame`, `EffectTutorialElapsed`, `EffectChartPreRollDuration`, `EffectPlayerModuloDividend`), their manifest/diagnostics/tests, and raises hook capacity from 42 to 46.
- Hook-plan assembly now inserts those four effect contracts before `NavigatorAdvance` and `OuterFrame`, moving those final hooks from indices 40/41 to 44/45 while retaining their IDs, RVAs, expected bytes, and callbacks.
- There is no deployed-branch change that expands MovieClip coverage, adds nested-clip semantics, or adds ActionScript transition tests relative to `main`.

Thus the deployed worktree's Flash-like 2D behavior is the mainline behavior plus unrelated CTune producer coverage, not a new 2D-completeness implementation.

## Final coverage assessment

### Confirmed current coverage

- Exact guarded function-entry hooks exist for MovieClip goto, ordinary MovieClip advance, and manual Navigator advance.
- One deterministic outer-call phase supplies the ordinary MovieClip and Navigator gates at every transformed target from 61 through 500; native 60 omits those transforms.
- Calls within one outer interval share the same phase boolean.
- IFBL integer waits preserve 0/1 and scale positive authored waits; News/Notice whole-task gates and IFBL loop scaling are absent.
- Non-song and lower-level input repeat durations are target-scaled without gating native input sampling.
- The current 240-FPS log proves the installed MovieClip and Navigator hooks were reached and produced the expected aggregate 1:3 outcome split.
- Historical `E-027`/`E-039` give binary-backed reasons that the selected callee boundaries are broader than one scene, but those claims need the separate current IDA trace for this audit's final matrix.

### Unproven coverage

- All Flash/RVB assets, all root owners, all nested clips, and all independent child playheads.
- Script-driven goto/play/stop/label paths beyond the one hooked goto entry and its synchronous same-thread nesting.
- Any direct timeline-field assignment or alternate advance primitive.
- Exactly-once-per-instance behavior on a tick, callback ordering relative to outer-phase publication, and cross-thread safety.
- Side-effect/return-value equivalence when an original MovieClip or Navigator function is skipped.
- Script/IFBL transition timing outside the specific integer-wait store and repeat globals.
- The broad-looking `visual frame milliseconds`, smoothing, decay, and palette consumers' relationship to Flash/RVB.
- 60/120/non-multiple runtime acceptance and actual displayed-frame sequences.

### Likely gaps

- **Definite test gap:** callback semantics, nested timelines, ActionScript transitions, and content reach have no automated regression.
- **Definite diagnostic gap:** cumulative aggregate counters cannot attribute coverage or detect duplicate/missing per-instance advances; non-multiple tick spacing is not logged.
- **Potential runtime gap:** the boolean gate permits every call on a tick, so repeated visits to one instance in one outer interval can double-advance.
- **Potential reach gap:** goto-depth protection covers one entry and one dynamic same-thread scope; other goto/label/action paths may bypass it.
- **Potential clock-boundary gap:** no source/test invariant proves one `OuterFrame` publication precedes and encloses all relevant 2D callbacks.
- **Potential script gap:** native-rate IFBL callbacks are correct for elapsed seconds, but any unclassified frame-counted counter inside those callbacks remains outside MovieClip/IFBL-wait/Navigator corrections.

### False-positive risks

- `Anim::DrawTraverse` is Flash-like 2D/RVB despite the name `Anim`; it must not be grouped with generic 3D.
- The active bottom-right Navigator is a manual DDS renderer per prior binary evidence; the presence of authored navigator RVBs does not make its current hook a MovieClip hook.
- `CTuneEffect`, `FixedVisualFrameOperand`, stage clip mapping, and gameplay countdown are 2D-looking visuals but are not evidence of Flash menu coverage.
- Menu input-repeat scaling can change the perceived pace or trigger time of a transition without changing autonomous animation speed.
- `visual frame milliseconds`, smoothing, decay, and palette names are not call-graph proof.
- Large MovieClip counter values prove high traffic at one entry, not exhaustive asset or nested-child coverage.
- The old News/Notice gates are historical failure context, not current patches.

## Precise IDA/XFL correlation questions

### For the IDA analyst

1. Does every ordinary automatic MovieClip advance, including each nested child and every root owner, still converge on `0x004DF940` in the current executable? Enumerate any direct frame stores or alternate advance primitives that bypass it.
2. What exact high-level operations reach `0x004DEA30`: numeric goto, label goto, `gotoAndPlay`, `gotoAndStop`, frame actions, initialization, and nested child positioning? Identify any sibling overload/entry that can reach `0x004DF940` outside the hooked dynamic scope.
3. Does `Anim::DrawTraverse` visit each live MovieClip instance exactly once per outer interval, or can multiple roots, passes, masks, caching, or reentrancy call the same instance more than once?
4. Does `0x00458B70` execute exactly once per presented frame and before all MovieClip and Navigator advances? Are these paths on one thread, or can callbacks straddle a subsequent atomic phase publication?
5. At `0x004DF940`, is literal return `1` the complete successful no-op contract, and are there original side effects other than one timeline-frame advance that must still occur on non-ticks?
6. Reconfirm `0x005B6310` call count, sole-caller status, return contract, cross-scene owners, and separation from native-rate draw after the current executable/worktree baseline.
7. Do Flash/RVB paths consume `0x006F4604`, `0x006E8F00`, `0x006E8F04`, either palette normalizer, or another target-scaled global that could independently accelerate/slow a menu animation?
8. Is `0x006309D4` the universal IFBL integer-wait store, and do all News/Notice/other elapsed callbacks remain native-rate with no alternate task gate? Identify any frame-counted script state outside this wait store and the removed loop-count site.
9. Are there script/player APIs that set playhead/frame fields directly without calling the hooked goto/advance entries?

### For XFL correlation

1. Which assets contain nested MovieClips with independent playheads, and which parent/child symbols are expected to advance concurrently versus remain stopped?
2. Which timelines/actions use numeric or label `gotoAndPlay`, `gotoAndStop`, `play`, or `stop`, including frame actions that recursively reposition children?
3. Which assets have multiple active roots/instances or reusable symbols that could expose more than one advance call per authored tick?
4. Which tag/action families drive transitions through IFBL/host callbacks rather than ordinary MovieClip advance?
5. For representative main and nested timelines across attract, mode select, song select, customization, reward/result, News, and Notice, what exact authored-frame/label sequence should be correlated with runtime hook observations?
