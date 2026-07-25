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

Crash-repair static evidence appended on 2026-07-26; this is a new
observe-only candidate and does not rehabilitate the rejected crashing build:

- Source commit:
  `55e3b4f0b15381faf73667f99442570adf23ee70`
- Active mode: `observe`
- Release candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing\build-msvc32-release\dist\iDmacDrv32.dll`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026\iDmacDrv32.dll`
- Size: `5,656,064` bytes
- Last write time:
  `2026-07-26T03:32:00.3685629+08:00`
  (`2026-07-25T19:32:00.3685629Z`)
- SHA-256:
  `2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026`
- Architecture: PE `14C machine (x86)`, 32-bit word machine
- Debug gate: complete build and `57/57` CTest tests passed
- RelWithDebInfo gate: complete build and `57/57` CTest tests passed
- Candidate/archive hash comparison: exact match
- Ranking contract:
  RVA `0x00216EB4`, bytes `8B 4D E0 89 01`, shared continuation
  `0x00216EB9`
- HitChart contract:
  RVA `0x0026562F`, bytes `8B 8D 6C FF FF FF`, suppression continuation
  `0x00265637`
- The five counter paths now use explicit original-code continuations instead
  of trampoline-relative instruction-length increments.
- The rejected DLL, full-session log, crash dump, and immutable crash-run
  archive remain unchanged.

This candidate still performs no menu timing correction. Its next runtime gate
is limited to proving that Ranking and HitChart can activate and complete
without the previous interior-detour crash. Required evidence is nonzero
`ranking_entry` and `hitchart_entry` activation/counters, no crash through both
attract screens, and a final `menu_timing_mode=observe` record.

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

Crash-repair redeployment evidence appended on 2026-07-26; the preceding
deployment identifies the rejected crashing DLL and is retained:

- Deployment time:
  `2026-07-26T03:35:03.7629848+08:00`
  (`2026-07-25T19:35:03.7629848Z`)
- Live path: `H:\gc\iDmacDrv32.dll`
- Source archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026\iDmacDrv32.dll`
- Live size: `5,656,064` bytes
- Live SHA-256:
  `2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026`
- Pre-deployment process check: no `game471` process
- Post-copy verification: Release candidate, immutable archive, and live
  SHA-256 values match exactly
- Preserved prior full-session log SHA-256:
  `29935BEC9AB11736AEEAAEDC6396DCBC2A01C40F81C1DA13F7FBB2C85C4FE7A3`
- Preserved prior crash dump SHA-256:
  `DF24E584FC5D7C55BCDA5AE3E32F3A165CB067F53BD7D09884A3362E7E62E611`
- The rejected DLL remains available in its immutable hash archive and crash
  run archive.

Status: relocated-hook observe-only candidate deployed; Ranking and HitChart
runtime safety/activation test pending.

### Runtime exercises

Status: user run not yet performed.

Runtime evidence appended on 2026-07-26; the initial status above is retained
as historical context and is superseded by this entry:

- User exercise: one complete 240 FPS gameplay session through post-play reward
  and entry into the ranking screen
- Result: the ranking screen crashed; Stage A is rejected as unsafe
- Preserved evidence:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\runs\20260726-033536-ranking-crash`
- Full-session log SHA-256:
  `29935BEC9AB11736AEEAAEDC6396DCBC2A01C40F81C1DA13F7FBB2C85C4FE7A3`
- Crash dump SHA-256:
  `DF24E584FC5D7C55BCDA5AE3E32F3A165CB067F53BD7D09884A3362E7E62E611`
- Live DLL SHA-256:
  `4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F`
- The live DLL and release candidate hashes match exactly.
- The 240 FPS external cap validated at measured FPS `240.159`.
- Final menu counters before the crash:
  - `movieclip_preprocess=0/0/0`
  - `movieclip_preprocess_stop=0/0`
  - `movieclip_revisit=0/151900`
    (zero same-epoch revisits, 151,900 tracker hash collisions)
  - `ranking_entry=0/0`
  - `hitchart_entry=0/0`
  - `unlock_countdown=2/8/1`
  - `unlock_state_primary=0/0/0`
  - `unlock_state_secondary=2/8/1`
  - `menu_diagnostic_read_failures=0`
- The unlock countdown and secondary-state samples were both observed on
  non-authored ticks with action `would_suppress`.
- Gameplay-effect evidence from the same final record:
  - `gameplay_effect=6598/skip=19796`
  - `effect_tutorial_elapsed=3021`
  - `effect_chart_preroll=0`
  - `effect_player_modulo=0`

### Codex interpretation

Status: no runtime log has been supplied.

Interpretation appended on 2026-07-26; the initial status above is superseded:

- The full session exercised the unlock countdown and secondary-state paths,
  but not the primary-state path.
- The MovieClip preprocessing visitor and causal-stop diagnostics did not
  activate. The ordinary MovieClip tracker found no same-object repeat within
  an outer epoch; its direct-mapped table accumulated 151,900 collisions.
- Ranking did not produce a callback sample. Dump and IDA evidence proves this
  was not merely an unexercised counter: the ranking screen took a branch into
  the interior of the seven-byte SafetyHook overwrite before the callback.
- Root cause and the paired hit-chart exposure are recorded in
  `2d-menu-timing-ranking-crash-investigation.md`.

### User verdict

Stage A is diagnostic-only and carries no fix verdict.

User-reported runtime verdict appended on 2026-07-26:

- full gameplay session completed
- ranking screen crashes with the Stage A hooks installed
- no menu timing correction acceptance is implied

### Relocated-hook follow-up run

Runtime evidence appended on 2026-07-26:

- User exercise: one complete 240 FPS gameplay session with Ranking available
  and an eligible `unlock_reward` flow.
- Preserved evidence:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\runs\20260726-035055-ranking-safe-reward-flash`
- Full-session log SHA-256:
  `B01E64BF966D41F097A486A41B18E8E8496BB2DDE89EBB0B84EB2F2438032A57`
- Live DLL SHA-256:
  `2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026`
- Active mode: `observe`; no menu timing stores were suppressed.
- The external cap validated at target `240` and measured FPS `240.174`.
- Ranking activated at `2026-07-26 04:42:51.279`. Its first sample was a
  non-authored `0 -> 1` store with action `would_suppress`.
- Final Ranking counters were `ranking_entry=32445/97335`, an exact 1:3
  authored/non-authored split. The relocated Ranking hook remained active for
  the rest of the session without the previous crash.
- HitChart remained `hitchart_entry=0/0` for the entire run. It was not
  presented by the game, so its relocated hook remains runtime-unaccepted.
- UnlockReward activated at `2026-07-26 04:46:01.489`.
- Its first countdown sample was `10 -> 9` on a non-authored tick.
- Its first secondary-state sample was `33 -> 34` on a non-authored tick.
- Final UnlockReward counters were:
  - `unlock_countdown=2/8/1`
  - `unlock_state_primary=0/0/0`
  - `unlock_state_secondary=2/8/1`
- `menu_diagnostic_read_failures=0`.
- No fatal/error/crash record was present, and the input worker stopped after
  the game process exited.

User-visible evidence:

- The user identified the affected scene as `unlock_reward`, not `reward2`.
- The instruction text represented by
  `unlock_reward_xfl\LIBRARY\Image1.png` and `Image2.png` visibly flashed much
  too quickly.

### Relocated-hook follow-up interpretation

IDA and RVB/XFL evidence appended on 2026-07-26:

- Existing `game471.exe.i64` daemon PID `54944`, port `63612`, was reused in
  read-only mode.
- `0x00430520` sends the root MovieClip to `jf_unlock_start` when the native
  state reaches 31, or directly when no pending reward node exists.
- The root `jf_unlock_start` action sends `imc_un_navi` to
  `tg_instmsg01_start`.
- In the original `unlock_reward.rvb`, `Image1.png` is shape `UNIQUE_2`,
  wrapped by `UNIQUE_3`; `Image2.png` is shape `UNIQUE_5`, wrapped by
  `UNIQUE_6`. Both wrappers are owned by the instruction/navigator clip
  `UNIQUE_7`, instantiated as root path `imc_un_navi`.
- `UNIQUE_7` has matching ten-update entrance sequences:
  - `tg_instmsg01_start` begins at frame 1, moves through frames 2 through 9,
    settles the `UNIQUE_3` instruction at frame 10, and stops at frame 11.
  - `tg_instmsg02_start` begins at frame 23, moves through frames 24 through
    31, settles the `UNIQUE_6` instruction at frame 32, and stops at frame 33.
- In `0x00430C00`, native field `this+0x37D4` advances from 33 through 43
  once per recurrent IFBL update. Reaching 43 participates in completion of
  the UnlockReward flow.
- The runtime `2/8/1` secondary-state split proves that this ten-update native
  window completed with only two authored 60 Hz stores and eight extra
  non-authored 240 Hz stores. Observe mode deliberately allowed all ten.
- The shared MovieClip path remained gated to authored ticks, so the native
  completion window ran approximately four times faster than its coupled
  ten-frame instruction animation. This is the direct cause of the reported
  Image1/Image2 flash.
- Correct mode's existing narrow store gate is the intended correction: retain
  elapsed-time motion, drawing, input, and semantic gotos, while suppressing
  only the eight non-authored countdown/state stores observed here.

Follow-up status:

- Ranking relocated-hook runtime safety: accepted for this 240 FPS run.
- HitChart relocated-hook runtime safety: still unexercised and unaccepted.
- UnlockReward coverage finding: promoted from medium-confidence visible risk
  to runtime-confirmed visible defect.
- Stage A remains diagnostic-only; the user should still expect the visual
  defect in Observe mode.

## Stage B — Corrected with Diagnostics Retained

Status: corrected DLL deployed; gameplay acceptance pending.

### Corrected build identity

Build evidence appended on 2026-07-26:

- Source commit:
  `7ab8e9a7dc865050e844c0ea8b737ea177ed40c2`
- Branch: `ctune-effect-timing`
- Active internal mode: `correct`
- Release candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing\build-msvc32-release\dist\iDmacDrv32.dll`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct\FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922\iDmacDrv32.dll`
- Size: `5656064` bytes
- Candidate UTC timestamp: `2026-07-25T20:19:33.7044882Z`
- Candidate and archive SHA-256:
  `FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922`
- `dumpbin /headers` reports machine `14C (x86)` and a 32-bit word
  machine.
- Executable SHA-256:
  `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- IDB SHA-256:
  `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F`
- The live `H:\gc\iDmacDrv32.dll` was not replaced. It remains the Stage A
  Observe build with SHA-256
  `2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026`.

### Static verification

- The active-build regression was run before the production selector change.
  `FramerateMenuTimingTests` failed only for the expected Correct-mode
  identity, preprocessing exemption, Ranking gate, HitChart gate, and three
  UnlockReward gates.
- After changing only `ActiveMenuTimingMode()` from `Observe` to `Correct`,
  the focused `FramerateMenuTimingTests` passed.
- The focused Debug framerate gate passed all `9/9` tests.
- Fresh `msvc32-debug` configure and complete build succeeded; its complete
  CTest suite passed `57/57`.
- Fresh `msvc32-release` configure and complete build succeeded; its complete
  CTest suite passed `57/57`.
- The verified policy simulations cover 60, 120, 144, and 240 FPS, including
  the deterministic 144 FPS rational phase sequence and the Ranking,
  HitChart, UnlockReward primary, and UnlockReward secondary transition
  lengths.
- The corrected contract remains at capacity `53`: the complete transformed
  view has 53 contracts, selected transformed plans contain exactly `52/53`
  hooks without/with optional WASAPI, and native plans contain exactly `1/2`.
- Navigator remains transformed contract index 51 and `OuterFrame` remains
  index 52. Every transformed contract has a non-null runtime binding, and
  rollback tests pass across all 53 hook positions.
- All Stage A diagnostics remain installed and represented, including
  `MovieClipStopDiagnostic`, preprocessing causal-stop attribution, MovieClip
  revisit/collision tracking, activation/sample lines, boundary counters, and
  diagnostic-read failures.

This is static/build evidence only. No Stage B visual, timing, input, crash,
Ranking, HitChart, or UnlockReward runtime verdict has been recorded.

### Corrected deployment identity

Deployment evidence appended on 2026-07-26:

- User authorization: `Deploy the dll`
- Pre-deployment process check: no `game471.exe` process was running.
- Deployment timestamp: `2026-07-25T22:12:41.5560533Z`
- Previous live Stage A SHA-256:
  `2581359EAF0149A116B407289B4E2E5ACAFEA35C095458201C76FBA372723026`
- Previous live DLL and loader log snapshot:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\pre-deploy\20260726-061142`
- Deployment source:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct\FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922\iDmacDrv32.dll`
- New live path: `H:\gc\iDmacDrv32.dll`
- Source and live SHA-256:
  `FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922`
- Live size: `5656064` bytes
- Post-deployment process check: no `game471.exe` process was running.

This is deployment identity evidence only. Runtime behavior remains unaccepted
until the user completes gameplay testing and supplies the resulting log and
visual, timing, input, and crash verdicts.

## Stage C — Accepted Diagnostic Cleanup

Status: gated on explicit user acceptance of Stage B.
