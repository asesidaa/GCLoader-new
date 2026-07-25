# Safe Menu Counter Hooks Design

**Status:** Approved for implementation on 2026-07-26

## Context

The Stage A observe-only menu timing DLL crashed when the attract loop entered
the Ranking screen. The crash dump, live patched bytes, and IDA control flow
prove that the callback itself was not reached. The defect is the placement of
the `RankingEntryCounterStore` SafetyHook mid-hook:

```text
00616CAC  jl  loc_616EB9
...
00616EB4  mov ecx,[ebp-20h]
00616EB7  mov [ecx],eax
00616EB9  jmp loc_616C88
```

The intended instruction at `0x00616EB7` is only two bytes. SafetyHook needs at
least five whole instruction bytes for its x86 detour, so it overwrote the
store and the shared branch target at `0x00616EB9`. The branch then entered the
middle of the installed jump and executed invalid instructions.

IDA shows the same latent defect at the HitChart store:

```text
00665347  jl  loc_665637
...
0066562F  mov ecx,[ebp-94h]
00665635  mov [ecx],eax
00665637  lea ecx,[ebp-5Ch]
```

The full evidence is recorded in
`docs/reverse-engineering/2d-menu-timing-ranking-crash-investigation.md`.

## Decision

Relocate both unsafe mid-hooks to nearby complete, single-entry instruction
windows. Replace relative instruction-length skipping with explicit
continuation addresses for all five raw counter-store gates.

The next deployed DLL remains in `Observe` mode. It validates hook safety and
diagnostic reachability without changing menu timing. The same callback design
must already be safe for the later `Correct` build.

## Goals

1. Remove the proven Ranking crash and the paired HitChart hazard.
2. Preserve byte-for-byte native counter behavior in the next observe-only
   build.
3. Make later suppression resume at a proven original-code continuation rather
   than depend on trampoline-relative `EIP` arithmetic.
4. Make the exact SafetyHook overwrite span part of each affected byte
   contract.
5. Add a regression check for the two external control-flow entries that the
   previous tests missed.
6. Keep all existing logs, dumps, archived DLLs, and validation records.

## Non-goals

- Do not enable Stage B timing correction in the next DLL.
- Do not hook or throttle the complete Ranking or HitChart draw callbacks.
- Do not change the authored 60 Hz phase clock.
- Do not change MovieClip, Navigator, UnlockReward elapsed-time work, input,
  rendering, game data, XFL, RVB, or `game471.exe`.
- Do not introduce a public configuration switch for Observe/Correct mode.
- Do not remove temporary diagnostics before explicit runtime acceptance.

## Alternatives Considered

### Keep the two-byte store addresses with custom detours

A hand-written short jump or breakpoint/code-cave scheme could instrument the
exact stores. This preserves the old callback register assumptions, but adds a
new x86 relocation mechanism, executable scratch-code ownership, and more
version-sensitive assembly. It is disproportionate when safe nearby windows
already exist.

### Hook complete Ranking and HitChart callbacks

Whole-callback hooks avoid short-instruction relocation, but they broaden the
behavioral boundary to loop control, drawing, and callback return semantics.
The timing correction only needs to gate one terminal state write.

### Selected: relocate to safe predecessor windows

The selected windows consist of complete original instructions, satisfy the
minimum detour length, and end before each shared control-flow target. This
keeps the patch narrow and uses SafetyHook's ordinary trampoline machinery.

## Exact Hook Geometry

All addresses below are RVAs relative to the executable base.

| Path | Old hook RVA | New hook RVA | Complete expected bytes | Detour end | Suppress resume RVA |
|---|---:|---:|---|---:|---:|
| Ranking | `0x216EB7` | `0x216EB4` | `8B 4D E0 89 01` | `0x216EB9` | `0x216EB9` |
| HitChart | `0x265635` | `0x26562F` | `8B 8D 6C FF FF FF` | `0x265635` | `0x265637` |
| Unlock countdown | `0x030DA3` | unchanged | `89 90 6C 37 00 00` | `0x030DA9` | `0x030DA9` |
| Unlock primary | `0x030E54` | unchanged | `89 81 D4 37 00 00` | `0x030E5A` | `0x030E5A` |
| Unlock secondary | `0x030F23` | unchanged | `89 90 D4 37 00 00` | `0x030F29` | `0x030F29` |

The Ranking detour covers the pointer load and store. In Observe mode or on an
authored tick, the trampoline executes both and returns at `0x216EB9`. On a
non-authored tick in Correct mode, the callback resumes directly at
`0x216EB9`, skipping both relocated instructions.

The HitChart detour covers only the six-byte pointer load. In Observe mode or
on an authored tick, the trampoline executes the load, returns to the intact
store at `0x265635`, and then reaches `0x265637`. On a non-authored tick in
Correct mode, the callback resumes directly at `0x265637`, skipping both the
relocated load and the intact store.

These continuations are valid without the skipped pointer loads because the
original negative-entry branches already reach the same shared tails without
executing those loads.

## Centralized Site Geometry

The hook binding, byte-contract manifest, and counter descriptor must derive
their hook and continuation RVAs from one set of compile-time geometry
constants. This prevents a future edit from moving the verified contract while
leaving the actual mid-hook binding or suppression target behind.

The complete expected byte pattern is the asserted overwrite span:

- Ranking is exactly two whole instructions totaling five bytes.
- HitChart is exactly one whole instruction totaling six bytes.
- Each UnlockReward hook is exactly one six-byte instruction.

No two-byte expected pattern remains for a SafetyHook mid-hook.

## Control-flow Entry Invariant

For a detour span `[hook_rva, hook_rva + overwrite_length)`, no external branch
may target an address strictly inside that span.

The regression suite records the two IDA-proven witnesses:

| Path | Branch source RVA | Branch target RVA | Required relation |
|---|---:|---:|---|
| Ranking | `0x216CAC` | `0x216EB9` | target equals the new span end |
| HitChart | `0x265347` | `0x265637` | target lies after the new span |

The test must also demonstrate that the same targets fall inside the actual
minimum overwrite spans of the old hook placements. That makes the regression
causal instead of merely asserting new constants.

This is a regression check for the proven incoming edges, not a substitute for
a full disassembler CFG audit. Any future short-instruction mid-hook still
requires an IDA review of the complete relocated span.

## Counter Destination Resolution

At the new hook points, `ECX` has not yet been loaded with the counter pointer.
Diagnostics therefore resolve it without altering the captured context:

- Ranking reads the 32-bit pointer stored at `[EBP-0x20]`.
- HitChart reads the 32-bit pointer stored at `[EBP-0x94]`.
- UnlockReward keeps its existing register-plus-displacement destination
  calculations.

Both the pointer-slot read and the subsequent old-counter read remain
best-effort diagnostic operations. A failed read increments
`diagnostic_read_failures`, emits no unsafe dereference, and does not affect
the gate decision or trampoline behavior.

## Explicit Continuation Semantics

`ApplyMenuCounterStoreGate` accepts the exact absolute suppress-resume address,
not an instruction length.

- `Observe` on any phase leaves the entire context unchanged and returns
  `Commit` or `WouldSuppress` as it does today.
- `Correct` on an authored tick leaves the entire context unchanged and
  returns `Commit`.
- `Correct` on a non-authored tick changes only `EIP`, setting it exactly to
  the supplied original-code continuation, and returns `Suppress`.

It must not add to the trampoline `EIP`. Explicit assignment avoids depending
on SafetyHook's trampoline layout and supports HitChart, where suppression
skips an instruction outside the relocated span.

## Observe-only Runtime Behavior

`ActiveMenuTimingMode()` remains `MenuTimingMode::Observe`.

Consequently:

- Ranking and HitChart execute their original loads and stores on every native
  callback.
- UnlockReward behavior remains unchanged.
- MovieClip behavior remains unchanged.
- would-suppress counters and one-shot samples continue to be collected.
- a successful runtime test proves safe placement and path activation, not
  corrected animation speed.

Expected runtime proof is:

1. the game reaches and leaves Ranking without a crash;
2. `menu_timing_activation path=ranking_entry` appears;
3. Ranking counters become nonzero;
4. the game reaches and leaves HitChart without a crash;
5. `menu_timing_activation path=hitchart_entry` appears;
6. HitChart counters become nonzero; and
7. the final log still reports `menu_timing_mode=observe`.

## Automated Verification

Tests are written before the production change and must initially fail on the
old sites.

Focused tests cover:

- exact Ranking and HitChart hook RVAs and complete expected byte patterns;
- centralized continuation RVAs for all five store gates;
- the two old unsafe spans and two new safe spans against the IDA branch-target
  witnesses;
- exact `EIP` assignment to an arbitrary synthetic continuation;
- no register or flag changes on suppression;
- no context changes for Observe mode or Correct/authored mode;
- frame-local pointer resolution success and safe failure;
- unchanged seven-hook count, plan ordering, capacity, and native-plan
  exclusion; and
- non-null runtime operations for every contract.

Run the focused menu timing test first, then the complete test suites and
builds under both `msvc32-debug` and `msvc32-release`.

## Failure and Deployment Policy

- A byte mismatch or missing operation fails the existing transaction before
  partial installation is accepted.
- Do not deploy a DLL from a failed or partially verified build.
- Do not delete, truncate, rename away, or overwrite the current deployed DLL,
  full-session log, crash dump, or archived evidence while implementing.
- After both configurations pass, archive and hash the new Release DLL.
- Only then replace `H:\gc\iDmacDrv32.dll` with the verified observe-only DLL.
- Preserve the crash-run evidence and append the new build identity to the
  runtime validation ledger.

Runtime testing remains the user's acceptance gate. Static verification cannot
claim that Ranking or HitChart was visually correct or that the crash is gone
in game.
