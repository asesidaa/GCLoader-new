# 2D Menu Timing Runtime Validation

## Immutable Inputs

| Input | Identity |
|---|---|
| `H:\gc\game471.exe` | SHA-256 `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |
| `H:\gc\game471.exe.i64` | SHA-256 `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F` |
| Source worktree | `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing` |

## Evidence Rules

- Entries are append-only.
- Static, log, visual, timing, and input verdicts are recorded separately.
- Zero activation means unexercised, not safe.
- Untested FPS targets and unreproduced screens remain unaccepted.
- Only the user supplies visual/timing/input acceptance.

## Stage A — Observe-Only Diagnostics

### Build identity

Status: build not yet produced.

### Reproduction configuration

Read from `H:\gc\data\expconfig.cfg` without modification:

- `DoNotDisplayRanking = 0`
- `DoNotDisplayHitChart = 0`
- `ForceSkipReward = 0`

Ranking, HitChart, and Unlock Reward are configured as available. Runtime
activation has not yet been demonstrated.

### Deployment

Status: not yet authorized.

### Runtime exercises

Status: user run not yet performed.

### Codex interpretation

Status: no runtime log has been supplied.

### User verdict

Stage A is diagnostic-only and carries no fix verdict.

## Stage B — Corrected with Diagnostics Retained

Status: gated on completed Stage A evidence.

## Stage C — Accepted Diagnostic Cleanup

Status: gated on explicit user acceptance of Stage B.
