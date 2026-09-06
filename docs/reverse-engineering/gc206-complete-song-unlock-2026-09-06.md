# GC 2.06 complete song and difficulty unlock correction

Status: implemented; native evidence, CLion diagnostics, and x86 Debug/Release builds checked. On 2026-09-06 the user confirmed that the correction works and requested committing it.

This record supersedes the SongUnlock completeness claim in the earlier [2.06 implementation record](gc206-implementation-2026-09-06.md). The user initially reported that the general 2.06 port runs, but difficulty unlock was incomplete. The user also confirms that their 4.74 is the binary analyzed as `game471.exe.i64`.

## Cause

The original 2.06 port selected the wrong scope of native behavior. Its only write at RVA `0x223F10` redirects `GC206_SongAvailability_Rebuild` (`0x223E40`) into the all-song list branch at `0x2240E4`. That branch adds song IDs to the list at owner `+0x758`, retaining the native exclusions for song types 1 and 3. It does not compute difficulty availability.

The newer binary's patched availability routine (`0x257580`, patch at `0x257854`) also computes the rating cap and per-song EXTRA availability. Matching the older song-list branch was therefore not a complete semantic port. Correct bytes and successful builds did not establish equivalent feature behavior.

In 2.06, `CDifficultyTask` initialization at `0x19AAA0` separately computes the EXTRA state. Its normal path requires a nonempty EXTRA chart reference at song `+0x98`, the catalog eligibility byte returned by `0x223B00`, and rank-zero records on SIMPLE, NORMAL, and HARD, unless the native shared-selection override applies. The grade UI table identifies rank zero as `jf_rs_s` (S); an unplayed record returns 6, displayed as `jf_rs_x`. Thus the missing progression bypass is specifically the three S-rank prerequisites, not just a generic clear check.

The state is 0 for unavailable, 1 for locked, and 2 for selectable. At `0x19AD3D` a previous EXTRA selection is downgraded to HARD unless state is 2. The input handler at `0x19A4E0` also restricts the number of difficulties to `3 + (state == 2)`. This is a functional input restriction, not just a locked badge.

## Complete availability policy

With the existing option enabled, each local availability consumer treats a song's defined EXTRA chart as selectable, independent of its native eligibility byte and the player's S-rank prerequisites. Ordinary song membership continues through the existing all-song branch. A missing EXTRA chart reference stays unavailable. Normal category, selected-difficulty, rating-range, and special song-type rules remain native.

The implementation applies this policy at consumers, using direct guarded byte patches. It does not replace the shared eligibility or grade getters. Those getters also serve achievement evaluation, saved records, genuine unlock notifications, and reward processing. Likewise it does not set system-config `+0x24`, whose other consumers unlock non-song inventory, or the tournament setting.

The complete set contains 13 writes: the existing song-list write plus 12 new writes across nine availability functions. Every site belongs to the same optional SongUnlock feature plan and participates in the existing global preflight before installation.

| Consumer | Function RVA | Behavior covered |
| --- | --- | --- |
| Song membership | `0x223E40` | Native all-song list |
| Other-player focus panel | `0x18C900` | EXTRA displayed as selectable; real score/rank display retained |
| Visible song-list badges | `0x18D710` | EXTRA badge state 2 |
| Selected-song focus panel | `0x18D8F0` | EXTRA presentation and selected state agree |
| Selection state handoff | `0x18F100` | Availability boolean propagated into the native local/shared selection paths |
| Random-song candidates | `0x190530` | Include EXTRA while retaining category and rating-range filters |
| Sorted song lists | `0x190E30` | Include EXTRA difficulty rows and use four real chart scores for the score-sort average |
| Course random difficulty | `0x198550` | Include defined EXTRA in the native random choice |
| Difficulty screen | `0x19AAA0` | Native available path drives presentation, input bounds, and committed selection |
| Unlock track preview | `0x1D3F70` | Preview reflects the available EXTRA chart |

## Exact patch contracts

Addresses are RVAs in `H:\gc2_game\game_decrypted.exe`, preferred base `0x00400000`. Both the current IDB bytes and actual input-file bytes were read through the IDA-backed analysis and matched every original contract below.

| Site | RVA | Original | Replacement | Native destination |
| --- | --- | --- | --- | --- |
| Song list | `0x223F10` | `0F 85 CE 01 00 00` | `E9 CF 01 00 00 90` | `0x2240E4` |
| Other focus eligibility | `0x18C9D8` | `74 71` | `90 90` | Fall through into native drawing |
| Other focus available | `0x18CA03` | `75 24` | `EB 34` | `0x18CA39`, state 2 |
| List badge available | `0x18D7D3` | `74 48` | `EB 36` | `0x18D80B`, state 2 |
| Selected focus eligibility | `0x18D9B8` | `74 5C` | `90 90` | Fall through into native drawing |
| Selected focus available | `0x18D9DE` | `75 2D` | `EB 24` | `0x18DA04`, state 2 |
| Selection state available | `0x18F169` | `74 3D` | `EB 36` | `0x18F1A1`, boolean 1 |
| Random candidate available | `0x190B12` | `75 04` | `EB 50` | `0x190B64`, existing rating-range checks |
| Difficulty sort available | `0x191043` | `75 04` | `EB 3C` | `0x191081`, append chart row |
| Score sort available | `0x191132` | `75 06` | `EB 40` | `0x191174`, keep initialized EXTRA-present boolean |
| Course random available | `0x1987E6` | `74 04` | `90 90` | Fall through to include EXTRA |
| Difficulty screen available | `0x19AC29` | `74 33` | `90 90` | Native all-available path, still checks chart reference |
| Unlock preview available | `0x1D423E` | `74 1D` | `90 90` | Fall through to EXTRA-on drawing |

The nonempty chart-reference branches remain at `0x18C9C1`, `0x18D7C0`, `0x18D9A5`, `0x18F156`, `0x190AFC`, `0x191030`, `0x19111F`, `0x1987D3`, `0x19AC39`, and `0x1D4225`. Random candidate filtering before and after the bypass stays intact. Patches target existing decoded instructions in the same function, preserve instruction widths, and have no incoming code references into overwritten instruction interiors. Skipped grade-query regions contain no required drawing calls or record updates; drawing setup is preserved by the separate focus-panel writes.

## Reference census and protected behavior

All 13 direct calls to the native eligibility accessor `0x223B00` were classified across 12 functions. Nine functions are availability consumers covered above; the three remaining functions are deliberately preserved:

- `0x1DA110`: statistics and achievement/reward evaluation, including real chart counts, S-rank counters, and item grants.
- `0x1DC030` and `0x1DD970`: detect a genuine newly earned EXTRA unlock after play and enter its result animation.

All direct callers of the grade accessor `0x1F63C0` were also reviewed. Availability bypasses are local; rank badges, score calculations, real achievement criteria, the task dispatcher, and saved-result updates still read genuine records. The eligibility field census finds its game-data writer in `GC2_ProcessMusicSelectAll_Step` (`0x222200`) and its reader in `0x223B00`; neither is modified.

The selection-state handoff at `0x18F100` calls `0x14ACA0`. Its mode-specific callees update native selection fields (`0x14C220`) or serialize the existing selection fields (`0x14D350`). The patch changes the existing availability boolean only; packet structure and network implementation are untouched. This is static evidence of propagation, not proof of multiplayer acceptance.

The 4.71/4.74 profile remains its existing one-site implementation. There are no configuration, NESYS, input, audio, renderer, or widescreen changes in this correction.

## Evidence and validation

Repeatable read-only queries and results are under `.codex-tmp/gc206-unlock-2026-09-06/` in the source repository. The driver connects through `AgentSession.start(..., daemon=True, require_ida=True)`, probes the real IDA backend before each batch, and disconnects after each batch. No IDB mutation or save is performed.

- `query_entry.py`, `query_current_unlock.py`: original port mismatch between old and newer native routines.
- `query_difficulty_search.py`, `query_difficulty_owners.py`, `query_extra_consumers.py`, `query_difficulty_controls.py`, `query_unlock_evidence.py`: ownership, difficulty state, and input restrictions.
- `query_availability_census.py`, `query_remaining_consumers.py`, `query_availability_boundaries.py`: complete direct-reference census and consumer classification.
- `query_selection_state.py`: native shared-selection propagation and broader system-config consumers.
- `query_grade_meaning.py`: exact S/A/B/C/D/E/unplayed grade mapping.
- `query_complete_unlock_contracts.py`: all 13 real-image byte contracts, branch targets, instruction boundaries, preserved chart checks, and skipped calls. These are IDA analysis queries, not synthetic runtime tests.

The final [SongUnlock profile](../../src/Patches/SongUnlock/SongUnlockProfile.cpp) was edited and reread through CLion MCP. The file was opened in the editor before diagnostics; diagnostics returned no errors or warnings. Full `msvc32-debug` and `msvc32-release` build preset graphs completed successfully; logs are `build-debug.log` and `build-release.log` in the evidence directory. No synthetic tests or CTest run were added for this native patch correction.

## Operator acceptance and regression checklist

After this correction, the user reported: "Now it works, commit your work." This confirms resolution of the reported unlock problem. Individual observations for every scenario below were not supplied; the list remains a reference for broader regression coverage.

Use the complete enabled option on 2.06 with an account lacking the three S ranks and with guest/no-card play. Check song-list badges, both focus presentations, difficulty and score sorting, random candidates, difficulty-screen navigation, and actual EXTRA launch. A song without an EXTRA chart must still expose only its native three difficulties. Revisit the list after playing and confirm the policy stays consistent.

For local/shared selection, confirm the other player's presentation and final selected difficulty agree, including random/course paths. Separately verify real rank badges, scores, genuine earned-unlock notifications, item/reward progression, and saving retain their native behavior. Disable the option and confirm ordinary native locks return.

No runtime DLL deployment, game launch, or process lifecycle change was performed by the agent. Runtime acceptance above is the user's report.
