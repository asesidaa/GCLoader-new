# Stable context

## Objective

Determine how `game471.exe` partitions wall-clock time, simulation updates,
render frames, authored 60 Hz animation ticks, and input sampling. Use that
model to explain the 240 FPS regressions and design a coherent correction.

## Scope

- Main update scheduling and frame pacing.
- Flash-like/2D animation and transition advancement.
- Menu navigation, held-input repeat, and edge consumption.
- Gameplay judgement and chart-time progression.
- Gameplay effects, player-position visuals, stage rendering, and clip masks.
- The current GCLoader framerate hooks and direct timing patches.

## Constraints

- Diagnose before patching.
- Use the existing IDA-CLI daemon attached to `H:\gc\game471.exe.i64`.
- Treat `H:\gc` as runtime evidence and `H:\gc\artifacts\GCLoader` as source.
- Restrict binary-database work to read-only analysis unless persistence is
  explicitly approved.
- Preserve the current branch and unrelated untracked files.

## Current reproduction report

- Target: 240 FPS.
- Loading crash: resolved.
- Transitions: prolonged intervals.
- Concrete transition: the legal-information notice in `signature` takes
  roughly 2x or more too long before the TAITO logo at high FPS.
- Card-scan result/menu input: presses feel dropped or difficult to register.
- Card-selection confirmation: its visible countdown is approximately 4x
  slower at 240 FPS. The relevant visual states are believed to be in
  `H:\gc\artifacts\2d_boost\start2_xfl`.
- In-game animation: timing still incorrect.
- Menu navigation: accelerated, especially outside song selection.
- Desired invariant: 60 FPS real-time behavior with smoother rendering and
  input opportunity at higher FPS.

## Targets

- IDA database: `H:\gc\game471.exe.i64`
- Executable image base: `0x00400000`
- Source: `H:\gc\artifacts\GCLoader`
- Runtime config: `H:\gc\config.toml`
- Runtime log: `H:\gc\loader-log.txt`

## Prior-analysis baseline

`H:\gc\artifacts\framerate_120fps` is the May 2026 120 FPS investigation
archive. Its useful architectural claims must be revalidated against the
current binary and source before reuse. In particular, the archive already
separates native-rate update/input from authored 60 Hz UI cadence and warns
against generic update gating, but several files describe superseded
experiments and 120-only constants.

`H:\gc\artifacts\2d_boost\signature_xfl` is the dumped XFL for the startup
signature scene. Its labels match the live `CNoticeTask` and `CSignatureTask`
IFBL descriptors and provide authored-timeline context.

`H:\gc\artifacts\2d_boost\start2_xfl` is the dumped card/start UI. Its
`imc_card_01` labels correlate with the native `CStartTask` `SELECT_NOCARD*`
descriptor family. `sub_5A4540` owns its decision input; the sequence's
absolute countdown is serviced separately by `sub_5A3AC0`. The old
`0x005A5E80` page mapping is invalid.

## Current shared-2D audit boundary

`Anim::DrawTraverse` owns automatic MovieClip frame advancement for all
MovieClip instances, while CSeqTask/IFBL callbacks own a separate family of
elapsed-seconds timers and input decisions. The current IDB contains 21 unique
2D opcode-1 callbacks that consume the global delta. Do not change the global
delta, opcode-1 cadence, a complete sequence task, or the shared MovieClip
callee until runtime evidence identifies which boundary has the wrong
calls-per-second versus delta-per-call relationship. See E-027.

## Current deployed diagnostic

The E-028 opcode-1 diagnostic remains in the currently deployed DLL but has
completed its purpose. A full 240 FPS session showed native cadence for its
sampled callbacks and proved it cannot observe the opcode-`0x27`
`SELECT_NOCARD*` page. Do not add further probes; remove diagnostics from the
next production build.

## Mandatory login correction

E-029 supersedes the earlier `0x005A5E80` screen mapping. The broken mandatory
login page is the `SELECT_NOCARD*` descriptor family, and its confirmed entry
callback is `sub_5A4540`. Continue IDA analysis from this sequence; do not use
the old card-confirm probe as evidence for this page.

E-030 proves that this page's timer is an absolute countdown whose remaining
value is reduced by the global elapsed delta from `sub_5A3AC0`. E-031 proves
that CStartTask is dispatched through the normal native task traversal; there
is no CStartTask-specific 60 Hz scheduler gate. Continue through the exact
`SELECT_NOCARD` descriptor loop and its timer/display callbacks.

E-032 establishes the actual login-page root cause. Its descriptor loop uses
an opcode-`0x11` wait of 1 solely to yield until the next polling update. The
current global `IfblWait` hook scales that 1 to 4 at 240 FPS, so the absolute
timer callback and one-update input checks run only once per four updates.
The next production rule must preserve one-update polling yields while
retaining authored-duration treatment only where the descriptor semantics
actually require it.

E-033 completes that classification for the executable: all 22 value-1 waits
are polling-loop yields, while the only two positive waits above 1 are
authored 15-frame card-name animation pauses. The production hook rule is now
fixed: preserve 0/1 and scale only values greater than 1.

E-034 records the implemented and deployed production fix. The active runtime
DLL SHA256 is
`3EA2BF5238E1F9795EC99B91AA8EF1531D80740C0DB7ADD61B574CC11BB9628E`;
it contains no temporary probes. Runtime acceptance at 240 FPS is the only
remaining step for this correction.

E-035 records the operator's 240 FPS runtime acceptance of that exact deployed
DLL and the request to commit and merge. This closes the blocking correction
for integration; it does not claim an independently measured four-rate matrix.
