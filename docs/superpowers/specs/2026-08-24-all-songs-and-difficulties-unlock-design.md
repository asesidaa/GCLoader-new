# All Songs and Difficulties Unlock Design

**Status:** Approved on 2026-08-24

The original evidence and one-site implementation below describe 4.71 (also
called 4.74 for this binary). The 2.06 correction uses 13 guarded writes because
song membership and EXTRA availability are separate native consumers. See the
[complete 2.06 correction and consumer census](../../reverse-engineering/gc206-complete-song-unlock-2026-09-06.md)
for selection, sorting, random choice, badges, preserved progression behavior,
the user's runtime confirmation, and the regression checklist.

## Goal

Add an opt-in GCLoader runtime patch that makes every song, authored
difficulty, and eligible EXTRA chart selectable without a card or server while
leaving every other tournament-mode behavior disabled.

## Native Evidence

The supported analysis target is `H:\gc\game471.exe.i64`, loaded at preferred
image base `0x00400000`.

- `sub_657580` owns song/chart availability.
- At EA `0x657854` (RVA `0x00257854`), the clean instruction is
  `0F 85 1D 02 00 00` (`jnz 0x657A77`).
- The branch target is the tournament tune-availability path. It raises the
  playable difficulty/rating cap to 15 and marks eligible EXTRA tune data
  available.
- Replacing that instruction with `E9 1E 02 00 00 90` makes only that local
  availability decision unconditional.

This patch must not set the global tournament DWORD at SystemSetting payload
`+0x8C`. The score/judgement selectors, event-mode restrictions, card-save
suppression, timer behavior, and tournament item initialization all consume
that global flag separately.

## User Contract

The strict runtime configuration gains:

```toml
[experimental]
unlock_all_songs_and_difficulties = false
```

- `false`: do not read or write the optional song-unlock site.
- `true`: accept the exact clean bytes or exact already-patched bytes at RVA
  `0x00257854`; write only the clean form.
- Any unreadable address, unknown bytes, page-protection failure, copy failure,
  instruction-cache flush failure, or protection-restore failure aborts game
  startup. There is no fallback.
- The patch applies only in the game process. The NESYS process remains
  untouched.
- Changing the option requires a game restart.

The option unlocks only songs, difficulties, and eligible EXTRA charts. It
does not grant or alter gameplay items, avatars, titles, navigators, skins,
sound effects, judgement windows, score tables, chain score, event mode,
timers, or card/server saving.

## Architecture

Create a dedicated `gc::song_unlock` runtime-patch unit. It reuses
`ProductionGameBinaryPatchActions()` for guarded executable-image reads and
writes, but owns its one-site byte contract and logging. This avoids enabling
tournament mode and avoids adding the opt-in site to the mandatory four-site
game compatibility transaction.

Load the configuration normally, then call
`SongUnlockPatchInit(config.GetUnlockAllSongsAndDifficulties())` inside the
existing game-only startup path. A disabled option is a successful no-op; an
enabled failure returns `false` to `DllMain`.

## Verification Boundary

Static verification consists of:

- rerunning the read-only IDA 9 trace against `game471.exe.i64`;
- CLion inspections and compiler-model checks for every touched C++ file;
- complete x86 MSVC Debug and Release builds of `iDmacDrv32` and `ConfigGUI`;
- source diff and whitespace checks.

Those checks do not establish gameplay acceptance. A later operator run must
confirm song/difficulty/EXTRA availability and independently confirm normal
judgement, chain score, item inventory, event mode, and saving behavior.
