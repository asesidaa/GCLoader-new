# Investigation workspace

This folder preserves the high-FPS timing investigation across context resets.
The canonical GSD debug state is the sibling file
`../high-fps-timing-domains.md`.

## Files

- `CONTEXT.md`: stable scope, constraints, reproduction, and target artifacts.
- `FINDINGS.md`: verified facts only, with source and confidence.
- `HYPOTHESES.md`: live hypotheses, disproof tests, and status.
- `RESULTS.md`: conclusions, proposed architecture, and validation outcomes.
- `CLASS-AWARE-AUDIT-TODO.md`: completed native audit checklist and evidence
  inventory.
- `CLASS-AWARE-ANNOTATION-LEDGER.md`: persisted IDA rename/comment history and
  final readback record.
- `evidence/INDEX.md`: index of raw IDA, source, log, and debugger artifacts.
- `traces/INDEX.md`: subsystem call/data-flow traces.

## Evidence rules

1. Keep binary facts, source facts, runtime observations, and inference distinct.
2. Record executable addresses as both IDA EA and image-relative RVA.
3. Label runtime captures with configured FPS and build/DLL hash.
4. Do not promote a hypothesis to a finding until its disproof test has been run.
5. Do not edit production patch code until `RESULTS.md` contains an accepted design.

The native input/judgement phase is closed by E-046. Loader source review is a
separate design phase and must start from E-046 rather than E-043's historical
raw-ID classification.
