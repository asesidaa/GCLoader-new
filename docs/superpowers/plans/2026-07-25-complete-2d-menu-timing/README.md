# Complete 2D Menu Timing Plan Set

This directory turns the approved
[Complete 2D Menu Timing Fix Design](../../specs/2026-07-25-complete-2d-menu-timing-design.md)
into three separately gated implementation plans.

> **2026-07-26 safety correction:** Ranking and HitChart hook geometry and all
> counter-gate continuation semantics are superseded by the
> [Safe Menu Counter Hooks plan](../2026-07-26-safe-menu-counter-hooks/PLAN.md).
> The original stage plans remain as the historical execution record.

**Implementation baseline:** `817b25d` (`docs: design complete 2D menu timing fixes`)

**Execution mode:** inline in the
`H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing` worktree. Do not
dispatch implementation agents unless the user later changes that instruction.

## Execution Order

| Order | Stage | Plan | Entry gate | Exit gate |
|---:|---|---|---|---|
| 1 | A | [Observe-only diagnostics](stage-a-diagnostics/PLAN.md) | Approved design and clean implementation baseline | Diagnostic DLL is statically verified, separately archived and hashed, user has exercised the supplied matrix, and the resulting log is recorded and interpreted |
| 2 | B | [Corrections with diagnostics retained](stage-b-corrections/PLAN.md) | Stage A runtime evidence is append-only in the validation record | Corrected DLL is statically verified, separately archived and hashed, and the user has supplied an explicit visual/timing/input verdict |
| 3 | C | [Post-acceptance cleanup](stage-c-cleanup/PLAN.md) | The user explicitly authorizes cleanup after accepting Stage B behavior | Temporary diagnostics alone are removed, final static verification passes, and the user completes a final smoke test |

Stage C is not ordinary follow-on work. It is a destructive cleanup gate.
Nothing listed for removal in Stage C may be removed, disabled, folded away,
or made unreachable during Stages A or B.

## Shared Invariants

- Modify source only in
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`.
- Treat `H:\gc` as the live runtime/evidence tree. Do not modify
  `game471.exe`, `game471.exe.i64`, XFL, RVB, MTX, DDS, PNG, or game-data
  content.
- The executable identity is
  `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.
  The IDB identity is
  `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F`.
- Preserve the current `Authored60PhaseClock` as the only authored-cadence
  publisher. Do not add a second clock or accumulator.
- Keep Observe/Correct selection internal to separately hashed staged builds.
  Do not add a public configuration key or ConfigGUI surface.
- Keep goto operations and preprocessing semantic movement immediate. Gate
  only ordinary recurring MovieClip advances.
- Keep Ranking, HitChart, and UnlockReward callbacks running at the target
  update rate. Gate only the five proven terminal raw stores.
- Do not gate News, Notice, `Anim::DrawTraverse`, an entire draw callback, an
  entire IFBL callback, rendering, input, or elapsed-seconds work.
- Same-object MovieClip revisit data is observation only. Never use it to skip,
  merge, or deduplicate a call.
- Best-effort diagnostic reads never affect a behavior decision and never use
  the fatal conversion path.
- Every new hook is covered by an exact expected-byte contract, a non-null
  runtime binding, preflight, full-capacity rollback, and an exact order test.
- `NavigatorAdvance` remains immediately before `OuterFrame`; `OuterFrame`
  remains the final full transformed contract.
- Use both `msvc32-debug` and `msvc32-release` presets. A passing build is
  static evidence, not in-game acceptance.
- Do not overwrite the live DLL without explicit user authorization and a
  stopped game. Preserve every staged DLL and every pre-deployment live DLL in
  additive runtime archives; do not delete those archives during this plan
  set.
- Do not truncate, delete, or replace earlier runtime evidence. The validation
  record is append-only.

## Hook Counts and Ordering

The existing optional WASAPI resync policy is independent of menu timing and
must remain unchanged. Therefore `FramerateHookContracts(false)` still exposes
only `OuterFrame`, while `BuildFramerateHookPlan(false, true)` may contain the
already-committed WASAPI policy plus `OuterFrame`.

| Contract fact | Stages A/B | Stage C |
|---|---:|---:|
| `kMaximumFramerateHooks` | 53 | 52 |
| Full transformed contract view | 53 | 52 |
| Transformed plan, WASAPI excluded | 52 | 51 |
| Transformed plan, WASAPI committed | 53 | 52 |
| Native plan, WASAPI excluded | 1 | 1 |
| Native plan, WASAPI committed | 2 | 2 |
| Permanent menu hooks | 6 | 6 |
| Temporary `MovieClipStopDiagnostic` | 1 | 0 |
| `NavigatorAdvance` index in full view | 51 | 50 |
| `OuterFrame` index in full view | 52 | 51 |

No new menu ID may appear in either native plan.

## Exact Menu Hook Set

| ID | RVA | Expected bytes | Kind | Lifetime |
|---|---:|---|---|---|
| `MovieClipPreprocessVisit` | `0x000EFB90` | `6A FF 68 10 49 67 00` | Inline | Permanent |
| `MovieClipStopDiagnostic` | `0x000D1730` | `C7 81 1C 01 00 00 01 00 00 00 C3` | Inline | Stages A/B only |
| `RankingEntryCounterStore` | `0x00216EB7` | `89 01` | Mid | Permanent |
| `HitChartEntryCounterStore` | `0x00265635` | `89 01` | Mid | Permanent |
| `UnlockRewardCountdownStore` | `0x00030DA3` | `89 90 6C 37 00 00` | Mid | Permanent |
| `UnlockRewardPrimaryStateStore` | `0x00030E54` | `89 81 D4 37 00 00` | Mid | Permanent |
| `UnlockRewardSecondaryStateStore` | `0x00030F23` | `89 90 D4 37 00 00` | Mid | Permanent |

The five store callbacks must preserve every general-purpose register and
`EFLAGS`. In Correct mode they change only `EIP`, by exactly two bytes for
Ranking/HitChart or six bytes for the three UnlockReward stores.

## Durable Runtime Artifacts

The source-controlled append-only record is:

`docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

The additive runtime archive root is:

`H:\gc\artifacts\runtime-builds\2d-menu-timing`

Each staged build receives its own directory:

```text
stage-a-observe\<dll-sha256>\iDmacDrv32.dll
stage-b-correct\<dll-sha256>\iDmacDrv32.dll
stage-c-final\<dll-sha256>\iDmacDrv32.dll
pre-deploy\<timestamp>\iDmacDrv32.dll
```

Do not replace a prior staged binary with a later build. Record the archive
path, size, timestamp, SHA-256, source commit, executable identity, and IDB
identity before deployment.

## Runtime Ownership Boundary

Codex owns:

- implementation;
- automated tests;
- exact contract/count/order verification;
- debug and release builds;
- staged DLL hashing and archival;
- deployment preparation after authorization;
- log extraction and interpretation; and
- append-only evidence updates.

The user owns:

- starting and controlling the game;
- selecting target and external-cap FPS;
- exercising the concrete screens;
- visual timing comparison;
- input/responsiveness judgment; and
- the explicit authorization that unlocks Stage C.

A counter proves that a hook path executed. It does not prove that the screen
looked correct.

## Required Runtime Matrix

| Path | Concrete exercise |
|---|---|
| Ranking | Wait through attract rotation until `CRankingTask` / `ranking.rvb`; inspect `tg_rank01..30` row slide/fade |
| HitChart | Wait through attract rotation until `CHitChartTask` / `hitchart.rvb`; inspect `tg_01..30` entry slide/fade |
| UnlockReward | Complete a credit with an eligible profile reward and enter `CUnlockRewardTask` / `unlock_reward.rvb`; inspect panels, arrows, text, item/coin presentation |
| PreProcessor | Restart at each tested FPS, then traverse Select Mode, Select Game, Select Music, Results, and Unlock Reward |
| Revisit stress | Exercise `selectmode2`, `selectgame2`, `selectmusic2`, `result_local`, and `unlock_reward` |
| Navigator control | Exercise the bottom-right Navigator in Select Mode, Select Music, and Results |
| News/Notice control | Observe boot/legal/news wall time and ordinary input responsiveness |

`H:\gc\data\expconfig.cfg` currently has
`DoNotDisplayRanking = 0`, `DoNotDisplayHitChart = 0`, and
`ForceSkipReward = 0`. Re-read and record those values for each runtime stage;
do not silently assume they remained unchanged.

## Pause Rules

- Pause after the Stage A deployment instructions and wait for the user-run
  log. Do not infer runtime evidence locally.
- If a Stage A path has a zero activation count, record it as unexercised.
  Repeat only the missing path or target; do not call it safe.
- Pause after the Stage B deployment instructions and wait for the user's
  visual/timing/input verdict.
- Do not begin Stage C because counters look plausible. Begin it only after an
  explicit user statement accepting the corrected build and authorizing
  cleanup.
- If a Stage B result fails, retain the entire diagnostic build and use
  systematic debugging. Do not weaken tests or remove probes.
- Untested targets and unreproduced paths remain explicitly unaccepted in the
  final record.

## Planned Commit Sequence

| Stage | Commit |
|---|---|
| A | `test: define menu timing diagnostic policy` |
| A | `feat: add checked menu timing hook contracts` |
| A | `feat: observe missing 2D menu timing paths` |
| A | `docs: start 2D menu timing runtime validation` |
| A | `docs: record Stage A menu diagnostic build` |
| A | `test: record Stage A menu timing diagnostics` |
| B | `fix: correct remaining 2D menu timing paths` |
| B | `docs: record Stage B corrected menu build` |
| B | `test: record Stage B menu timing acceptance` |
| C | `refactor: remove accepted menu stop diagnostic hook` |
| C | `refactor: trim accepted menu timing diagnostics` |
| C | `docs: record final menu timing build` |
| C | `test: record final menu timing smoke test` |

Do not create an empty commit when a checkpoint has no source-controlled
change. Runtime-evidence commits occur only after the user supplies the
corresponding result.
