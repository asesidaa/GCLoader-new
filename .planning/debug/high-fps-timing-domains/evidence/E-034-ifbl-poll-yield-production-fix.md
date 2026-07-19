# E-034: IFBL polling-yield production fix and deployment

## Source change

The production hook at `0x006309D4` now calls
`ScaleIfblIntegerWait(profile, raw_value)`:

- raw 0 remains 0;
- raw 1 remains 1;
- values greater than 1 use the existing checked target-rate duration
  scaling;
- negative signed sentinels continue to survive through
  `ScalePositiveDuration` unchanged.

Both temporary diagnostics were removed: the invalid `0x005A5E80` hook and
the shared opcode-1 callsite hook at `0x00630603`. The transformed production
matrix is again 17 direct writes and 41 hooks.

## Static verification

The x86 RelWithDebInfo build completed for `iDmacDrv32` and
`FramerateAuthoredClockTests`. The focused executable passed with exit code
zero and covers the 240 FPS mapping `0 -> 0`, `1 -> 1`, and `15 -> 60`.

## Deployment

`game471.exe` was not running during deployment.

- Build and deployed DLL SHA256:
  `3EA2BF5238E1F9795EC99B91AA8EF1531D80740C0DB7ADD61B574CC11BB9628E`
- Runtime path: `H:\gc\iDmacDrv32.dll`
- Previous diagnostic backup:
  `H:\gc\deploy-backups\20260720-ifbl-poll-yield-fix\iDmacDrv32.dll`
- Backup SHA256:
  `0B5E0FEDCD1866A1F959E15F62C8F386A5D76312C4F21A04539A44E196F9452D`

Runtime acceptance remains pending. The first check is the mandatory
`SELECT_NOCARD` page at 240 FPS: its countdown must advance at real time and
decision/navigation input must register normally. The same session should
also cover the other E-033 polling families.
