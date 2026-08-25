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

## Shared navigator follow-up

The final reported 240 FPS defect is the bottom-right navigator character,
first observed in song selection and also present in mode selection and other
menus. E-036 records two rejected timeline candidates. E-037 proves the actual
target: global renderer `dword_7F2524` draws per-navigator
`base.dds`/`face.dds` layers, and its manual state routine `sub_5B6310` runs
once per render callback. E-038 maps `CSelectGameTask`, `CSelectMusicTask`, and
eight additional tasks to that same callback. Do not modify the shared
MovieClip gate or patch individual scenes. The narrow design is to keep
drawing at native cadence and execute only the shared `sub_5B6310` state
advance on authored-60-Hz ticks.

E-039 records implementation of that exact design through the normal
`msvc32-release` preset and deployment to `H:\gc\iDmacDrv32.dll`. Static,
build, and focused-test verification are complete. High-FPS operator
acceptance of navigator timing across mode selection, song selection, and the
other mapped scenes is the remaining step.

## Class-aware gameplay audit boundary (2026-08-17)

The persisted class-aware audit is E-042. It reuses the completed RTTI dump,
class/method index, physical-input trace, and batched note-handler traces; it
does not rerun those scans. The proven path is:

`GWInputDeviceXioFio_BOOST` → `GWInputXio` → gameplay capture `0x659920` →
`CBooster` history/query → `CGameMainTask`/`CDemoPlayTask` →
`CTuneGameManager_RunGameplayFrameStateMachine` (`0x6630B0`) →
`CTuneGameManager_ProcessJudgementFrame` (`0x6401E0`) → judgement core
(`0x5D68E0`).

The current binary operand at `0x6401EF` is the 60-FPS `16.666666 ms/frame`
constant, not an 8.333 ms/120-Hz clock. `GameTimeOffset` and
`JudgTimeOffset` remain the only variable timing settings for the proposed
design; other judgement settings are treated as static. E-046 corrects E-042's
initial raw-type matrix with the complete raw/canonical/effective mapping and
progression order. E-044 closes the former score-publication caveat. Static IDA
evidence remains separate from gameplay acceptance.

## Native catch-up and loader-scope audit (2026-08-17)

E-043 extends E-042 through the native catch-up boundary. At `0x6630B0`, input
history is filled at `0x664DDC`, judgement runs at `0x664E06`, and the
authoritative frame is committed at `0x664E23`. The manager processor at
`0x6401E0` loops from `m = 1` through `Tune+0x14`, projects each frame to an
integer recognition millisecond value, and calls `0x5D68E0`; skipped history
entries inherit held state and are not fresh physical polls. The current
catch-up limit is `floor(50 ms * target_fps)`, yielding maxima of 3/7/8/12
native judgement calls at 60/144/165/240 FPS.

The QPC transition journal is drained once into a sample for each native core
call. `JudgementInputScope` spans the entire `0x5D68E0` call, including both
booster-component passes, lifecycle/aggregation, and post-descriptor free
input. E-045 proves that native pressed queries intentionally do not consume
edges; both components must observe one stable frame fact. E-043 records a
historical loader source risk around note-dependent recomputation, but current
source compliance remains deliberately unaudited until the design phase. This
is not a runtime claim and does not authorize production changes.

## Native result-publication closure (2026-08-17)

E-044 proves the per-player state split at `CTuneGameManager+0x254`
(`GameplayJudgementState`, 444 bytes) and `+0x26C` (`GameplayScoreState`, 368
bytes, non-polymorphic). The judgement state computes timing/duration grades
and aggregates component results. The score state consumes the resolved grade
using the same integer `recognition_ms` and owns the MISS/GOOD/COOL/GREAT
counters at offsets `+120/+124/+128/+132`. `0x5D0820` publishes separate
note/effect metadata.

Chart types `7/8` are native `HIDDEN/HIDDEN2` hidden notes routed through the
normal chart-note grade/score path. `0x5D2040` is separate post-descriptor free
input and emits a free-input effect without becoming a chart descriptor
result. Descriptor `IsMute` is an independent field. E-046 completes the
remaining normalization and progression proof; no loader defect is declared
solely from this native evidence.

## Native normalization and progression closure (2026-08-17)

Chart preparation first bakes raw chart types into canonical and effective
descriptor types. The recognition-time pipeline is then:

`physical switch state` → `GWInput/CBooster frame history` → `native catch-up
recognition step` → `first incomplete effective descriptor per eligible
internal chart row` → `effective-family pressed/held judgement` → `long-note
continuation/finalization` → `grade/result/score/effect publication` →
`guarded post-descriptor free input` → `next-step row eligibility refresh`.

E-046 proves raw aliases `B→A`, `C/E→9`, and `D→4`; effective `0` is skipped.
Candidate lists are fixed before each component pass and exclude same-row
followers. A later catch-up step can expose the next descriptor but cannot
replay the earlier pressed edge because skipped native history carries held
state only. Free input runs before `0x5D58D0` refreshes the row gate at `+0xCC`;
that helper is not a cursor advance.

The design-phase invariants are therefore native facts rather than proposed
policy: one immutable full input view per recognition step; no per-descriptor
edge consumption; no same-row or catch-up edge synthesis; effective-family
routing with canonical identity retained where requested; original candidate,
conflict, long-note, grade, and free-input rules left in native ownership; and
only `GameTimeOffset`/`JudgTimeOffset` treated as variable settings. The final
saved IDB SHA-256 is
`3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163`.
