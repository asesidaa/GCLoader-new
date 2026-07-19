# E-035 - 240 FPS operator acceptance

- Date: 2026-07-20
- Evidence type: operator runtime observation
- Runtime DLL SHA256: `3EA2BF5238E1F9795EC99B91AA8EF1531D80740C0DB7ADD61B574CC11BB9628E`

## Observation

After testing the production DLL containing the 0/1-preserving IFBL wait
correction, the operator reported that the game "seems fine now" and requested
that the changes be committed and merged.

## Scope

This is runtime acceptance of the currently deployed correction, including the
previously blocking 240 FPS behavior. It is not a timestamped or instrumented
60/120/144/240 acceptance matrix, and no such quantitative evidence is inferred
from the report.

## Result

The IFBL polling-yield correction is accepted for integration. Temporary probes
remain removed; no additional diagnostic instrumentation is required.
