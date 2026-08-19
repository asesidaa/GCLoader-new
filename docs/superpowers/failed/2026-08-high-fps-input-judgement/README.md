# Failed High-FPS Input/Judgement Attempts

> **NEGATIVE EVIDENCE ONLY.** None of the linked designs or plans is approved
> for implementation. Do not execute them, copy their architecture, or treat a
> former green build/test result as evidence that their behavior was correct.

## Rollback boundary

- Rollback date: 2026-08-20.
- Clean code baseline: commit `800b619` (`Document ASIO runtime diagnostic
  interpretation`), the final ASIO commit immediately before the first
  high-FPS input-attempt document.
- Failed implementation head retained in Git history/reflog for provenance:
  `a71afbe`.
- All post-baseline input/judgement implementation, hook wiring, transition
  journal code, and framerate-domain changes were removed.
- The unit-test suite remains intentionally removed. Its deletion was not
  collateral damage from the rollback.

The files remain at their original paths so historical cross-references and
evidence pointers do not break. Their banners and this index define their
status.

## Ground truth and proof policy

The supported game binary, its IDB/disassembly, and observed game behavior are
the ground truth. Loader-side emulations and expected values invented from the
same design as the implementation are not independent evidence.

No automated test may be added unless every asserted expectation is formally
and strictly derived from verified binary/ABI/control-flow facts, a proven
mathematical identity with verified assumptions, actual recorded game
behavior, or an external protocol authority. TDD ceremony is not a reason to
create a test. Build success proves compilation only; game acceptance requires
the actual executable.

## Retained authoritative evidence

- Completed native audit:
  `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`
  (especially E-042 through E-046). Do not repeat this audit without a
  concrete, recorded gap.
- [Input-to-judgement pipeline](../../../reverse-engineering/high-fps-input-judgement-pipeline.md):
  native static pipeline evidence, not a loader design.
- [Hook manifest](../../../reverse-engineering/high-fps-input-judgement-hook-manifest.md):
  historical binary/hook inventory; the rollback does not install those
  high-FPS judgement hooks.
- [Failure index](../../../reverse-engineering/high-fps-absolute-time-redesign-failure-index.md):
  chronology and failure evidence.
- [Runtime failure diagnosis](../../specs/2026-08-19-absolute-time-judgement-driver-runtime-failure-diagnosis.md):
  diagnosis of the final failed implementation using `H:\gc\loader-log.txt`.

## Historical diagnostic work

These documents describe removed diagnostic instrumentation. They may explain
how evidence was gathered, but they are not current implementation plans:

- [Input edge diagnostics design](../../specs/2026-08-09-high-fps-input-edge-diagnostics-design.md)
- [Input edge diagnostics plan](../../plans/2026-08-09-high-fps-input-edge-diagnostics.md)
- [Historical decision record](../../../reverse-engineering/high-fps-input-judgement-decisions.md)

## Failed implementation chains

Each chain produced additional complexity without establishing correct game
behavior:

1. Exact-frame transition bridge:
   [design](../../specs/2026-08-10-high-fps-input-transition-bridge-design.md),
   [plan](../../plans/2026-08-10-high-fps-input-transition-bridge.md).
2. Transaction/late-gate/one-shot lifetime chain:
   [transaction design](../../specs/2026-08-15-high-fps-input-judgement-transactions-design.md),
   [transaction plan](../../plans/2026-08-15-high-fps-input-judgement-transactions.md),
   [late-gate design](../../specs/2026-08-15-high-fps-late-gate-preview-correction-design.md),
   [late-gate plan](../../plans/2026-08-15-high-fps-late-gate-preview-correction.md),
   [one-shot correction](../../specs/2026-08-15-high-fps-one-shot-input-lifetime-correction-design.md).
3. Song-timed/authoritative query-composition chain:
   [song-timed design](../../specs/2026-08-16-high-fps-song-timed-input-judgement-design.md),
   [song-timed plan](../../plans/2026-08-16-high-fps-song-timed-input-judgement.md),
   [authoritative design](../../specs/2026-08-16-high-fps-authoritative-input-judgement-correction-design.md),
   [authoritative plan](../../plans/2026-08-16-high-fps-authoritative-input-judgement-correction.md).
4. Absolute-time judgement driver:
   [design](../../specs/2026-08-19-absolute-time-judgement-driver-design.md),
   [plan](../../plans/2026-08-19-absolute-time-judgement-driver.md), and its
   historical `.tasks.json` ledger. This implementation reached `a71afbe` and
   produced a run in which no input registered and no judgement was made.

The later
[native-cadence redesign draft](../../specs/2026-08-19-absolute-time-native-cadence-judgement-design.md)
was never implemented or finally approved. It is also abandoned because its
review inherited assumptions from the failed driver.

## Confirmed non-repeat findings

- Do not preserve old code merely because it exists; implementation starts
  from the post-ASIO baseline.
- Do not let an input/judgement patch redefine the existing high-FPS visual or
  shared `Tune` clock by inheritance. The failed driver changed
  `GameplaySongClock::Create(target_fps, 1)` to `Create(60, 1)` and then added
  `0x63FA0C` as compensation for the stepping that change created. That
  coupling was introduced by the failed design; it is not a proven native
  requirement.
- Do not reproduce native behavior in loader-side tests and then cite those
  tests as proof. Five observed runtime defects passed the former suite.
- Do not treat rounded frame timestamps, implementation-derived fixtures, or
  successful hook installation as proof of judgement correctness.
- Preserve the still-standing product goal only: judgement must use absolute
  time and remain independent of render framerate. All mechanisms and patch
  boundaries must be justified again from the retained native evidence and
  actual runtime behavior.
