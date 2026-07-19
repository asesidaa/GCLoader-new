# E-029: login card-use path correction

## Why the previous mapping is invalid

The 2026-07-20 full 240 FPS session traversed the mandatory login page that
asks whether to use a card. Across the complete session, both the direct hook
at `0x005A5E80` and the shared IFBL bucket for RVA `0x001A5E80` recorded zero
calls. Therefore `0x005A5E80` is not the callback that drives the mandatory
login card-use confirmation page. The screen identity asserted in E-026 and
F-028 through F-030 is superseded by this evidence.

Captured log:
`traces/240fps-full-session-20260720-034152.txt`, SHA256
`F5A4592E20E3FF50939DE23DE12C9EBB1E16CE3F81EBF786F66F203E77C9D614`.

## Correct page identity

The existing IDA daemon on `H:\gc\game471.exe.i64` resolves the `start2` login
page to the `SELECT_NOCARD` state family. Its descriptor labels include:

- `SELECT_NOCARD_INIT` and `SELECT_NOCARD_INIT2`;
- `SELECT_NOCARD`, `SELECT_NOCARD_YES`, and `SELECT_NOCARD_NO`;
- `SELECT_NOCARD_DECIDE` and `SELECT_NOCARD_TIMEOUT`;
- `jf_decision_card`, `jf_card_yes`, `jf_card_no`, and
  `jf_card_01_timeout`.

These labels match the `start2_xfl` root `imc_card_01` hierarchy. In static
initializer `sub_69D700`, the descriptor labelled `SELECT_NOCARD_INIT` binds
callback `sub_5A4540`. This is the new starting point for tracing the login
timer, input, and 2D advance path. No production correction should use
`0x005A5E80` as the login-page boundary.

## Other runtime result retained

Sustained sampled IFBL elapsed-time callbacks such as `select-music`,
`difficulty`, and `game-over` ran near 240 calls per second and accumulated
about one second of global delta per real second. The full session therefore
disproves a global 60-Hz IFBL callback-service defect. Shared IFBL delta or
opcode-1 cadence must not be scaled or throttled.
