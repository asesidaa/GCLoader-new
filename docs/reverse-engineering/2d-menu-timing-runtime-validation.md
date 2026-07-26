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

### Corrected-build full-session verdict: prompt flash persists

Runtime evidence appended on 2026-07-26:

- User verdict after another complete session: the other exercised menu
  timing paths appear correct, but the instruction text on `unlock_reward`
  still flashes.
- The failed run is preserved at:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct\FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922\runs\20260726-174555-unlock-flash-still`.
- Preserved `loader-log.txt` SHA-256:
  `F8598F7749AD790EBB097D3B1F4DE2FB59C1299A4E8B259E3EB0840789BC3052`.
- The preserved and live DLL identity is the corrected Stage B build:
  `FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922`.
- Startup selected `menu_timing_mode=correct`, installed all seven menu
  timing contracts, and committed 17 direct writes plus 53 hooks.
- The external 240 FPS cap validated at approximately `239.987` FPS.
- `unlock_reward` activated at `2026-07-26 18:43:26.122`.
- Final UnlockReward counters were:
  - `unlock_countdown=10/29/1`;
  - `unlock_state_primary=0/0/0`;
  - `unlock_state_secondary=10/28/1`; and
  - `menu_diagnostic_read_failures=0`.
- There was no fatal error or crash record. The exact deployed build and all
  three relevant store gates therefore executed as designed; stale
  deployment and failed hook installation are excluded.

This run falsifies the earlier exact-symptom attribution recorded for Stage A.
The raw 33-through-43 state was a real inverse-FPS path and Stage B corrected
it from target cadence to approximately 60 committed updates per second, but
that correction did not remove the visible text flash. The earlier record is
retained as historical reasoning; this section supersedes its claim that the
secondary state was the direct and sufficient cause.

The `unlock_state_primary=0/0/0` result is also material. In this reproduction,
the controller took the no-pending-reward-node path: `0x00430520` moved state
zero directly to 32 and played `jf_unlock_start`; the 1-through-31 primary
range never ran. The remaining ten secondary commits reproduce the original
60 FPS wall time rather than proving an additional high-FPS acceleration.

Follow-up IDA and original-RVB evidence:

- The existing read-only daemon for `H:\gc\game471.exe.i64` remained healthy
  at PID `54944`, port `63612`, with `database_opened=true` and
  `ida_available=true`.
- `0x00430A60` initializes the elapsed completion timer at `this+0x3770` to
  `60.0` seconds through `0x00430240`. This is distinct from the
  `this+0x37D0` activity field.
- After callback `0x005F7D20 -> 0x00430C00` returns true, the IFBL sequence
  conditionally executes a `0.5`-second float wait (opcode `0x10`), then
  opcode `0x22` plays `jf_unlock_end`. The interpreter subtracts its
  elapsed-delta field before evaluating that float wait, so it is not another
  raw target-frame wait.
- In the original `unlock_reward.rvb`, navigator symbol `UNIQUE_7` fades the
  prompt in for ten frames, stops fully opaque, and only fades it out after
  `tg_instmsg01_end` or `tg_instmsg02_end`.
- The Image1 and Image2 bitmap wrappers are `UNIQUE_3` and `UNIQUE_6`.
  Each wrapper has exactly two frames: frame one places the bitmap and frame
  two removes it. Unlike the other two-frame control symbols in this asset,
  neither wrapper contains a `stop()` action.

The two-frame wrappers are a newly isolated, independent flash mechanism.
Their runtime play/graphic synchronization and actual cadence still require
proof before changing code. No broader MovieClip cadence change is justified:
the user's full-session verdict says the other exercised paths are now fine,
and the failed-run log shows the shared MovieClip gate continuing at its
expected approximately 1:3 run/skip ratio.

#### Narrow prompt-wrapper corrective candidate

Follow-up MovieClip class analysis established the runtime identity and
default-play mechanism needed for a scoped correction:

- MovieClipInstance `this+0x110` is its parent/owner pointer.
- `this+0x120` is the instance-name pointer and `this+0x140` is the
  zero-seeded 33x name hash.
- `this+0x11C` is the play/stop flag. Construction initializes it to playing;
  neither `UNIQUE_3` nor `UNIQUE_6` invokes the stop method.
- The two prompt-child instance names and hashes are:
  - `imc_tx` / `0xFCDA0604`; and
  - `igr_un_instmsg01_img` / `0x9D55AF65`.
- Their required direct parent is `imc_un_navi` / `0x59FE24C8`.

The candidate therefore suppresses ordinary timeline motion only for either
exact child identity under that exact parent while menu correction mode is
active. Goto and preprocessing advances remain untouched. The parent
`imc_un_navi` continues to play its authored fade-in, stop, and fade-out
frames; only the nested visible/empty two-frame loop is held on its initially
visible frame. Hashes are only a fast prefilter: the candidate also safely
reads and exactly compares both names so a collision or a generic `imc_tx`
elsewhere cannot activate the correction.

Runtime evidence remains enabled. Periodic statistics publish
`unlock_prompt_holds=<transition>/<stable>`, and separate one-time activation
records identify which of the two prompt-child paths executed. Any invalid
MovieClip identity read increments `menu_diagnostic_read_failures`.

Test-first verification:

- The new policy test first failed because the identity policy and diagnostic
  fields did not exist.
- It covers both exact positive identities plus observe mode, goto,
  preprocessing, wrong parent, wrong name, and synthetic hash-collision
  negatives.
- The complete Debug and Release suites pass: 57 of 57 tests in each
  configuration.
- Candidate SHA-256:
  `F76FDE7FE654E506C8F84A4F3BD0E0245E6429A919BD12A6D1626966FE5535E2`.
- Preserved candidate:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-prompt-hold\F76FDE7FE654E506C8F84A4F3BD0E0245E6429A919BD12A6D1626966FE5535E2\iDmacDrv32.dll`.

This candidate has not been deployed. The live DLL remains corrected Stage B
hash `FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922`;
visual acceptance and the two new hold counters remain pending a gameplay
run.

### Native-60 control and refined static boundary

Runtime evidence appended on 2026-07-26:

- The same live Stage B DLL was exercised at target 60 FPS, and the user
  confirmed that the `unlock_reward` instruction text did not flash.
- Latest log SHA-256:
  `3520BE47E2A485ACAC0C3D2A2AC783B2E298CFC83828A95253286052598E9AD4`.
- Startup selected native timing with `direct_writes=0`, `hooks=2`, and
  `authored_clock=native_bypass`. Therefore the native run does not install
  the MovieClip, UnlockReward-store, preprocessing, or stop-diagnostic hooks.
- The 60 FPS cap validated at approximately `60.0105`.
- Preserved evidence:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct\FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922\runs\20260726-203046-60fps-no-flash`.

Further read-only IDA and original-RVB analysis supersedes the hypothesis that
the two prompt wrappers may simply receive no native service:

- Original `MOVC` definitions `UNIQUE_3` and `UNIQUE_6` dispatch through
  `MovieClip::CreateInstance` at `0x004E12A0`, which allocates a real
  `MovieClipInstance` through constructor `0x004E11A0`.
- The base instance constructor at `0x004D45E0` initializes the play/stop field
  `this+0x11C` to playing.
- The draw visitor recursively enters child MovieClip instances through
  `MovieClipInstance::Accept` at `0x004E0CD0` and visitor handler
  `0x004CEED0`.
- That handler resolves the child timeline and dispatches it to
  `0x004CEC70`, which processes the current frame and invokes the forward
  timeline advance slot before recursively drawing the resulting child
  display list.
- The original PLC3 parser at `0x004DDE80` and placement core at
  `0x004DC4B0` show that repeated PLACE tags reuse a matching child at the
  same depth; they do not silently convert these MOVC definitions into static
  graphics.
- The main loop at `0x00458C10` executes the update/render/present helper chain
  once per loop, and the outer-phase hook at `0x00458B70` observes that loop.
  No static second native traversal has been found that would cancel the
  wrappers' two-frame alternation before presentation.
- The existing correction is installed one semantic layer too low.
  `0x004CEC70` dispatches the current `FRAM` through `Frame::Accept` and
  `Frame::VisitChildren` (`0x004D54D0` and `0x004D1990`) before it calls the
  forward-advance slot that reaches hooked primitive `0x004DF940`. Therefore
  returning success without motion suppresses only the terminal frame-index
  change; the frame's ASRC, PLC3, and RMOV tags have already executed.
- At target 240 FPS this changes the engine contract from native
  `process current frame once; advance once` to
  `process current frame four times; advance once`.
- This mismatch is directly relevant to `unlock_reward.rvb`. Root frame
  `jf_unlock_start` executes cross-clip `gotoAndPlay` calls, including
  `imc_un_navi.gotoAndPlay("tg_instmsg01_start")`; holding that root frame
  replays the child restart. The `UNIQUE_3` and `UNIQUE_6` frames also replay
  their PLACE/RMOV tags while held.

Consequences:

- Static analysis now predicts that both wrappers are serviced at native
  60 FPS. The user's clean native run therefore falsifies the claim that an
  omitted wrapper `stop()` is, by itself, the proven visible-flash cause.
- Static analysis does prove a deeper transformed-path defect: the current
  MovieClip gate decouples frame-content execution from frame advancement and
  over-services frame side effects at target FPS. This is a stronger
  root-cause candidate than the asset-specific prompt hold.
- The binary and asset can enumerate possible internal writers, but they
  cannot identify which changing runtime state produced the user's visual
  observation. The remaining alternatives are the wrapper display-list
  state, parent frame/alpha or visibility, repeated semantic goto/placement,
  or behavior introduced by the transformed-only hook/trampoline path.
- The prompt-hold candidate remains an unproven symptom mask and must not be
  deployed as the root-cause fix.
- The smallest discriminating evidence is per-instance observation at both
  native 60 and transformed 240 FPS: concrete vtable/definition, frame before
  and after traversal, play flag, parent frame/alpha/visibility, PLACE/RMOV
  events, semantic gotos, and the state immediately before Present.

#### Atomic timeline-traversal corrective candidate

The approved root-boundary candidate replaces the primitive-only cadence gate
with an inline hook at DrawTraverse timeline handler `0x004CEC70`
(`RVA 0x000CEC70`). The existing IDA daemon reconfirmed the entry bytes as:

`6A FF 68 A0 2E 67 00 64 A1 00 00 00 00 50 83 EC 44`

Corrected transformed behavior is now:

- authored tick: execute the complete timeline handler, including current
  frame tags, scripts, placement/removal, and terminal frame advance;
- non-authored tick: return before all timeline mutation;
- caller `0x004CEED0` then continues through the retained display-list draw
  path, so already-built 2D content can still render every host frame; and
- goto and preprocessing primitive advances remain immediate. Ordinary
  primitive `0x004DF940` is no longer a cadence boundary.

The prompt-specific hold remains present only as historical identity
instrumentation. It is no longer called from `HookMovieClipAdvance` and
cannot change playback. Consequently `unlock_prompt_holds` must remain
`0/0` in the candidate run.

New runtime evidence distinguishes the two exact wrappers under
`imc_un_navi`:

- `unlock_prompt_transition_timeline=<runs>/<skips>` for `imc_tx`;
- `unlock_prompt_stable_timeline=<runs>/<skips>` for
  `igr_un_instmsg01_img`; and
- one-time `path=movieclip_timeline_traversal` activation records include the
  matched instance and whether the first observation executed or skipped the
  timeline.

At a validated 240 FPS cap, both exact paths should be nonzero and converge
toward one run per three skips. The global `movieclip=<runs>/skip=<skips>`
field now measures the same higher semantic boundary rather than the
primitive frame-index function.

Test-first evidence:

- changing ordinary primitive non-tick behavior first failed the focused
  policy matrix twice, then passed after the primitive gate was removed;
- the new traversal policy first failed on corrected non-authored ticks, then
  passed after the atomic skip decision was implemented;
- the hook-plan test first failed on the absent ID/capacity, then passed with
  the byte-guarded `0x000CEC70` contract and runtime binding;
- transformed capacity is now 54 contracts, or 53 installed hooks without
  the optional WASAPI resync hook and 54 with it; and
- complete Debug and Release suites pass 57 of 57 tests each.

Release candidate SHA-256:
`DF8BE5BC2B82F08861BE777E8DB68938DD1F32586164FDCB1A1BEA6F7C7726AA`.
The exact candidate is preserved at
`H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-atomic-timeline\DF8BE5BC2B82F08861BE777E8DB68938DD1F32586164FDCB1A1BEA6F7C7726AA\iDmacDrv32.dll`
and deployed to `H:\gc\iDmacDrv32.dll`; post-copy SHA-256 matched. No
`game471` process was active during deployment, and the old
`H:\gc\loader-log.txt` was intentionally removed so the next capture begins
cleanly. Runtime visual acceptance remains pending the requested 240 FPS
full-session run.

#### Atomic-candidate Stamp regression and rollback

The first 240 FPS runtime attempt falsified the candidate's retained-render
assumption before reaching UnlockReward:

- the user reported that both Stamp images and Stamp text were absent;
- the deployed DLL hash was the expected atomic candidate
  `DF8BE5BC2B82F08861BE777E8DB68938DD1F32586164FDCB1A1BEA6F7C7726AA`;
- startup committed 17 direct writes and all 54 hooks;
- the cap validated at approximately `240.038` FPS;
- by the final periodic sample, the new boundary had executed 70,401
  timelines and skipped 211,100, the expected approximately 1:3 ratio; and
- there were no diagnostic-read failures, fatal records, or deployment
  mismatches.

Failure-log SHA-256:
`0CC76D9ECEC4DE6DA633091717FA7F16101B2D7F159076D377A35662EFACBF25`.
It is preserved at
`H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-atomic-timeline\DF8BE5BC2B82F08861BE777E8DB68938DD1F32586164FDCB1A1BEA6F7C7726AA\runs\20260726-233655-stamp-images-text-missing\loader-log.txt`.

Re-reading the IDA call split identifies the invalid assumption:

- `0x004CEC70` obtains the current frame and invokes its virtual `Accept`
  method before the terminal advance;
- `Frame::Accept` at `0x004D54D0` dispatches back to the DrawTraverse visitor,
  whose child iteration at `0x004D1990` visits every frame tag;
- these visits are not merely timeline mutation. They emit the current
  bitmap/text/display-list content for the presented frame;
- caller `0x004CEED0` performs transform/render-state setup and restores that
  state after the instance dispatch. It does not contain a separate
  retained-display-list draw after `0x004CEC70`; and
- whole-handler suppression therefore prevents current frame content from
  being emitted on non-authored host frames. The candidate blanked three of
  every four Stamp presentations, which is a rendering regression rather
  than a valid timing correction.

The candidate is rejected. As containment, `H:\gc\iDmacDrv32.dll` was restored
while `game471` was stopped to the prior Stage B hash
`FD719CDF4539387AA2E1550840265B46BC13A2F14B57CEB4AD7483AE2C7A8922`.
No Stamp-specific exception is justified: the global hook boundary itself is
semantically invalid.

#### Narrow-gate rollback with passive instance diagnostics

The replacement candidate restores the last behaviorally safe hook boundary
and adds observation without another trampoline:

- transformed plans are back to 53 total contracts, or 52 installed hooks
  without optional WASAPI and 53 with it;
- `0x004CEC70` is absent from the plan and has no runtime binding;
- ordinary `MovieClip::AdvanceOneTimelineFrame` calls at `0x004DF940` are
  again skipped on non-authored ticks;
- goto traversal remains immediate;
- corrected preprocessing calls remain forced, preserving the prior
  asset-load fix; and
- the rejected exact UnlockReward prompt hold remains disabled.

`FrameratePatchPlanTests` now permanently rejects any cadence contract at
RVA `0x000CEC70` with the explicit invariant that the mixed DrawTraverse
render/update handler must never be gated. The test first failed against the
54-hook candidate and passed only after the hook and binding were removed.

Passive diagnostics run through the existing MovieClip goto and advance
hooks. No hook is added. Identity matching uses the engine's zero-seeded 33x
hash as a prefilter and then requires the exact instance string:

- Stamp `imc_scard` / `0x09CA86C5`;
- Stamp `imc_window` / `0x4CE37C30`;
- UnlockReward `imc_tx` / `0xFCDA0604`, with exact parent
  `imc_un_navi` / `0x59FE24C8`; and
- UnlockReward `igr_un_instmsg01_img` / `0x9D55AF65`, with the same exact
  parent.

IDA establishes the fields observed by the diagnostic:

- `0x004DF940` reads and writes the one-based 64-bit current frame at
  `MovieClipInstance+0x178`;
- `0x004D1730` writes the stop flag at `MovieClipInstance+0x11C`; and
- `MovieClipInstance+0x110` is the parent pointer already proven during the
  prompt-identity analysis.

For an exact target, the hook records frame and stop state for the instance
and parent before and after the existing operation. It also records authored
tick, ordinary/preprocessing context, run/skip decision, forward/loop
arguments, semantic goto requests, return value, and outer epoch. Logging is
bounded to 512 detailed samples per target. First observations of each path
and every observed frame/stop-state change are retained until that cap;
later qualifying samples increment a suppression count rather than producing
unbounded log output.

Periodic runtime fields use this order:

`ordinary_runs/ordinary_skips/preprocess_runs/preprocess_skips/goto_calls/frame_changes/samples_logged/samples_suppressed`

The four fields are:

- `movieclip_diag_stamp_scard`;
- `movieclip_diag_stamp_window`;
- `movieclip_diag_unlock_transition`; and
- `movieclip_diag_unlock_stable`.

The diagnostic is deliberately non-corrective. It cannot suppress a frame,
change a MovieClip field, issue a goto, or alter the existing cadence
decision. `unlock_prompt_holds` must remain `0/0`.

Verification and deployment:

- complete Debug suite: 57 of 57 tests passed;
- complete Release suite: 57 of 57 tests passed;
- candidate SHA-256:
  `F79E07187A47E8C4DD8B51356121AD94297D7912F2B811D605CA0DDB02828CCE`;
- preserved candidate:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-narrow-diagnostics\F79E07187A47E8C4DD8B51356121AD94297D7912F2B811D605CA0DDB02828CCE\iDmacDrv32.dll`;
- `game471` was stopped during deployment;
- the build, archive, and live `H:\gc\iDmacDrv32.dll` hashes matched; and
- the old `H:\gc\loader-log.txt` was removed for an unambiguous next run.

Runtime acceptance remains pending. The next 240 FPS run must exercise Stamp
through visible images/text and then reach UnlockReward long enough to
observe Image1/Image2 flashing. It may stop after UnlockReward; Ranking and a
second full session are unnecessary for this discriminating capture.

#### Failed narrow diagnostic and exhaustive replacement

The subsequent full-session log falsified the narrow diagnostic's structural
assumption. The deployed DLL was the expected
`F79E07187A47E8C4DD8B51356121AD94297D7912F2B811D605CA0DDB02828CCE`;
the captured `loader-log.txt` SHA-256 was
`EC681A7C47A05E4C8A9763BD353C3F1D86623C0F712B9770BDAA675A0B74D637`.
Stamp rendered correctly and Ranking was reached, but UnlockReward produced
no exact prompt samples:

- `movieclip_diag_unlock_transition` and
  `movieclip_diag_unlock_stable` remained zero;
- `menu_diagnostic_read_failures` rose from zero to 908 immediately after
  UnlockReward activation and ended at 1,453; and
- failures occurred after the prompt hash prefilter matched, so the missing
  evidence was caused by the subsequent string/owner chain rather than a
  failure to encounter the prompt objects.

Rechecking the existing IDA daemon corrected the layout:

- the base constructor at `0x004D45E0` initializes `+0x110`, `+0x118`,
  `+0x11C`, `+0x120`, `+0x140`, and `+0x150` as distinct fields;
- the MovieClipInstance constructor at `0x004E11A0` stores its definition at
  `+0x118`; and
- the placement core at `0x004DC4B0`, specifically
  `0x004DC575: mov [esi+150h], edi`, writes the display owner. Therefore
  `+0x150`, not `+0x110`, is the owning parent link.

The replacement diagnostic deliberately has redundant coverage. The
transformed manifest now contains 60 contracts, or 59 installed hooks without
optional WASAPI and 60 with it. Seven new passive boundaries are present:

| Boundary | RVA | Evidence captured |
|---|---:|---|
| MovieClip creation | `0x000E12A0` | returned instance and definition |
| owner placement | `0x000DC575` | child in ESI and owner in EDI before the `+0x150` write |
| frame-tag dispatch begin | `0x000D19A6` | tag pointer, vtable, visitor, and raw tag bytes |
| frame-tag dispatch end | `0x000D19B0` | the same tag after visitor execution and return EAX |
| DrawTraverse entry | `0x000CEC70` | traversal object and current MovieClip from `+0xF8` |
| DrawTraverse exit | `0x000CEEB6` | paired traversal/MovieClip post-state |
| present phase | `0x00058A50` | once-per-present snapshots of the retained object registry |

The `0x004CEC70` hooks are midhooks and never suppress or replace the mixed
draw/update handler. The Stamp regression guard now permits only the named
passive diagnostic entry there; cadence gating remains forbidden.

The existing goto, advance, preprocessing, stop, and three UnlockReward-store
hooks also write to the same sequence-numbered ledger. UnlockReward scene
activation enables unbounded raw logging until Ranking begins. Target
recognition falls back to the instance hash when the name pointer, name
string, owner, or owner name is unavailable. Each failed read has its own
stage name and address rather than disappearing into one aggregate counter.
Every retained MovieClip snapshot includes decoded fields plus raw bytes from
`+0x110..+0x18F`; the two prompt hashes additionally receive a full `0x2C0`
byte instance dump on every present.

Static and test verification for this replacement:

- all seven entry byte sequences were read from the connected
  `game471.exe.i64` database;
- focused hook-contract and menu-timing tests pass;
- complete Debug suite: 57 of 57 tests passed; and
- complete Release suite: 57 of 57 tests passed.

The exhaustive diagnostic Release DLL SHA-256 is
`A06E2D7560D211968B8FF5C4A222CAEAC72E22B75BA4CA8B852FF0DED110A29E`.
The build and deployed `H:\gc\iDmacDrv32.dll` hashes match, `game471` was not
running during deployment, and the prior `H:\gc\loader-log.txt` was removed.

#### Exhaustive-run result and corrective boundary

Runtime evidence appended on 2026-07-27:

- The exhaustive diagnostic DLL was exercised through Stamp and the complete
  pre-selection portion of UnlockReward. The deployed DLL remained the expected
  SHA-256
  `A06E2D7560D211968B8FF5C4A222CAEAC72E22B75BA4CA8B852FF0DED110A29E`.
- The resulting `H:\gc\loader-log.txt` is 104,856,868 bytes with SHA-256
  `B9D566A0AF5B77151E5693FB5FFCAA8453F4ED5FD093BC9E461BED50018937B5`.
- The bounded exact-instance diagnostic activated before reward selection for
  both corrected `+0x150` owner chains:
  - `imc_tx` / `0xFCDA0604`, instance `0x1F77F330`;
  - `igr_un_instmsg01_img` / `0x9D55AF65`, instance `0x1F77C1D0`;
  - both owners were verified as `imc_un_navi` / `0x59FE24C8` at
    `0x1F777EF0`.
- `imc_tx` advanced from frame 1 to frame 2 and then issued an internal
  `goto(1)` while its parent traversed entrance frames 2 through 11.
- Once the parent stopped on frame 12, `igr_un_instmsg01_img` continued the
  same frame-1/frame-2 loop for the whole selection wait. At
  `2026-07-27 01:34:31.342`, its counters were
  `304/915/0/0/152/305/306/0`: 304 authored ordinary advances, 915
  non-authored skips, 152 internal gotos, and 305 observed state changes.
- The original RVB maps runtime frame 1 of `UNIQUE_3` and `UNIQUE_6` to the
  `PLACE` tag for Image1 and Image2 respectively, and runtime frame 2 to the
  corresponding `RMOV`. Both definitions are the same two-frame, no-stop
  structure. `UNIQUE_7` assigns both definitions the same two runtime instance
  names above, so one exact identity policy covers both reward prompts.
- After selection, the stable wrapper was stopped on visible frame 1. The raw
  traversal ledger then showed its `PLACE` tag executing on every presentation,
  confirming the decoded identity, frame, and tag mapping.
- Stamp rendered correctly in this run. Its tracked paths did not exhibit the
  persistent no-stop two-frame PLACE/RMOV loop: `imc_scard` stopped after its
  first authored advance and `imc_window` completed its finite authored
  sequence.

This evidence promotes the two exact UnlockReward wrappers from a speculative
asset mask to the demonstrated source of the visible/empty alternation. The
correction must be frame-aware and fail closed:

- only corrected transformed mode and ordinary automatic playback are eligible;
- both the exact child name/hash and the exact `+0x150` owner name/hash must
  match;
- a playing wrapper already on visible frame 1 is held there;
- an eligible wrapper first encountered on frame 2 is allowed to execute its
  original wrap to frame 1 before subsequent calls are held; and
- goto traversal, preprocessing, stopped clips, parent animation, Stamp, and
  every unrelated MovieClip remain untouched.

The exhaustive ledger itself is not suitable for another gameplay candidate.
UnlockReward activation produced 59,288 object-event lines, 107,216 tag-event
lines, and 185 full registry snapshots in approximately eleven seconds. Its
synchronous output reduced presentation to roughly 30 FPS and prevented the
normal post-selection flow. The corrective candidate must remove those seven
raw hooks and retain only bounded exact-path counters/activation records.

#### Corrective candidate implementation

The next candidate implements the demonstrated frame-aware boundary directly
in the existing `MovieClipAdvance` hook:

- target recognition still starts with the instance hash, but the correction
  itself requires the live child string/hash and the live owner string/hash;
- the owner is read from the proved `MovieClipInstance+0x150` field;
- the current one-based frame is reconstructed from `+0x178/+0x17C`, and the
  stop flag is read from `+0x11C`;
- only ordinary playback in corrected transformed timing can be held;
- `imc_tx` and `igr_un_instmsg01_img`, owned by `imc_un_navi`, return success
  without motion only while playing on visible frame 1;
- frame 2 executes normally so it can wrap to frame 1, while stopped clips,
  goto execution, preprocessing, Stamp, and every nonmatching MovieClip remain
  on their existing paths.

The seven exhaustive diagnostic contracts and all associated raw logging,
object retention, tag dumping, and present snapshots have been removed. The
manifest is restored to 53 contracts: 52 installed at transformed timing
without optional WASAPI and 53 with it. The retained diagnostics cover only
the four named MovieClips, cap samples per target, and expose separate
transition/stable hold counters plus one-time activation records.

Static verification completed for this candidate:

- focused menu policy, hook-plan, transaction, and runtime tests pass;
- complete Debug suite: 57 of 57 tests passed;
- complete Release suite: 57 of 57 tests passed;
- the Release DLL is x86 and has SHA-256
  `4560F5FFFD67431E6C48DD83B383DBB996F107D89ACF5CA844EDE2AF320E56FA`;
- the built and deployed `H:\gc\iDmacDrv32.dll` hashes match, and `game471`
  was not running during deployment.

The existing log was retained. Its pre-test boundary is byte
`104856868`, with SHA-256
`B9D566A0AF5B77151E5693FB5FFCAA8453F4ED5FD093BC9E461BED50018937B5`,
so the next review can read only newly appended data.

## Stage C — Accepted Diagnostic Cleanup

Status: unlocked by explicit user acceptance and cleanup authorization.

### Accepted corrective build and preserved runtime evidence

The exact source exercised by the accepted run is commit `817c00f`
(`fix: finalize unlock reward prompt timing`) on `ctune-effect-timing`.

- Live and archived DLL SHA-256:
  `4560F5FFFD67431E6C48DD83B383DBB996F107D89ACF5CA844EDE2AF320E56FA`.
- Immutable DLL:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-prompt-hold\4560F5FFFD67431E6C48DD83B383DBB996F107D89ACF5CA844EDE2AF320E56FA\iDmacDrv32.dll`.
- Accepted 240 FPS log SHA-256:
  `BB4289699AE8A223F343A4ABDA64CC9CD964481A699AAFDCC0E562647A50E942`.
- Immutable accepted log:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-prompt-hold\4560F5FFFD67431E6C48DD83B383DBB996F107D89ACF5CA844EDE2AF320E56FA\runtime-logs\loader-log.target-240.20260727-022224.accepted.txt`.

The accepted run covered a complete gameplay session at configured target
240 FPS and measured approximately 240.136 FPS. The transformed transaction
installed 53 hooks. No fatal, crash, exception, preflight, or hook-install
failure was logged.

The exact UnlockReward prompt hold activated for both proved wrappers:

- transition wrapper: 276 holds;
- stable wrapper: 1,341 holds;
- all sampled held calls remained on visible frame 1, were ordinary
  automatic playback, and retained the verified `imc_un_navi` owner; and
- no held call ran the original advance or issued a semantic goto.

The raw-store totals remained consistent with the shared authored cadence:

- Ranking: 21,370 commits / 64,110 suppressions;
- HitChart: 24,869 commits / 74,666 suppressions;
- UnlockReward countdown: 10 commits / 29 suppressions / 1 boundary;
- UnlockReward secondary state: 10 commits / 28 suppressions / 1 boundary;
- UnlockReward primary state was not exercised in this session.

The post-reward presentation sample remained at target cadence rather than
the earlier diagnostic-induced 30 FPS regression. Stamp controls remained
normal (`imc_scard` 1/1 and `imc_window` 32/94 with two gotos). The only
unrelated warning was that the configured XInput slot was unavailable.

The user's exact acceptance statement was:

> "Now check the latest log. The flashing issue is finally fixed. A full
> session with hit chart, ranking, reward and more is played."

Visual and timing verdict: pass for the reproduced 240 FPS full session,
including UnlockReward prompt text, HitChart, Ranking, Reward, Stamp, and the
other traversed gameplay/menu paths. Input was not separately graded, but the
full session completed and no input regression was reported.

The native 60 FPS comparison previously confirmed that the prompt did not
flash. Transformed 120 and 144 FPS were not exercised for this final
corrective DLL. UnlockReward primary-state cadence was not reached in the
accepted 240 FPS session. Those targets/paths remain explicitly unaccepted.

### Cleanup authorization

The user then explicitly authorized the destructive Stage C transition:

> "Now let's clean up and merge into main."

This authorizes removal of temporary diagnostic machinery only. The exact
UnlockReward prompt-frame hold, the preprocessing exemption, the five safe
counter-store gates, their lightweight cumulative totals, and all immutable
DLL/log evidence remain permanent.

### Final cleanup build

Stage C cleanup is recorded by:

- `a594bed` — `refactor: remove accepted menu stop diagnostic hook`;
- `1099541` — `refactor: trim accepted menu timing diagnostics`.

The final source state is `1099541` on `ctune-effect-timing`. It retains:

- the exact UnlockReward prompt-frame hold and its transition/stable totals;
- the depth-only preprocessing exemption and visit/forced totals;
- the relocated safe Ranking and HitChart counter hooks;
- all three UnlockReward raw-store gates;
- exact child/owner/frame/stopped fail-closed matching; and
- the existing shared authored-60 phase, Navigator behavior, and
  `OuterFrame` ownership.

It removes the temporary Stop hook, mode selector, causal Stop state, Stamp
and UnlockReward sample streams, destination/value/boundary reads, activation
latches, outer diagnostic epoch, revisit tracker, and diagnostic read-failure
aggregation.

Final static verification:

- full transformed contract capacity: 52;
- transformed plans: 51 hooks without optional WASAPI and 52 with it;
- native plans: 1 hook without optional WASAPI and 2 with it;
- `NavigatorAdvance` / `OuterFrame` final indices: 50 / 51;
- complete x86 Debug suite: 57 of 57 passed;
- complete x86 Release suite: 57 of 57 passed;
- temporary-symbol negative source/test search: passed;
- `git diff --check`: passed; and
- Release image: `14C machine (x86)`, 32-bit.

Final Release DLL:

- size: 5,622,784 bytes;
- UTC timestamp: `2026-07-26T17:45:47.6134954Z`;
- SHA-256:
  `7E667E693B33E0C312A91A9B0D258EC8826EBC70282DDF25BBCA911FFA37C8A3`;
- immutable archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-c-final\7E667E693B33E0C312A91A9B0D258EC8826EBC70282DDF25BBCA911FFA37C8A3\iDmacDrv32.dll`.

The final cleanup DLL was not copied to `H:\gc\iDmacDrv32.dll`. The live DLL
remains the accepted Stage B build with SHA-256
`4560F5FFFD67431E6C48DD83B383DBB996F107D89ACF5CA844EDE2AF320E56FA`,
and its accepted log remains untouched. Therefore Stage C has static
verification but no post-cleanup runtime smoke verdict.

Transformed 120 and 144 FPS, UnlockReward primary-state cadence, and every
path not named in the accepted 240 FPS session remain unaccepted. They are
not promoted to pass by cleanup or by the successful static suites.
