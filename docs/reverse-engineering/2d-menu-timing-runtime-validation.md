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

Static evidence appended on 2026-07-25; the initial status above is retained
as the historical starting state and is superseded by this entry:

- Source commit:
  `99b42a6412fae0850886806283581498887a0b58`
- Release candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing\build-msvc32-release\dist\iDmacDrv32.dll`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F\iDmacDrv32.dll`
- Size: `5,656,064` bytes
- Last write time (UTC): `2026-07-25T14:59:47.1836121Z`
- SHA-256:
  `4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F`
- Architecture: PE `14C machine (x86)`, 32-bit word machine
- Debug gate: complete build and `57/57` CTest tests passed
- RelWithDebInfo gate: complete build and `57/57` CTest tests passed
- Candidate/archive hash comparison: exact match

This is static evidence only. No live DLL was copied and no gameplay run was
performed.

### Reproduction configuration

Read from `H:\gc\data\expconfig.cfg` without modification:

- `DoNotDisplayRanking = 0`
- `DoNotDisplayHitChart = 0`
- `ForceSkipReward = 0`

Ranking, HitChart, and Unlock Reward are configured as available. Runtime
activation has not yet been demonstrated.

### Deployment

Status: not yet authorized.

Deployment evidence appended on 2026-07-26; the initial status above is
retained as historical context and is superseded by this entry:

- User authorization: explicit live deployment authorization received
- Deployment time: `2026-07-26T02:33:25.8779529+08:00`
  (`2026-07-25T18:33:25.8809714Z`)
- Live path: `H:\gc\iDmacDrv32.dll`
- Source archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F\iDmacDrv32.dll`
- Live size: `5,656,064` bytes
- Live SHA-256:
  `4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F`
- Pre-deployment process check: no `game471` process and no process with the
  live DLL loaded
- Post-copy verification: archive and live SHA-256 values match exactly
- Preservation: the existing DLL and loader log were not preserved, per the
  user's explicit direction

Status: observe-only Stage A DLL deployed; user diagnostic run pending.

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
