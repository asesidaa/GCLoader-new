# E-028: shared IFBL callback cadence probe

Deployed 2026-07-20 for diagnosis only. No timer, scheduler, register, input,
or callback result is modified.

- Hook: image RVA `0x00230603`, instruction `call [ebp-0x20]`, exact bytes
  `FF 55 E0` in `0x006304B0`.
- Scope: all opcode-1 IFBL callback invocations. The probe buckets the 21
  IDA-proven callback RVAs that directly consume the global elapsed delta.
- Log: `FrameratePatch: ifbl_callback_stats`; each nonzero bucket is
  `label@rva:calls/delta_seconds` for the preceding five-second window.
- 240 FPS interpretation: approximately `300/1.25` for an active single
  callback confirms the suspected 60-Hz-callback plus 1/240-second-delta
  mismatch; approximately `1200/5.0` disproves that mismatch at this boundary.
- DLL: `H:\gc\iDmacDrv32.dll`, SHA256
  `0B5E0FEDCD1866A1F959E15F62C8F386A5D76312C4F21A04539A44E196F9452D`.
- Backup: `H:\gc\deploy-backups\20260720-ifbl-opcode1-probe\iDmacDrv32.dll`,
  SHA256
  `972C08BF181D371C36B154B8EC2CA8AA1860A128BCB0738C0D24377974F7FF1E`.

Runtime result is pending. Do not select a shared timing correction until a
240 FPS session provides these buckets.
