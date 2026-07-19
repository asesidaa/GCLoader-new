---
status: design_pending_review
trigger: "The crash is gone, but at 240 FPS transitions are prolonged, card-scan/menu input is hard to register, in-game animation timing remains broken, and non-song-selection menus scroll too quickly. Determine which systems are absolute-time versus tick-based and redesign the high-FPS timing patch from binary evidence."
created: 2026-07-19
updated: 2026-07-19
mode: diagnose-only
---

# High-FPS timing domains

## Symptoms

- Expected: judgement behavior retains its 60 FPS real-time semantics.
- Expected: 2D animation, menu navigation, and transitions retain their 60 FPS real-time duration at higher render rates.
- Expected: stage/chart rendering and input sampling become smoother at higher render rates.
- Actual at 240 FPS: intervals between transitions are prolonged.
- Actual at 240 FPS: input on the card-scan result/menu is difficult to register and feels dropped.
- Actual at 240 FPS: in-game animation timing remains incorrect.
- Actual at 240 FPS: menu scrolling, especially outside song selection, is accelerated.
- Error state: the former loading crash is gone after relocating `EffectCadence16B`.
- Reproduction: configure 240 FPS, then exercise card-scan results, non-song-selection menus, transitions, and an in-game stage.

## Current Focus

- hypothesis: The current regressions come from crossing four distinct domains: absolute elapsed time, target-rate simulation ticks, authored 60 Hz asset frames, and control-flow counts.
- test: Completed static traces from the native scheduler through Notice/News, IFBL, input repeat, judgement, gameplay effects, player-position state, and stage/chart rendering.
- expecting: Removing complete-task gates and converting only at explicit integer-duration or authored-asset sinks will preserve 60 FPS wall-time behavior while retaining target-rate judgement/chart/input updates.
- next_action: Review the correction architecture in `high-fps-timing-domains/RESULTS.md`; do not edit production code until the design is accepted.
- reasoning_checkpoint: Transition, menu-repeat, judgement, GREAT/GOOD, direct-effect, IFBL-loop, player-position, and stage domains are statically classified. Card-result edge loss still requires a fresh 240 FPS action/edge trace before changing input semantics.
- tdd_checkpoint: Not applicable during diagnose-only work; implementation tests begin only after design approval.

## Evidence

- timestamp: 2026-07-19
  source: user runtime report
  observation: The relocated hook removed the loading crash, but 240 FPS behavior is worse across transitions, input, animation, and menu navigation.
  implication: The crash and the timing correctness problem were independent; safe hook placement did not validate hook semantics.

- timestamp: 2026-07-19
  source: `H:\gc\config.toml` and `H:\gc\loader-log.txt` inspected after the report
  observation: The presently available runtime capture is a 60 FPS run (`target_fps=60`), not the reported 240 FPS reproduction.
  implication: It can establish baseline startup/input state but cannot validate or refute any 240 FPS timing hypothesis.

- timestamp: 2026-07-19
  source: current GCLoader source
  observation: Transformed timing currently combines direct constant replacement, duration scaling, authored-frame mapping, and a QPC-derived shared 60 Hz boolean gate.
  implication: Every hook must be checked against the original variable semantics and actual caller cadence; common naming is not evidence of a common clock domain.

- timestamp: 2026-07-19
  source: live IDA daemon on `H:\gc\game471.exe.i64`
  observation: The legal notice owns a 2.0-second delta-time IFBL wait but the loader calls its complete update only at 60 Hz; at 240 FPS this predicts an 8-second wait.
  implication: Notice and News must remain native-rate tasks; only their frame-counted MovieClip/IFBL operations may be adapted.

- timestamp: 2026-07-19
  source: live IDA judgement/effect traces
  observation: Judgement compares milliseconds correctly at target rate, while GREAT/GOOD and several direct effect writers use the target 4.1667 ms frame size as an authored asset-frame duration.
  implication: Retain the target gameplay clock and restore 16.6667 ms only at proven asset-frame lifetime/index sinks.

- timestamp: 2026-07-19
  source: live IDA IFBL/menu traces
  observation: The non-song repeat helper still uses 16/3 native ticks, and the IFBL loop hook scales a repetition count rather than a wait.
  implication: Scale the repeat thresholds, retain IFBL integer-wait scaling, and remove loop-count scaling.

## Eliminated

- hypothesis: The loading crash explains the remaining high-FPS behavior.
  reason: The user confirms the crash is gone while all timing/input symptoms remain.

## Resolution

- root_cause: Mixed-clock patching: complete elapsed-time tasks are throttled, target-frame milliseconds leak into authored asset frames, native-duration thresholds remain unscaled, and an IFBL control-flow count is scaled as time.
- design: Keep absolute-time and gameplay simulation native; scale only integer durations; map only final authored asset indices/cadences; never scale control-flow cardinality. Exact hook decisions and sites are in `high-fps-timing-domains/RESULTS.md`.
- verification: Static IDA/source diagnosis and production implementation are complete. The operator accepted the deployed 240 FPS correction on 2026-07-20 and requested commit and merge; this does not imply a quantitative four-rate matrix was captured.
- files_changed: Production framerate source/tests and the persistent investigation documents under `.planning/debug/high-fps-timing-domains*`.

## Companion artifacts

See `.planning/debug/high-fps-timing-domains/README.md`.
