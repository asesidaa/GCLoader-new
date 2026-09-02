# Native Auto Play Safety Design

**Status:** Approved for implementation planning on 2026-09-03

## Goal

Add one opt-in GCLoader feature that enables Groove Coaster's native auto-play
judgement path while making that mode unsuitable for recording legitimate
results:

- gameplay judgement input and free taps are ignored by the game's own
  auto-play gate;
- every authored chart component, including HIDDEN/AD-LIB descriptors, is
  resolved by the native auto-play path at its authored chart timestamp;
- the game's native card-data save path and local result-CSV export are
  suppressed;
- a fixed, unmistakable in-game marker is drawn on every main render frame.

The feature is a single safety contract. Auto play, HIDDEN/AD-LIB completion,
save suppression, and the visible marker cannot be enabled or configured
independently.

## Supported Binary and Evidence

The supported analysis target is `H:\gc\game471.exe.i64`, with preferred image
base `0x00400000`. The corresponding deployed `game471.exe` has SHA-256
`FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.

The existing game-binary compatibility audit accounts for every `.text`
difference between this image and the clean `game_decrypted.exe`. None of those
differences overlap the sites in this design, so both forms of this game build
have the same auto-play, no-save, marker-hook, and native-text contracts.
Applicability still comes from exact bytes at each named RVA, not from a file
name or hash alone.

The reusable read-only IDA queries are stored under
`H:\gc\artifacts\GCLoader\.codex-tmp` and connect briefly to the existing
daemon with `AgentSession.connect(...)`. The decisive artifacts are:

| Artifact | SHA-256 |
|---|---|
| `game471-autoplay-patch-contract.json` | `06E2C1F788F4DD8BBE072D53E3952E2275D376954EF1A399331C2215022A423F` |
| `game471-autoplay-mute-audio-closure.json` | `B44046F98BCEBFE2F353BF64A54CA9CFCD7B4932325AD1218BA069972F7C10F9` |
| `game471-autoplay-grade-state.json` | `06DB7C31042170D84C1893F977F7C25BDE02818EBFCF7B84FEF07AC0A7136471` |
| `game471-debug-text-outer-frame-trace.json` | `FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7` |

These artifacts are analysis evidence, not runtime acceptance.

## Native Behavior

### Auto-play ownership and input suppression

The byte at `GameplayJudgementState + 0xA5` is the game's native auto-play
state. Native demo play sets this state, and tutorial commands can toggle it.
Rather than trying to find and reproduce every mode transition, the proposed
runtime patch changes its six-byte getter to return true.

The getter is consumed throughout the judgement lifecycle. In particular:

- `sub_5D1720` resolves eligible chart descriptors when chart time reaches
  their target time and writes the authored target time into the result;
- `GameplayJudgementState_ProcessFreeInput` at EA `0x005D2040` returns before
  querying physical gameplay controls or setting the left/right free-tap flags
  at `+0xED` and `+0xEE`;
- all manual note-type handlers guarded by the getter are bypassed;
- `GameplayResult_ExportCsv` at EA `0x005D61A0` returns before formatting the
  score and per-note timing CSV.

This already supplies the requested input policy. GCLoader must not inject
synthetic booster input and must not add a lower-level input block. Menu,
pause, service, and exit controls stay on their existing paths; only gameplay
judgement/free-tap consumption is suppressed by the native gate.

### HIDDEN/AD-LIB descriptors and authored sound

The adjacent `GameplayJudgementState + 0xA6` state controls whether the
auto-play helper may complete a descriptor whose native `IsMute` property is
set. `IsMute` is not a request to silence all note sounds. It is a descriptor
property that includes player-facing HIDDEN/AD-LIB content:

- with `+0xA6` false, native auto play sends such a descriptor through its miss
  path;
- with `+0xA6` true, the descriptor takes the same exact-target completion path
  as other authored descriptors;
- successful completion reaches `sub_5D07D0`, publishes the per-frame
  HIDDEN/AD-LIB completion flag at judgement-state `+0xAA`, and
  `CTuneGameManager_ProcessJudgementFrame` calls
  `GC120FPS_CSoundManager_PlayArrangeSE_DoubleBuffered` at EA `0x00611760`.

Therefore the feature forces both the `+0xA5` and `+0xA6` getters. This keeps
authored HIDDEN/AD-LIB arrangement/keysound content in songs that depend on it.
It does not synthesize the separate generic left/right tap sound; those sounds
are driven by free-input flags, which auto play intentionally suppresses.

### Native result grade

The auto-play helper records the authored target timestamp and normally writes
grade value `3`, which this build labels `GREAT`. Its only alternate branch
tests the adjacent `+0xA7` byte and would write grade `2` (`COOL`) once before
resetting that byte.

The supported binary initializes `+0xA7` to zero. The judgement-state ownership
scan found no statically referenced setter or direct store that sets it to one;
the only explicit post-construction write resets it to zero. GCLoader therefore
does not patch grade logic or `+0xA7`. Under the supported native state,
auto-play results remain GREAT with the result timestamp equal to the
descriptor's authored timestamp.

This is a judgement-time guarantee. Audible arrangement playback still occurs
when the native frame lifecycle processes that completed descriptor; the
design does not claim sample-level audio scheduling beyond the game's own
sound path.

### Built-in no-save path

The game loads the relative path `data/expconfig.cfg`; in the current runtime
layout it resolves to `H:\gc\data\expconfig.cfg`. Its parser at EA
`0x006698E0` reads `DoNotSaveCardData` into scheduler byte `+0x02`.

At the start of `ProcessSavePlayerData` (`sub_61D1C0`), that byte is checked
before the save state machine creates or submits its player/card-data work. A
true value skips directly to the post-save portion of the state machine. The
feature forces the parsed value to true with the game's existing built-in
policy; it does not edit `expconfig.cfg` on disk and does not reimplement the
NESYS save protocol.

Together, this flag and the `+0xA5` CSV guard provide two native containment
layers:

1. no player/card-data save transaction is started by the normal finish-game
   save state machine;
2. no local gameplay-result CSV is emitted by the native diagnostic exporter.

Static analysis does not prove that every external deployment lacks another
independent persistence path. Runtime acceptance must additionally verify the
actual NESYS traffic and server/card state described below.

## Configuration Contract

The strict runtime document gains one required experimental Boolean:

```toml
[experimental]
enable_auto_play = false
```

- `false`: perform no auto-play feature memory reads, writes, native-function
  resolution, or hook installation.
- `true`: install the complete four-part safety contract or abort game-process
  startup. Partial operation is forbidden.
- The setting is copied into immutable validated per-launch settings. It is not
  hot-reloadable; changing it requires a game restart.
- The setting applies only to the game process. The NESYS process neither
  parses nor installs this feature.

There are no settings for accuracy, HIDDEN/AD-LIB handling, input policy,
save behavior, marker text, marker position, marker color, or marker opacity.

ConfigGUI exposes one checkbox labelled `Native auto play`. While checked, it
shows an adjacent warning that gameplay input/free taps are ignored, card and
score saving are disabled, an in-game marker is mandatory, and restart is
required. Saving and reopening ConfigGUI must round-trip the one Boolean
without changing unrelated settings.

## Executable Contracts

Every address is an RVA from the loaded main executable. Production code
resolves the actual module base and checks every address calculation for
overflow.

### Direct runtime patches

| Contract | RVA | Clean bytes | Patched bytes | Effect |
|---|---:|---|---|---|
| Native auto-play getter | `0x0003CADA` | `8A 80 A5 00 00 00` | `B0 01 90 90 90 90` | Return true instead of reading judgement-state `+0xA5` |
| Auto-complete `IsMute` descriptors | `0x0003CAFA` | `8A 80 A6 00 00 00` | `B0 01 90 90 90 90` | Return true instead of reading judgement-state `+0xA6` |
| Force `DoNotSaveCardData` | `0x00269951` | `0F 95 C1` | `B1 01 90` | Store true for the parsed native no-save flag regardless of file value |

Each direct site accepts its exact clean or exact already-patched form. Only a
clean site is written. An unreadable site or any other byte sequence is an
unsupported/conflicting image and fails before mutation.

### Marker hook and native text target

| Contract | RVA | Expected bytes | Use |
|---|---:|---|---|
| Marker producer seam | `0x00058BE9` | `8D 44 24 08 50 E8 8D 03 00 00` | Install a SafetyHook mid-hook immediately before the native render-subscriber dispatcher |
| Native debug text routine | `0x00069650` | `55 8B EC 6A FF` | Call the game's `__cdecl` formatted-text producer |

The marker seam is inside `GWMain`'s render phase, after the render phase has
passed its native `CanRun` check and immediately before
`GWSystemEventRender_DispatchSubscribers`. It is reached once per main render
frame at both native and elevated frame rates. Queuing text here lets the
native debug-display subscriber consume it in the same render frame.

The hook site must match the exact native form. GCLoader does not attempt to
recognize, chain through, or overwrite another component's detour. The text
routine is not hooked, but its prologue is preflighted before its fixed RVA is
used as a callable ABI contract.

## Visible Marker Contract

After the installation transaction commits, the render hook queues these two
fixed strings on every main render frame:

```text
AUTO PLAY
SCORE SAVE DISABLED
```

The text uses the game's own debug-display instance and fixed logical canvas
coordinates:

- first line at `(32.0, 32.0)`;
- second line at `(32.0, 52.0)`;
- opaque bright yellow foreground (`AARRGGBB 0xFFFFFF00`);
- an opaque black copy offset by `(2.0, 2.0)` behind each line so the plain
  glyphs remain readable over bright gameplay content.

The marker is deliberately not conditional on being inside a song. It remains
present on every screen produced by the main render loop, including gameplay
and result screens, for the whole committed process lifetime. It uses no
custom font, D3D resource, animation, or effect and must not depend on the
windowed-widescreen option. The logical placement remains inside the original
game canvas so normal and widescreen composition both retain it.

The callback performs no allocation owned by GCLoader, no configuration read,
and no logging on the successful per-frame path. It preserves the intercepted
register/stack context and never lets a C++ or structured exception cross the
hook. A guarded native-text call failure publishes a one-shot fatal runtime
error and ends the game process; continuing playable auto play without its
marker is not an allowed fallback.

## Runtime Architecture

Add a feature-owned `gc::auto_play` unit under `src/Patches/AutoPlay/`. It owns:

- the three direct byte contracts;
- marker-hook and native-text ABI contracts;
- checked planning and installation;
- transactional rollback;
- the inactive/committed marker state;
- structured diagnostics and the one-shot runtime marker failure path.

The unit reuses `ProductionGameBinaryPatchActions()` for guarded executable
reads and writes and uses the repository's established SafetyHook mechanism
for the one real detour. It does not add these opt-in sites to the mandatory
game compatibility patch, does not add a general overlay framework, and does
not couple auto play to the framerate or windowed-widescreen implementations.

`ValidatedConfig` stores the compiled Boolean and exposes it through its
immutable per-launch interface. The patch entry point accepts that Boolean;
there is no separate settings object for a one-field feature.

`DllMain` invokes `AutoPlayPatchInit(settings.enable_auto_play())` after validated
configuration and input-runtime configuration are available, before the other
optional gameplay patches. This is still before the game can parse
`data/expconfig.cfg` or enter its main render loop. A false result is a
fail-closed DLL attach and no later feature initialization runs.

## Installation Transaction

When enabled, initialization performs this sequence:

1. Resolve the loaded main-module base and all five contract addresses with
   checked arithmetic.
2. Read and classify all three direct patch sites.
3. Verify the exact native bytes at the marker hook seam and native text
   routine.
4. Reject any read failure, unknown direct bytes, hook conflict, or callable
   ABI mismatch before changing memory.
5. Construct the feature runtime with its atomic marker state inactive.
6. Install the marker hook. While inactive, the callback is a no-op.
7. Write only clean direct sites in this safety order:
   `DoNotSaveCardData`, `+0xA6`, then `+0xA5`. The auto-play activation getter
   is the final executable write.
8. Publish the marker state active with a non-failing atomic store and commit
   the transaction.

If hook installation or any direct write fails, initialization resets the hook
and restores every direct site that this invocation changed, in reverse order.
Sites already patched on entry are never restored because GCLoader did not own
those writes. Rollback success or failure is included in the structured error,
and startup always aborts after an installation failure.

Installing the dormant hook before the direct writes and writing `+0xA5` last
keeps auto play behind both the no-save policy and a ready marker path. The
game loop is not allowed to start until the final active publication commits.

Repeated initialization within the same process returns the prior committed
result without reinstalling. No patch or hook is restored during normal DLL
detach; all state is process-local and disappears at process exit.

## Diagnostics

Successful startup emits one concise warning-level record, for example:

```text
AutoPlayPatch: state=enabled direct_patched=3 direct_existing=0 marker=active score_save=disabled
```

A disabled startup emits one informational `state=disabled` record. There is
no per-frame, per-note, or per-input logging.

Initialization errors identify:

- failure stage and contract name;
- RVA;
- expected clean and patched bytes where applicable;
- actual bytes when readable;
- memory or hook operation;
- Windows error where available;
- whether rollback was attempted and completed.

The user-facing fatal title is `GCLoader auto play setup failed`. Its message
states that GCLoader refused to continue because it could not guarantee both
save suppression and the visible marker, and directs the operator to
`loader-log.txt` for the exact contract failure.

## Test Contract

Automated tests exercise production-facing configuration and the transaction
through injected memory and hook actions. They do not patch a live process or
claim gameplay/visual success.

Required configuration cases are:

- the distributed `config.toml` strictly parses with
  `enable_auto_play = false`;
- omitting the required field fails strict parsing;
- false and true values survive parse, compile, immutable settings transfer,
  serialization, and reparse;
- ConfigGUI changes only the one field.

Required transaction cases are:

- disabled initialization performs zero reads, writes, hook operations, or
  native-target resolution;
- all eight clean/already-patched combinations across the three direct sites
  are accepted, and only clean sites are written;
- the hook is installed for every enabled combination, including an image
  whose three direct sites were already patched;
- an unknown value or read failure at each direct site causes zero mutation;
- a marker-seam mismatch, text-target mismatch, address overflow, or invalid
  action table causes zero mutation;
- hook installation failure occurs before any direct write;
- failure at each direct-write position resets the hook and restores only the
  writes owned by that invocation;
- rollback failure is reported and can never publish committed/active state;
- successful writes occur in no-save, `+0xA6`, `+0xA5` order, and marker
  activation occurs only after the last write succeeds;
- a second initialization after commit performs no additional operations.

A focused marker-producer test may use an injected native-text sink to verify
that inactive state emits nothing and committed state emits the fixed two-line
foreground/shadow call set once per callback. That test proves only the
producer contract; it does not prove native rendering, visibility, placement,
or z-order.

The small binary fixtures must record their independent provenance from the
IDA artifacts above. Tests must not scrape the production source or duplicate
an entire executable.

## Static Verification and Runtime Acceptance

Static verification requires:

1. rerun the saved read-only IDA scripts against the current
   `game471.exe.i64` daemon and retain artifact hashes;
2. build affected x86 Debug and Release targets, including `iDmacDrv32` and
   `ConfigGUI`;
3. run focused auto-play/configuration tests and the full Debug and Release
   CTest suites;
4. inspect the built DLL for the fixed marker strings and named contract RVAs;
5. run `git diff --check` and inspect the final working-tree/staged state.

These checks prove buildability, byte classification, transaction behavior,
rollback, and static integration only. Runtime acceptance requires an
operator-run game and must separately establish:

- with the setting false, normal gameplay input, free taps, card saving, CSV
  behavior, and absence of the marker remain unchanged;
- with the setting true, booster input cannot create judgements or free taps,
  while pause, service, and exit controls remain usable;
- a representative chart containing taps, holds, slides, scratches, paired or
  dual components, and HIDDEN/AD-LIB descriptors finishes with native GREAT
  results at authored timestamps and no auto-play misses;
- authored HIDDEN/AD-LIB arrangement sounds remain audible, with no synthetic
  generic tap sounds from ignored gameplay input;
- both lines of the marker remain legible and unobscured throughout gameplay
  and the result screen at normal output and with windowed widescreen enabled;
- no new result CSV is produced;
- NESYS capture shows no player/card-data save transaction from the normal
  finish-game path, and server/card state confirms that score, clear/rank
  state, currency, unlocks, and inventory did not persist;
- after restarting with the setting false, the marker is absent and normal
  saving resumes.

Deployment to `H:\gc`, launching the game, and cabinet acceptance are not part
of implementation unless separately requested.

## Non-Goals

- Input injection, replay files, chart prediction, or a new auto-play engine.
- Blocking keyboard/controller/FastIO input below the judgement layer.
- Configurable accuracy, deliberate misses, or per-note behavior.
- Patching the `+0xA7` grade state or changing native score formulas.
- A separately selectable HIDDEN/AD-LIB, no-save, CSV, or marker option.
- Editing `H:\gc\data\expconfig.cfg` or either executable on disk.
- Server, database, protocol, or NESYS-process changes.
- A custom D3D font, overlay window, animated marker, or reusable overlay
  framework.
- Hot reload or disabling the marker during a committed process lifetime.
- Support for a different game build without a new exact-byte audit.

## Expected Source Ownership

Implementation is expected to touch only the feature and its direct integration
surfaces:

- `src/Patches/AutoPlay/` for contracts, transaction, marker, and diagnostics;
- `src/Patches/CMakeLists.txt` for the production target;
- `src/Config/ConfigDocument.h` and `ConfigCompiler.*` for the strict immutable
  setting;
- `src/Loader/DllMain.cpp` for game-only startup;
- `tools/ConfigGUI/Main.cpp` for the checkbox and warning;
- `config.toml` for the required disabled default;
- focused files under `tests/Config/` and `tests/Patches/AutoPlay/`, plus the
  test target registration.

No unrelated framerate, input, audio, NESYS, renderer, widescreen, or game
compatibility behavior is part of this feature.
